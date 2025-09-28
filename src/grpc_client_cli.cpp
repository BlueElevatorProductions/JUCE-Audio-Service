#include <iostream>
#include <memory>
#include <string>
#include <filesystem>
#include <vector>
#include <chrono>
#include <iomanip>

#include <grpcpp/grpcpp.h>
#include "audio_engine.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::Status;

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

        if (response.success()) {
            std::cout << "File loaded successfully: " << response.message() << std::endl;
            if (response.has_file_info()) {
                const auto& info = response.file_info();
                std::cout << "  Duration: " << info.duration_seconds() << " seconds" << std::endl;
                std::cout << "  Sample Rate: " << info.sample_rate() << " Hz" << std::endl;
                std::cout << "  Channels: " << info.num_channels() << std::endl;
                std::cout << "  File Size: " << info.file_size_bytes() << " bytes" << std::endl;
            }
            return true;
        } else {
            std::cout << "Failed to load file: " << response.message() << std::endl;
            return false;
        }
    }

    bool Render(const std::string& inputFile, const std::string& outputFile,
               double startTime = -1, double duration = -1) {
        audio_engine::RenderRequest request;
        request.set_input_file(inputFile);
        request.set_output_file(outputFile);

        if (startTime >= 0) {
            request.set_start_time(startTime);
        }
        if (duration >= 0) {
            request.set_duration(duration);
        }

        ClientContext context;
        std::unique_ptr<ClientReader<audio_engine::RenderResponse>> reader(
            stub_->Render(&context, request));

        audio_engine::RenderResponse response;
        bool success = false;

        while (reader->Read(&response)) {
            if (response.has_progress()) {
                const auto& progress = response.progress();
                std::cout << "\rProgress: " << std::fixed << std::setprecision(1)
                         << progress.progress_percentage() << "% - " << progress.status_message();
                if (progress.has_estimated_remaining_seconds()) {
                    std::cout << " (ETA: " << std::fixed << std::setprecision(1)
                             << progress.estimated_remaining_seconds() << "s)";
                }
                std::cout.flush();
            } else if (response.has_complete()) {
                const auto& complete = response.complete();
                std::cout << std::endl << "Render completed!" << std::endl;
                std::cout << "  Output file: " << complete.output_file_path() << std::endl;
                std::cout << "  Duration: " << complete.total_duration_seconds() << " seconds" << std::endl;
                std::cout << "  File size: " << complete.output_file_size_bytes() << " bytes" << std::endl;
                std::cout << "  SHA256: " << complete.sha256_hash() << std::endl;
                success = true;
            } else if (response.has_error()) {
                const auto& error = response.error();
                std::cout << std::endl << "Render error [" << error.error_code() << "]: "
                         << error.error_message() << std::endl;
                return false;
            }
        }

        Status status = reader->Finish();
        if (!status.ok()) {
            std::cout << std::endl << "Render RPC failed: " << status.error_message() << std::endl;
            return false;
        }

        return success;
    }

    void Ping() {
        // Use LoadFile with empty path as a ping
        audio_engine::LoadFileRequest request;
        request.set_file_path("");

        audio_engine::LoadFileResponse response;
        ClientContext context;

        auto start = std::chrono::steady_clock::now();
        Status status = stub_->LoadFile(&context, request, &response);
        auto end = std::chrono::steady_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        if (status.ok()) {
            std::cout << "Server is responding (ping: " << duration.count() << "ms)" << std::endl;
        } else {
            std::cout << "Server ping failed: " << status.error_message() << std::endl;
        }
    }
};

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options] <command> [args...]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --server <address>  Server address (default: localhost:50051)" << std::endl;
    std::cout << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  ping                          Test server connectivity" << std::endl;
    std::cout << "  load <file>                   Load an audio file" << std::endl;
    std::cout << "  render <input> <output>       Render loaded file to output" << std::endl;
    std::cout << "  render <input> <output> <start> <duration>  Render with time window" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << programName << " ping" << std::endl;
    std::cout << "  " << programName << " load input.wav" << std::endl;
    std::cout << "  " << programName << " render input.wav output.wav" << std::endl;
    std::cout << "  " << programName << " render input.wav output.wav 1.0 5.0" << std::endl;
}

int main(int argc, char** argv) {
    std::string serverAddress = "localhost:50051";
    std::vector<std::string> args;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--server" && i + 1 < argc) {
            serverAddress = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            args.push_back(arg);
        }
    }

    if (args.empty()) {
        printUsage(argv[0]);
        return 1;
    }

    // Create client
    auto channel = grpc::CreateChannel(serverAddress, grpc::InsecureChannelCredentials());
    AudioEngineClient client(channel);

    const std::string& command = args[0];

    if (command == "ping") {
        client.Ping();
    } else if (command == "load") {
        if (args.size() != 2) {
            std::cout << "Error: load command requires exactly one argument (file path)" << std::endl;
            return 1;
        }

        if (!std::filesystem::exists(args[1])) {
            std::cout << "Error: file does not exist: " << args[1] << std::endl;
            return 1;
        }

        if (!client.LoadFile(args[1])) {
            return 1;
        }
    } else if (command == "render") {
        if (args.size() < 3 || args.size() > 5) {
            std::cout << "Error: render command requires 2-4 arguments: <input> <output> [<start>] [<duration>]" << std::endl;
            return 1;
        }

        std::string inputFile = args[1];
        std::string outputFile = args[2];
        double startTime = -1;
        double duration = -1;

        if (args.size() >= 4) {
            try {
                startTime = std::stod(args[3]);
            } catch (...) {
                std::cout << "Error: invalid start time: " << args[3] << std::endl;
                return 1;
            }
        }

        if (args.size() >= 5) {
            try {
                duration = std::stod(args[4]);
            } catch (...) {
                std::cout << "Error: invalid duration: " << args[4] << std::endl;
                return 1;
            }
        }

        if (!client.Render(inputFile, outputFile, startTime, duration)) {
            return 1;
        }
    } else {
        std::cout << "Error: unknown command: " << command << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}