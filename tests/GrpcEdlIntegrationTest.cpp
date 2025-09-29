#include <iostream>
#include <memory>
#include <string>
#include <filesystem>
#include <thread>
#include <chrono>
#include <future>
#include <fstream>
#include <cstdlib>
#include <regex>

#include <grpcpp/grpcpp.h>
#include "audio_engine.grpc.pb.h"
#include "util/EdlJson.h"

#include <juce_core/juce_core.h>

#ifndef PROJECT_SOURCE_DIR
#define PROJECT_SOURCE_DIR "."
#endif

// Helper function to get absolute path from project root
static std::string absFromProject(const char* rel) {
    juce::File root(PROJECT_SOURCE_DIR);
    return root.getChildFile(rel).getFullPathName().toStdString();
}

// Helper function to get absolute path to fixture files
static std::string fixturePath(const char* name) {
    return absFromProject((std::string("fixtures/") + name).c_str());
}

// Helper function to create absolute path in output directory
static std::string getOutputPath(const char* relativePath) {
    juce::File root(PROJECT_SOURCE_DIR);
    juce::File outputPath = root.getChildFile("out").getChildFile(relativePath);
    // Ensure parent directory exists
    outputPath.getParentDirectory().createDirectory();
    return outputPath.getFullPathName().toStdString();
}

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::Status;

namespace fs = std::filesystem;

class EdlTestClient {
private:
    std::unique_ptr<audio_engine::AudioEngine::Stub> stub_;

public:
    EdlTestClient(std::shared_ptr<Channel> channel)
        : stub_(audio_engine::AudioEngine::NewStub(channel)) {}

    bool UpdateEdl(const std::string& edlPath, std::string& edlId, std::string& revision,
                   int& trackCount, int& clipCount, bool replace = false) {
        // Read JSON file
        std::string jsonString, error;
        if (!juceaudioservice::EdlJson::readJsonFromFile(edlPath, jsonString, error)) {
            std::cout << "Failed to read EDL file: " << error << std::endl;
            return false;
        }

        // Parse JSON to EDL
        audio_engine::Edl edl;
        if (!juceaudioservice::EdlJson::parseFromJson(jsonString, edl, error)) {
            std::cout << "Failed to parse EDL JSON: " << error << std::endl;
            return false;
        }

        // Call UpdateEdl RPC
        audio_engine::UpdateEdlRequest request;
        request.mutable_edl()->CopyFrom(edl);
        request.set_replace(replace);

        audio_engine::UpdateEdlResponse response;
        ClientContext context;

        Status status = stub_->UpdateEdl(&context, request, &response);

        if (!status.ok()) {
            std::cout << "UpdateEdl RPC failed: " << status.error_message() << std::endl;
            return false;
        }

        edlId = response.edl_id();
        revision = response.revision();
        trackCount = response.track_count();
        clipCount = response.clip_count();

        return true;
    }

    bool RenderEdlWindow(const std::string& edlId, double startSec, double durSec,
                        const std::string& outputPath, std::string& finalChecksum, int bitDepth = 16) {
        // Convert seconds to samples (assume 48kHz)
        const int sampleRate = 48000;
        int64_t startSamples = static_cast<int64_t>(startSec * sampleRate);
        int64_t durationSamples = static_cast<int64_t>(durSec * sampleRate);

        audio_engine::RenderEdlWindowRequest request;
        request.set_edl_id(edlId);
        request.mutable_range()->set_start_samples(startSamples);
        request.mutable_range()->set_duration_samples(durationSamples);
        request.set_out_path(outputPath);
        request.set_bit_depth(bitDepth);

        ClientContext context;
        std::unique_ptr<ClientReader<audio_engine::EngineEvent>> reader(
            stub_->RenderEdlWindow(&context, request));

        audio_engine::EngineEvent event;
        bool success = false;
        bool hasProgress = false;

        while (reader->Read(&event)) {
            if (event.has_progress()) {
                hasProgress = true;
            } else if (event.has_complete()) {
                const auto& complete = event.complete();
                finalChecksum = complete.sha256();
                success = true;
            } else if (event.has_edl_error()) {
                const auto& error = event.edl_error();
                std::cout << "EDL Error: " << error.reason() << std::endl;
                return false;
            }
        }

        Status status = reader->Finish();
        if (!status.ok()) {
            std::cout << "RenderEdlWindow RPC failed: " << status.error_message() << std::endl;
            return false;
        }

        return success && hasProgress;
    }

