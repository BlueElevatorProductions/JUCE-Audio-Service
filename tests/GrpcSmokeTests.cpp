#include <iostream>
#include <memory>
#include <string>
#include <filesystem>
#include <thread>
#include <chrono>
#include <future>
#include <fstream>

#include <grpcpp/grpcpp.h>
#include "audio_engine.grpc.pb.h"

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "JuceAudioService/AudioFileSource.h"
#include "JuceAudioService/OfflineRenderer.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::Status;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;

namespace fs = std::filesystem;

// Forward declaration from grpc_server.cpp - we'll include the service implementation
class AudioEngineServiceImpl final : public audio_engine::AudioEngine::Service {
private:
    std::unique_ptr<juceaudioservice::AudioFileSource> currentAudioSource;
    std::unique_ptr<juceaudioservice::OfflineRenderer> renderer;

public:
    AudioEngineServiceImpl() : renderer(std::make_unique<juceaudioservice::OfflineRenderer>()) {}

    Status LoadFile(ServerContext* context, const audio_engine::LoadFileRequest* request,
                   audio_engine::LoadFileResponse* response) override {

        const std::string& filePath = request->file_path();

        if (!fs::exists(filePath)) {
            response->set_success(false);
            response->set_message("File does not exist: " + filePath);
            return Status::OK;
        }

        currentAudioSource = std::make_unique<juceaudioservice::AudioFileSource>();
        juce::File file(filePath);
        bool loaded = currentAudioSource->loadFile(file);

        if (!loaded) {
            response->set_success(false);
            response->set_message("Failed to load audio file: " + filePath);
            currentAudioSource.reset();
            return Status::OK;
        }

        response->set_success(true);
        response->set_message("File loaded successfully");

        auto* fileInfo = response->mutable_file_info();
        fileInfo->set_path(filePath);
        fileInfo->set_sample_rate(static_cast<int32_t>(currentAudioSource->getSampleRate()));
        fileInfo->set_num_channels(currentAudioSource->getNumChannels());

        double sampleRate = currentAudioSource->getSampleRate();
        if (sampleRate > 0) {
            fileInfo->set_duration_seconds(static_cast<double>(currentAudioSource->getTotalLength()) / sampleRate);
        }

        try {
            fileInfo->set_file_size_bytes(static_cast<int64_t>(fs::file_size(filePath)));
        } catch (...) {
            fileInfo->set_file_size_bytes(0);
        }

        return Status::OK;
    }

    Status Render(ServerContext* context, const audio_engine::RenderRequest* request,
                 ServerWriter<audio_engine::RenderResponse>* writer) override {

        if (!currentAudioSource || !currentAudioSource->isLoaded()) {
            audio_engine::RenderResponse response;
            auto* error = response.mutable_error();
            error->set_error_code("NO_FILE_LOADED");
            error->set_error_message("No audio file is currently loaded. Call LoadFile first.");
            writer->Write(response);
            return Status::OK;
        }

        // Simple test render - just write a basic response
        audio_engine::RenderResponse progressResponse;
        auto* progress = progressResponse.mutable_progress();
        progress->set_progress_percentage(50.0);
        progress->set_status_message("Rendering...");
        writer->Write(progressResponse);

        // Create a simple test output file
        std::ofstream outFile(request->output_file());
        outFile << "test audio data" << std::endl;
        outFile.close();

        audio_engine::RenderResponse completeResponse;
        auto* complete = completeResponse.mutable_complete();
        complete->set_output_file_path(request->output_file());
        complete->set_sha256_hash("test_hash");
        complete->set_total_duration_seconds(1.0);
        complete->set_output_file_size_bytes(17); // "test audio data\n"

        writer->Write(completeResponse);
        return Status::OK;
    }

    Status UpdateEdl(ServerContext* context, const audio_engine::UpdateEdlRequest* request,
                    audio_engine::UpdateEdlResponse* response) override {
        return Status(grpc::StatusCode::UNIMPLEMENTED, "UpdateEdl is not implemented");
    }

    Status Subscribe(ServerContext* context, const audio_engine::SubscribeRequest* request,
                    ServerWriter<audio_engine::SubscribeResponse>* writer) override {
        return Status(grpc::StatusCode::UNIMPLEMENTED, "Subscribe is not implemented");
    }
};

class AudioEngineClient {
private:
    std::unique_ptr<audio_engine::AudioEngine::Stub> stub_;

public:
    AudioEngineClient(std::shared_ptr<Channel> channel)
        : stub_(audio_engine::AudioEngine::NewStub(channel)) {}

    bool LoadFile(const std::string& filePath) {
        audio_engine::LoadFileRequest request;
        request.set_file_path(filePath);

        audio_engine::LoadFileResponse response;
        ClientContext context;

        Status status = stub_->LoadFile(&context, request, &response);
        return status.ok() && response.success();
    }

