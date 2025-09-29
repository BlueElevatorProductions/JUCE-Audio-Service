#include <iostream>
#include <memory>
#include <string>
#include <filesystem>
#include <vector>
#include <chrono>
#include <iomanip>
#include <algorithm>

#include <grpcpp/grpcpp.h>
#include "audio_engine.grpc.pb.h"
#include "util/EdlJson.h"

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

    bool UpdateEdl(const std::string& edlPath, bool replace = false) {
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

        std::cout << "EDL updated successfully:" << std::endl;
        std::cout << "  EDL ID: " << response.edl_id() << std::endl;
        std::cout << "  Revision: " << response.revision() << std::endl;
        std::cout << "  Track Count: " << response.track_count() << std::endl;
        std::cout << "  Clip Count: " << response.clip_count() << std::endl;

        return true;
    }

    bool RenderEdlWindow(const std::string& edlId, double startSec, double durSec,
                        const std::string& outputPath, int bitDepth = 16) {
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
        std::string finalChecksum;

        while (reader->Read(&event)) {
            if (event.has_progress()) {
                const auto& progress = event.progress();
                std::cout << "\rProgress: " << std::fixed << std::setprecision(1)
                         << (progress.fraction() * 100.0) << "%";
                if (!progress.eta().empty()) {
                    std::cout << " (ETA: " << progress.eta() << ")";
                }
                std::cout.flush();
            } else if (event.has_complete()) {
                const auto& complete = event.complete();
                std::cout << std::endl << "Render completed!" << std::endl;
                std::cout << "  Output file: " << complete.out_path() << std::endl;
                std::cout << "  Duration: " << complete.duration_sec() << " seconds" << std::endl;
                std::cout << "  SHA256: " << complete.sha256() << std::endl;
                finalChecksum = complete.sha256();
                success = true;
            } else if (event.has_edl_error()) {
                const auto& error = event.edl_error();
                std::cout << std::endl << "EDL Error: " << error.reason() << std::endl;
                return false;
            }
        }

        Status status = reader->Finish();
        if (!status.ok()) {
            std::cout << std::endl << "RenderEdlWindow RPC failed: " << status.error_message() << std::endl;
            return false;
        }

        return success;
    }

    bool Subscribe(const std::string& edlId) {
        audio_engine::SubscribeRequest request;
        request.set_session(edlId);

        ClientContext context;
        std::unique_ptr<ClientReader<audio_engine::EngineEvent>> reader(
            stub_->Subscribe(&context, request));

        audio_engine::EngineEvent event;
        std::string eventJson, error;

        std::cout << "Subscribing to events for EDL: " << edlId << std::endl;
        std::cout << "Press Ctrl+C to exit..." << std::endl;

        while (reader->Read(&event)) {
            // Convert event to NDJSON (one JSON object per line)
            if (juceaudioservice::EdlJson::eventToJson(event, eventJson, error)) {
                std::cout << eventJson << std::endl;
            } else {
                std::cerr << "Failed to convert event to JSON: " << error << std::endl;
            }
        }

        Status status = reader->Finish();
        if (!status.ok()) {
            std::cout << "Subscribe RPC ended: " << status.error_message() << std::endl;
            return false;
        }

        return true;
    }
};

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options] <command> [args...]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --addr <address>    Server address (default: localhost:50051)" << std::endl;
    std::cout << "  --server <address>  Server address (alias for --addr)" << std::endl;
    std::cout << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  ping                                         Test server connectivity" << std::endl;
    std::cout << "  load --path <file>                          Load an audio file" << std::endl;
    std::cout << "  render --path <input> --out <output>        Render full file" << std::endl;
    std::cout << "  render --path <input> --out <output> --start <time> --dur <duration>  Render window" << std::endl;
    std::cout << std::endl;
    std::cout << "EDL Commands:" << std::endl;
    std::cout << "  edl-update --edl <path.json> [--replace]    Update EDL from JSON file" << std::endl;
    std::cout << "  edl-render --edl-id <id> --start <sec> --dur <sec> --out <path> [--bit-depth 16|24|32]  Render EDL window" << std::endl;
    std::cout << "  subscribe --edl-id <id>                     Subscribe to EDL events (NDJSON)" << std::endl;
    std::cout << std::endl;
    std::cout << "Legacy format (still supported):" << std::endl;
    std::cout << "  load <file>" << std::endl;
    std::cout << "  render <input> <output> [<start>] [<duration>]" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << programName << " --addr 127.0.0.1:50051 ping" << std::endl;
    std::cout << "  " << programName << " load --path input.wav" << std::endl;
    std::cout << "  " << programName << " render --path input.wav --out output.wav" << std::endl;
    std::cout << "  " << programName << " render --path input.wav --out output.wav --start 1.0 --dur 5.0" << std::endl;
    std::cout << "  " << programName << " edl-update --edl fixtures/test_edl.json" << std::endl;
    std::cout << "  " << programName << " edl-render --edl-id abc123 --start 0 --dur 5 --out output.wav --bit-depth 24" << std::endl;
    std::cout << "  " << programName << " subscribe --edl-id abc123" << std::endl;
}