    bool Subscribe(const std::string& edlId, int maxEvents, std::vector<std::string>& events) {
        audio_engine::SubscribeRequest request;
        request.set_session(edlId);

        ClientContext context;
        std::unique_ptr<ClientReader<audio_engine::EngineEvent>> reader(
            stub_->Subscribe(&context, request));

        audio_engine::EngineEvent event;
        std::string eventJson, error;
        int eventCount = 0;

        // Set a timeout for the subscribe test
        auto start = std::chrono::steady_clock::now();
        auto timeout = std::chrono::seconds(10);

        while (reader->Read(&event) && eventCount < maxEvents) {
            // Convert event to NDJSON
            if (juceaudioservice::EdlJson::eventToJson(event, eventJson, error)) {
                events.push_back(eventJson);
                eventCount++;
            }

            // Check timeout
            if (std::chrono::steady_clock::now() - start > timeout) {
                break;
            }
        }

        // Note: We expect the reader to be cancelled when we exit the loop
        // This is normal behavior for the subscribe test
        return eventCount > 0;
    }
};

// Start test server on given address
std::unique_ptr<std::future<void>> startTestServer(const std::string& address) {
    // Extract port from address
    size_t colonPos = address.find(':');
    std::string port = (colonPos != std::string::npos) ? address.substr(colonPos + 1) : "50051";

    // Launch server binary
    std::string serverPath = absFromProject("build/bin/audio_engine_server");
    std::string command = serverPath + " --port " + port;

    auto future = std::make_unique<std::future<void>>(
        std::async(std::launch::async, [command]() {
            std::system(command.c_str());
        })
    );

    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    return future;
}

bool testEdlUpdate() {
    std::cout << "Testing EDL Update..." << std::endl;

    // Start server
    std::string serverAddress = "localhost:50053";
    auto serverFuture = startTestServer(serverAddress);
    if (!serverFuture) {
        std::cout << "Failed to start test server" << std::endl;
        return false;
    }

    // Create client
    auto channel = grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials());
    EdlTestClient client(channel);

    // Test update EDL
    std::string edlPath = fixturePath("test_edl.json");
    std::string edlId, revision;
    int trackCount, clipCount;

    bool result = client.UpdateEdl(edlPath, edlId, revision, trackCount, clipCount);

    if (result) {
        std::cout << "  EDL ID: " << edlId << std::endl;
        std::cout << "  Revision: " << revision << std::endl;
        std::cout << "  Track Count: " << trackCount << std::endl;
        std::cout << "  Clip Count: " << clipCount << std::endl;

        // Validate results
        if (edlId.empty()) {
            std::cout << "ERROR: EDL ID is empty" << std::endl;
            result = false;
        }
        if (trackCount != 1) {
            std::cout << "ERROR: Expected 1 track, got " << trackCount << std::endl;
            result = false;
        }
        if (clipCount != 1) {
            std::cout << "ERROR: Expected 1 clip, got " << clipCount << std::endl;
            result = false;
        }
    }

    std::cout << "EDL Update test " << (result ? "passed" : "failed") << std::endl;
    return result;
}