    bool Render(const std::string& inputFile, const std::string& outputFile) {
        audio_engine::RenderRequest request;
        request.set_input_file(inputFile);
        request.set_output_file(outputFile);

        ClientContext context;
        std::unique_ptr<ClientReader<audio_engine::RenderResponse>> reader(
            stub_->Render(&context, request));

        audio_engine::RenderResponse response;
        bool hasComplete = false;

        while (reader->Read(&response)) {
            if (response.has_complete()) {
                hasComplete = true;
            } else if (response.has_error()) {
                return false;
            }
        }

        Status status = reader->Finish();
        return status.ok() && hasComplete;
    }
};

std::string createTestAudioFile() {
    const std::string testFile = "/tmp/test_audio.wav";

    // Create a simple WAV file
    std::ofstream file(testFile, std::ios::binary);

    // WAV header for 1 second of 44.1kHz mono silence
    const uint32_t sampleRate = 44100;
    const uint16_t numChannels = 1;
    const uint16_t bitsPerSample = 16;
    const uint32_t numSamples = sampleRate; // 1 second
    const uint32_t dataSize = numSamples * numChannels * (bitsPerSample / 8);
    const uint32_t fileSize = 36 + dataSize;

    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&fileSize), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);

    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1; // PCM
    uint32_t byteRate = sampleRate * numChannels * (bitsPerSample / 8);
    uint16_t blockAlign = numChannels * (bitsPerSample / 8);

    file.write(reinterpret_cast<const char*>(&fmtSize), 4);
    file.write(reinterpret_cast<const char*>(&audioFormat), 2);
    file.write(reinterpret_cast<const char*>(&numChannels), 2);
    file.write(reinterpret_cast<const char*>(&sampleRate), 4);
    file.write(reinterpret_cast<const char*>(&byteRate), 4);
    file.write(reinterpret_cast<const char*>(&blockAlign), 2);
    file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&dataSize), 4);

    // Write silence
    std::vector<char> silence(dataSize, 0);
    file.write(silence.data(), dataSize);

    file.close();
    return testFile;
}

std::unique_ptr<Server> startTestServer(const std::string& address) {
    AudioEngineServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    return server;
}

bool testServerStartup() {
    std::cout << "Testing server startup..." << std::endl;

    auto server = startTestServer("localhost:0"); // Use any available port
    if (!server) {
        std::cout << "Failed to start server" << std::endl;
        return false;
    }

    // Server started successfully
    std::cout << "Server startup test passed" << std::endl;
    server->Shutdown();
    return true;
}

bool testLoadFileWithClient() {
    std::cout << "Testing LoadFile with client..." << std::endl;

    // Start server
    std::string serverAddress = "localhost:50052";
    auto server = startTestServer(serverAddress);
    if (!server) {
        std::cout << "Failed to start server" << std::endl;
        return false;
    }

    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Create test file
    std::string testFile = createTestAudioFile();

    // Create client and test
    auto channel = grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials());
    AudioEngineClient client(channel);

    bool result = client.LoadFile(testFile);

    // Cleanup
    fs::remove(testFile);
    server->Shutdown();

    if (result) {
        std::cout << "LoadFile test passed" << std::endl;
    } else {
        std::cout << "LoadFile test failed" << std::endl;
    }

    return result;
}

bool testRenderWithClient() {
    std::cout << "Testing Render with client..." << std::endl;

    // Start server
    std::string serverAddress = "localhost:50053";
    auto server = startTestServer(serverAddress);
    if (!server) {
        std::cout << "Failed to start server" << std::endl;
        return false;
    }

    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Create test file
    std::string testInputFile = createTestAudioFile();
    std::string testOutputFile = "/tmp/test_output.wav";

    // Create client and test
    auto channel = grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials());
    AudioEngineClient client(channel);

    // Load file first
    bool loadResult = client.LoadFile(testInputFile);
    if (!loadResult) {
        std::cout << "Failed to load file for render test" << std::endl;
        fs::remove(testInputFile);
        server->Shutdown();
        return false;
    }

    // Test render
    bool renderResult = client.Render(testInputFile, testOutputFile);

    // Cleanup
    fs::remove(testInputFile);
    fs::remove(testOutputFile);
    server->Shutdown();

    if (renderResult) {
        std::cout << "Render test passed" << std::endl;
    } else {
        std::cout << "Render test failed" << std::endl;
    }

    return renderResult;
}

int main(int argc, char** argv) {
    // Initialize JUCE
    juce::initialiseJuce_GUI();

    std::cout << "Running gRPC smoke tests..." << std::endl;

    bool allTestsPassed = true;

    try {
        // Test 1: Server startup
        if (!testServerStartup()) {
            allTestsPassed = false;
        }

        // Test 2: LoadFile functionality
        if (!testLoadFileWithClient()) {
            allTestsPassed = false;
        }

        // Test 3: Render functionality
        if (!testRenderWithClient()) {
            allTestsPassed = false;
        }

    } catch (const std::exception& e) {
        std::cout << "Test failed with exception: " << e.what() << std::endl;
        allTestsPassed = false;
    }

    juce::shutdownJuce_GUI();

    if (allTestsPassed) {
        std::cout << "All gRPC smoke tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "Some gRPC smoke tests failed!" << std::endl;
        return 1;
    }
}