// Helper function to find named argument value
std::string getNamedArg(const std::vector<std::string>& args, const std::string& name, const std::string& defaultValue = "") {
    auto it = std::find(args.begin(), args.end(), name);
    if (it != args.end() && std::next(it) != args.end()) {
        return *std::next(it);
    }
    return defaultValue;
}

bool hasNamedArg(const std::vector<std::string>& args, const std::string& name) {
    return std::find(args.begin(), args.end(), name) != args.end();
}

int main(int argc, char** argv) {
    std::string serverAddress = "localhost:50051";
    std::vector<std::string> args;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--server" || arg == "--addr") && i + 1 < argc) {
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
        std::string filePath;

        // Check for new format: load --path <file>
        if (hasNamedArg(args, "--path")) {
            filePath = getNamedArg(args, "--path");
            if (filePath.empty()) {
                std::cout << "Error: --path requires a file path argument" << std::endl;
                return 1;
            }
        }
        // Legacy format: load <file>
        else if (args.size() == 2) {
            filePath = args[1];
        }
        else {
            std::cout << "Error: load command requires --path <file> or <file> argument" << std::endl;
            return 1;
        }

        if (!std::filesystem::exists(filePath)) {
            std::cout << "Error: file does not exist: " << filePath << std::endl;
            return 1;
        }

        if (!client.LoadFile(filePath)) {
            return 1;
        }
    } else if (command == "render") {
        std::string inputFile, outputFile;
        double startTime = -1;
        double duration = -1;

        // Check for new format: render --path <input> --out <output> [--start <time>] [--dur <duration>]
        if (hasNamedArg(args, "--path") && hasNamedArg(args, "--out")) {
            inputFile = getNamedArg(args, "--path");
            outputFile = getNamedArg(args, "--out");

            if (inputFile.empty()) {
                std::cout << "Error: --path requires a file path argument" << std::endl;
                return 1;
            }
            if (outputFile.empty()) {
                std::cout << "Error: --out requires a file path argument" << std::endl;
                return 1;
            }

            std::string startStr = getNamedArg(args, "--start");
            if (!startStr.empty()) {
                try {
                    startTime = std::stod(startStr);
                } catch (...) {
                    std::cout << "Error: invalid start time: " << startStr << std::endl;
                    return 1;
                }
            }

            std::string durStr = getNamedArg(args, "--dur");
            if (!durStr.empty()) {
                try {
                    duration = std::stod(durStr);
                } catch (...) {
                    std::cout << "Error: invalid duration: " << durStr << std::endl;
                    return 1;
                }
            }
        }
        // Legacy format: render <input> <output> [<start>] [<duration>]
        else if (args.size() >= 3 && args.size() <= 5) {
            inputFile = args[1];
            outputFile = args[2];

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
        }
        else {
            std::cout << "Error: render command requires --path <input> --out <output> [--start <time>] [--dur <duration>]" << std::endl;
            std::cout << "       or legacy format: <input> <output> [<start>] [<duration>]" << std::endl;
            return 1;
        }

        if (!client.Render(inputFile, outputFile, startTime, duration)) {
            return 1;
        }
    } else if (command == "edl-update") {
        std::string edlPath = getNamedArg(args, "--edl");
        bool replace = hasNamedArg(args, "--replace");

        if (edlPath.empty()) {
            std::cout << "Error: edl-update command requires --edl <path.json>" << std::endl;
            return 1;
        }

        if (!std::filesystem::exists(edlPath)) {
            std::cout << "Error: EDL file does not exist: " << edlPath << std::endl;
            return 1;
        }

        if (!client.UpdateEdl(edlPath, replace)) {
            return 1;
        }
    } else if (command == "edl-render") {
        std::string edlId = getNamedArg(args, "--edl-id");
        std::string startStr = getNamedArg(args, "--start");
        std::string durStr = getNamedArg(args, "--dur");
        std::string outputPath = getNamedArg(args, "--out");
        std::string bitDepthStr = getNamedArg(args, "--bit-depth", "16");

        if (edlId.empty() || startStr.empty() || durStr.empty() || outputPath.empty()) {
            std::cout << "Error: edl-render command requires --edl-id <id> --start <sec> --dur <sec> --out <path>" << std::endl;
            return 1;
        }

        double startSec, durSec;
        int bitDepth;

        try {
            startSec = std::stod(startStr);
            durSec = std::stod(durStr);
            bitDepth = std::stoi(bitDepthStr);
        } catch (...) {
            std::cout << "Error: invalid numeric parameter" << std::endl;
            return 1;
        }

        if (bitDepth != 16 && bitDepth != 24 && bitDepth != 32) {
            std::cout << "Error: bit depth must be 16, 24, or 32" << std::endl;
            return 1;
        }

        if (!client.RenderEdlWindow(edlId, startSec, durSec, outputPath, bitDepth)) {
            return 1;
        }
    } else if (command == "subscribe") {
        std::string edlId = getNamedArg(args, "--edl-id");

        if (edlId.empty()) {
            std::cout << "Error: subscribe command requires --edl-id <id>" << std::endl;
            return 1;
        }

        if (!client.Subscribe(edlId)) {
            return 1;
        }
    } else {
        std::cout << "Error: unknown command: " << command << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}