bool testEdlRender() {
    std::cout << "Testing EDL Render..." << std::endl;

    // Start server
    std::string serverAddress = "localhost:50054";
    auto serverFuture = startTestServer(serverAddress);
    if (!serverFuture) {
        std::cout << "Failed to start test server" << std::endl;
        return false;
    }

    // Create client
    auto channel = grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials());
    EdlTestClient client(channel);

    // First update EDL to get ID
    std::string edlPath = fixturePath("test_edl.json");
    std::string edlId, revision;
    int trackCount, clipCount;

    if (!client.UpdateEdl(edlPath, edlId, revision, trackCount, clipCount)) {
        std::cout << "Failed to update EDL for render test" << std::endl;
        return false;
    }

    // Test render 250ms window
    std::string outputPath = getOutputPath("test_edl_render.wav");
    std::string checksum;
    bool result = client.RenderEdlWindow(edlId, 0.0, 0.25, outputPath, checksum, 16);

    if (result) {
        std::cout << "  Output: " << outputPath << std::endl;
        std::cout << "  SHA256: " << checksum << std::endl;

        // Validate results
        if (!fs::exists(outputPath)) {
            std::cout << "ERROR: Output file does not exist" << std::endl;
            result = false;
        }
        if (checksum.empty() || checksum.length() != 64) {
            std::cout << "ERROR: Invalid SHA256 checksum (expected 64 hex chars)" << std::endl;
            result = false;
        }
        // Validate checksum is hex
        std::regex hexPattern("^[0-9a-fA-F]{64}$");
        if (!std::regex_match(checksum, hexPattern)) {
            std::cout << "ERROR: SHA256 checksum is not valid hex" << std::endl;
            result = false;
        }
    }

    std::cout << "EDL Render test " << (result ? "passed" : "failed") << std::endl;
    return result;
}

bool testEdlSubscribe() {
    std::cout << "Testing EDL Subscribe..." << std::endl;

    // Start server
    std::string serverAddress = "localhost:50055";
    auto serverFuture = startTestServer(serverAddress);
    if (!serverFuture) {
        std::cout << "Failed to start test server" << std::endl;
        return false;
    }

    // Create client
    auto channel = grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials());
    EdlTestClient client(channel);

    // First update EDL to get ID
    std::string edlPath = fixturePath("test_edl.json");
    std::string edlId, revision;
    int trackCount, clipCount;

    if (!client.UpdateEdl(edlPath, edlId, revision, trackCount, clipCount)) {
        std::cout << "Failed to update EDL for subscribe test" << std::endl;
        return false;
    }

    // Start subscribe in background thread
    std::vector<std::string> events;
    std::future<bool> subscribeResult = std::async(std::launch::async, [&]() {
        return client.Subscribe(edlId, 5, events);  // Collect up to 5 events
    });

    // Give subscribe time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Trigger an event by doing another render
    std::string outputPath = getOutputPath("test_edl_subscribe.wav");
    std::string checksum;
    client.RenderEdlWindow(edlId, 0.0, 0.1, outputPath, checksum, 16);

    // Wait for subscribe to get events (with timeout)
    auto status = subscribeResult.wait_for(std::chrono::seconds(5));
    bool result = (status == std::future_status::ready) && subscribeResult.get();

    if (result) {
        std::cout << "  Received " << events.size() << " events" << std::endl;

        // Validate events
        if (events.empty()) {
            std::cout << "ERROR: No events received" << std::endl;
            result = false;
        } else {
            // Check that events are valid JSON
            for (const auto& event : events) {
                if (event.empty() || event.front() != '{') {
                    std::cout << "ERROR: Event is not valid NDJSON: " << event << std::endl;
                    result = false;
                    break;
                }
            }
        }
    }

    std::cout << "EDL Subscribe test " << (result ? "passed" : "failed") << std::endl;
    return result;
}

int main(int argc, char** argv) {
    std::cout << "Running gRPC EDL Integration Tests..." << std::endl;

    bool allTestsPassed = true;

    try {
        // Test 1: EDL Update
        if (!testEdlUpdate()) {
            allTestsPassed = false;
        }

        // Test 2: EDL Render
        if (!testEdlRender()) {
            allTestsPassed = false;
        }

        // Test 3: EDL Subscribe
        if (!testEdlSubscribe()) {
            allTestsPassed = false;
        }

    } catch (const std::exception& e) {
        std::cout << "Test exception: " << e.what() << std::endl;
        allTestsPassed = false;
    }

    std::cout << "All EDL integration tests " << (allTestsPassed ? "PASSED" : "FAILED") << std::endl;
    return allTestsPassed ? 0 : 1;
}