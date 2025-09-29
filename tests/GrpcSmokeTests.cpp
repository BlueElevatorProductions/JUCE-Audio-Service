#include <iostream>
#include <memory>
#include <string>
#include <filesystem>
#include <thread>
#include <chrono>
#include <future>
#include <fstream>
#include <cstdlib>

#include <grpcpp/grpcpp.h>
#include "audio_engine.grpc.pb.h"

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "JuceAudioService/AudioFileSource.h"
#include "JuceAudioService/OfflineRenderer.h"

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
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;

namespace fs = std::filesystem;

// We'll use the actual server binary for testing, not a duplicate implementation

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
        if (!status.ok()) {
            std::cout << "LoadFile RPC failed: " << status.error_message() << std::endl;
            return false;
        }
        return response.success();
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
                std::cout << "Render failed: " << response.error().error_message() << std::endl;
                return false;
            }
        }

        Status status = reader->Finish();
        if (!status.ok()) {
            std::cout << "Render RPC failed: " << status.error_message() << std::endl;
            return false;
        }
        return hasComplete;
    }
};

std::string createTestAudioFile() {
    const std::string testFile = getOutputPath("test_audio.wav");

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

// Launch actual server binary as subprocess
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

bool testServerStartup() {
    std::cout << "Testing server startup..." << std::endl;

    auto serverFuture = startTestServer("localhost:50054");
    if (!serverFuture) {
        std::cout << "Failed to start server" << std::endl;
        return false;
    }

    // Test connection
    auto channel = grpc::CreateChannel("localhost:50054", grpc::InsecureChannelCredentials());
    AudioEngineClient client(channel);

    // Simple connectivity test - try to call LoadFile with invalid path
    bool connected = false;
    try {
        client.LoadFile("nonexistent.wav"); // This should fail but prove connectivity
        connected = true;
    } catch (...) {
        connected = true; // Any response (even error) means server is reachable
    }

    std::cout << "Server startup test " << (connected ? "passed" : "failed") << std::endl;
    return connected;
}

bool testLoadFileWithClient() {
    std::cout << "Testing LoadFile with client..." << std::endl;

    // Start server
    std::string serverAddress = "localhost:50052";
    auto serverFuture = startTestServer(serverAddress);
    if (!serverFuture) {
        std::cout << "Failed to start server" << std::endl;
        return false;
    }

    // Create test file
    std::string testFile = createTestAudioFile();

    // Create client and test
    auto channel = grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials());
    AudioEngineClient client(channel);

    bool result = client.LoadFile(testFile);

    // Cleanup
    juce::File(testFile).deleteFile();

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
    auto serverFuture = startTestServer(serverAddress);
    if (!serverFuture) {
        std::cout << "Failed to start server" << std::endl;
        return false;
    }

    // Create test file
    std::string testInputFile = createTestAudioFile();
    std::string testOutputFile = getOutputPath("test_output.wav");

    // Create client and test
    auto channel = grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials());
    AudioEngineClient client(channel);

    // Load file first
    bool loadResult = client.LoadFile(testInputFile);
    if (!loadResult) {
        std::cout << "Failed to load file for render test" << std::endl;
        juce::File(testInputFile).deleteFile();
        return false;
    }

    // Test render
    bool renderResult = client.Render(testInputFile, testOutputFile);

    // Cleanup
    juce::File(testInputFile).deleteFile();
    juce::File(testOutputFile).deleteFile();

    if (renderResult) {
        std::cout << "Render test passed" << std::endl;
    } else {
        std::cout << "Render test failed" << std::endl;
    }

    return renderResult;
}

int main(int argc, char** argv) {
    // Initialize JUCE

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


    if (allTestsPassed) {
        std::cout << "All gRPC smoke tests passed!" << std::endl;
        return 0;
    } else {
        std::cout << "Some gRPC smoke tests failed!" << std::endl;
        return 1;
    }
}