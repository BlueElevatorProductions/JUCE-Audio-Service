#include <iostream>
#include <memory>
#include <string>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include "audio_engine.grpc.pb.h"
#include "JuceAudioService/AudioFileSource.h"
#include "JuceAudioService/OfflineRenderer.h"
#include "edl/EdlStore.h"
#include "edl/EdlCompiler.h"
#include "edl/EdlRenderer.h"
#include "util/EdlJson.h"

#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <openssl/sha.h>
#include <queue>
#include <atomic>
#include <condition_variable>
#include <set>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;
using grpc::StatusCode;

namespace fs = std::filesystem;

// Event broadcasting system
class EventBroadcaster {
public:
    using Subscriber = grpc::ServerWriter<audio_engine::EngineEvent>*;

    void subscribe(Subscriber writer) {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_.insert(writer);
    }

    void unsubscribe(Subscriber writer) {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_.erase(writer);
    }

    void broadcast(const audio_engine::EngineEvent& event) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto writer : subscribers_) {
            if (writer) {
                writer->Write(event);
            }
        }
    }

private:
    std::mutex mutex_;
    std::set<Subscriber> subscribers_;
};

class AudioEngineServiceImpl final : public audio_engine::AudioEngine::Service {
private:
    std::unique_ptr<juceaudioservice::AudioFileSource> currentAudioSource;
    std::unique_ptr<juceaudioservice::OfflineRenderer> renderer;

    // EDL components
    juceaudioservice::EdlStore edlStore_;
    juceaudioservice::EdlCompiler edlCompiler_;
    juceaudioservice::EdlRenderer edlRenderer_;

    // Event broadcasting
    EventBroadcaster eventBroadcaster_;
    std::atomic<bool> running_{true};

    std::string calculateSHA256(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[gRPC] Failed to open file for SHA256: " << filePath << std::endl;
            return "";
        }

        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256;
        SHA256_Init(&sha256);

        char buffer[8192];
        while (file.read(buffer, sizeof(buffer))) {
            SHA256_Update(&sha256, buffer, file.gcount());
        }
        if (file.gcount() > 0) {
            SHA256_Update(&sha256, buffer, file.gcount());
        }

        if (file.bad()) {
            std::cerr << "[gRPC] Error reading file for SHA256: " << filePath << std::endl;
            return "";
        }

        SHA256_Final(hash, &sha256);

        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }
        return ss.str();
    }

    // Helper method to load a file into currentAudioSource
    Status loadFileInternal(const std::string& inputPath, std::string& resolvedPath) {
        // Handle absolute vs relative paths using JUCE
        juce::File file(inputPath);
        if (!juce::File::isAbsolutePath(inputPath)) {
            file = juce::File::getCurrentWorkingDirectory().getChildFile(inputPath);
        }

        // Check if file exists
        if (!file.existsAsFile()) {
            return Status(StatusCode::NOT_FOUND,
                         "File not found: " + file.getFullPathName().toStdString());
        }

        // Validate audio format using JUCE AudioFormatManager
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (!reader) {
            return Status(StatusCode::INVALID_ARGUMENT,
                         "Unsupported or unreadable audio file: " + file.getFullPathName().toStdString());
        }

        // Create new audio source and load the file
        currentAudioSource = std::make_unique<juceaudioservice::AudioFileSource>();
        bool loaded = currentAudioSource->loadFile(file);

        if (!loaded) {
            currentAudioSource.reset();
            return Status(StatusCode::INTERNAL,
                         "Failed to load audio file: " + file.getFullPathName().toStdString());
        }

        // Store resolved path for caller
        resolvedPath = file.getFullPathName().toStdString();
        return Status::OK;
    }

public:
    AudioEngineServiceImpl() : renderer(std::make_unique<juceaudioservice::OfflineRenderer>()) {
        std::cout << "[gRPC] AudioEngine service initialized" << std::endl;
    }

    Status LoadFile(ServerContext* context, const audio_engine::LoadFileRequest* request,
                   audio_engine::LoadFileResponse* response) override {

        std::cout << "[gRPC] LoadFile request for: " << request->file_path() << std::endl;

        const std::string& inputPath = request->file_path();
        std::string resolvedPath;

        // Use helper method to load the file
        Status loadStatus = loadFileInternal(inputPath, resolvedPath);
        if (!loadStatus.ok()) {
            return loadStatus;
        }

        // Success - populate response with file info
        response->set_success(true);
        response->set_message("File loaded successfully");

        auto* fileInfo = response->mutable_file_info();
        fileInfo->set_path(resolvedPath);
        fileInfo->set_sample_rate(static_cast<int32_t>(currentAudioSource->getSampleRate()));
        fileInfo->set_num_channels(currentAudioSource->getNumChannels());

        double sampleRate = currentAudioSource->getSampleRate();
        if (sampleRate > 0) {
            fileInfo->set_duration_seconds(static_cast<double>(currentAudioSource->getTotalLength()) / sampleRate);
        }

        // Get file size from resolved path
        juce::File file(resolvedPath);
        fileInfo->set_file_size_bytes(file.getSize());

        std::cout << "[gRPC] LoadFile successful: " << resolvedPath << " ("
                  << fileInfo->duration_seconds() << "s, "
                  << fileInfo->sample_rate() << "Hz, "
                  << fileInfo->num_channels() << " channels)" << std::endl;

        return Status::OK;
    }

    Status Render(ServerContext* context, const audio_engine::RenderRequest* request,
                 ServerWriter<audio_engine::RenderResponse>* writer) override {

        std::cout << "[gRPC] Render request: " << request->input_file()
                  << " -> " << request->output_file() << std::endl;

        // Lazy-load if nothing is loaded yet
        if (!currentAudioSource || !currentAudioSource->isLoaded()) {
            const std::string& inputFile = request->input_file();
            if (inputFile.empty()) {
            audio_engine::RenderResponse response;
            auto* error = response.mutable_error();
            error->set_error_code("NO_FILE_LOADED");
            error->set_error_message("No audio file is currently loaded and no input file provided.");
            writer->Write(response);
                std::cout << "[gRPC] Render failed: no file loaded and no input file provided" << std::endl;
                return Status::OK;
            }

            // Attempt to lazy-load the input file
            std::string resolvedPath;
            Status loadStatus = loadFileInternal(inputFile, resolvedPath);
            if (!loadStatus.ok()) {
                audio_engine::RenderResponse response;
                auto* error = response.mutable_error();
                error->set_error_code("LAZY_LOAD_FAILED");
                error->set_error_message("Failed to lazy-load input file: " + loadStatus.error_message());
                writer->Write(response);
                std::cout << "[gRPC] Render failed: lazy-load failed - " << loadStatus.error_message() << std::endl;
                return Status::OK;
            }

            std::cout << "[gRPC] Lazy-loaded input for render: " << resolvedPath << std::endl;
        }

        auto startTime = std::chrono::steady_clock::now();

        try {
            // Get render parameters
            double sampleRate = currentAudioSource->getSampleRate();
            int numChannels = currentAudioSource->getNumChannels();

            // Calculate start and end samples
            juce::int64 startSample = 0;
            juce::int64 totalSamples = currentAudioSource->getTotalLength();

            if (request->has_start_time()) {
                startSample = static_cast<juce::int64>(request->start_time() * sampleRate);
            }

            if (request->has_duration()) {
                juce::int64 durationSamples = static_cast<juce::int64>(request->duration() * sampleRate);
                totalSamples = std::min(startSample + durationSamples, totalSamples);
            }

            juce::int64 numSamplesToRender = totalSamples - startSample;

            if (numSamplesToRender <= 0) {
                audio_engine::RenderResponse response;
                auto* error = response.mutable_error();
                error->set_error_code("INVALID_RANGE");
                error->set_error_message("Invalid time range specified");
                writer->Write(response);
                std::cout << "[gRPC] Render failed: invalid time range" << std::endl;
                return Status::OK;
            }

            // Send initial progress
            audio_engine::RenderResponse progressResponse;
            auto* progress = progressResponse.mutable_progress();
            progress->set_progress_percentage(0.0);
            progress->set_status_message("Starting render...");
            writer->Write(progressResponse);

            // Set position and render
            currentAudioSource->setPosition(startSample);

            constexpr int blockSize = 44100; // 1 second at 44.1kHz
            juce::AudioBuffer<float> outputBuffer;

            // Render in chunks for progress updates
            juce::int64 samplesRendered = 0;

            std::vector<float> allSamples;
            allSamples.reserve(static_cast<size_t>(numSamplesToRender * numChannels));

            while (samplesRendered < numSamplesToRender && !context->IsCancelled()) {
                juce::int64 samplesThisBlock = std::min(static_cast<juce::int64>(blockSize),
                                                       numSamplesToRender - samplesRendered);

                auto blockBuffer = renderer->renderWindow(
                    *currentAudioSource,
                    startSample + samplesRendered,
                    static_cast<int>(samplesThisBlock),
                    sampleRate,
                    sampleRate,
                    numChannels
                );

                // Append samples to output vector
                for (int channel = 0; channel < numChannels; ++channel) {
                    const float* channelData = blockBuffer.getReadPointer(channel);
                    for (int sample = 0; sample < samplesThisBlock; ++sample) {
                        allSamples.push_back(channelData[sample]);
                    }
                }

                samplesRendered += samplesThisBlock;

                // Send progress update
                double progressPercent = (static_cast<double>(samplesRendered) / numSamplesToRender) * 100.0;
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count() / 1000.0;

                audio_engine::RenderResponse progressResp;
                auto* prog = progressResp.mutable_progress();
                prog->set_progress_percentage(progressPercent);
                prog->set_status_message("Rendering... " + std::to_string(static_cast<int>(progressPercent)) + "%");
                prog->set_elapsed_seconds(elapsed);

                if (progressPercent > 0) {
                    double estimatedTotal = elapsed * (100.0 / progressPercent);
                    prog->set_estimated_remaining_seconds(estimatedTotal - elapsed);
                }

                writer->Write(progressResp);

                // Small delay to prevent flooding
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            if (context->IsCancelled()) {
                std::cout << "[gRPC] Render cancelled by client" << std::endl;
                return Status::CANCELLED;
            }

            // Write output file (simplified WAV writer)
            std::ofstream outFile(request->output_file(), std::ios::binary);
            if (!outFile) {
                audio_engine::RenderResponse response;
                auto* error = response.mutable_error();
                error->set_error_code("FILE_WRITE_ERROR");
                error->set_error_message("Cannot create output file: " + request->output_file());
                writer->Write(response);
                std::cout << "[gRPC] Render failed: cannot create output file" << std::endl;
                return Status::OK;
            }

            // Simple WAV header
            uint32_t sampleRateInt = static_cast<uint32_t>(sampleRate);
            uint32_t dataSize = static_cast<uint32_t>(allSamples.size() * sizeof(float));
            uint32_t fileSize = 36 + dataSize;

            outFile.write("RIFF", 4);
            outFile.write(reinterpret_cast<const char*>(&fileSize), 4);
            outFile.write("WAVE", 4);
            outFile.write("fmt ", 4);

            uint32_t fmtSize = 16;
            uint16_t audioFormat = 3; // IEEE float
            uint16_t numChannelsShort = static_cast<uint16_t>(numChannels);
            uint32_t byteRate = sampleRateInt * numChannels * sizeof(float);
            uint16_t blockAlign = static_cast<uint16_t>(numChannels * sizeof(float));
            uint16_t bitsPerSample = 32;

            outFile.write(reinterpret_cast<const char*>(&fmtSize), 4);
            outFile.write(reinterpret_cast<const char*>(&audioFormat), 2);
            outFile.write(reinterpret_cast<const char*>(&numChannelsShort), 2);
            outFile.write(reinterpret_cast<const char*>(&sampleRateInt), 4);
            outFile.write(reinterpret_cast<const char*>(&byteRate), 4);
            outFile.write(reinterpret_cast<const char*>(&blockAlign), 2);
            outFile.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

            outFile.write("data", 4);
            outFile.write(reinterpret_cast<const char*>(&dataSize), 4);
            outFile.write(reinterpret_cast<const char*>(allSamples.data()), dataSize);

            outFile.close();

            // Calculate final duration and file size
            auto endTime = std::chrono::steady_clock::now();
            auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count() / 1000.0;

            // Calculate SHA256
            std::string sha256Hash = calculateSHA256(request->output_file());

            // Send completion response
            audio_engine::RenderResponse completeResponse;
            auto* complete = completeResponse.mutable_complete();
            complete->set_output_file_path(request->output_file());
            complete->set_sha256_hash(sha256Hash);
            complete->set_total_duration_seconds(totalDuration);

            try {
                complete->set_output_file_size_bytes(static_cast<int64_t>(fs::file_size(request->output_file())));
            } catch (...) {
                complete->set_output_file_size_bytes(0);
            }

            writer->Write(completeResponse);

            std::cout << "[gRPC] Render completed successfully: " << request->output_file()
                      << " (" << totalDuration << "s, SHA256: " << sha256Hash.substr(0, 16) << "...)" << std::endl;

        } catch (const std::exception& e) {
            audio_engine::RenderResponse response;
            auto* error = response.mutable_error();
            error->set_error_code("RENDER_ERROR");
            error->set_error_message("Render failed: " + std::string(e.what()));
            writer->Write(response);
            std::cout << "[gRPC] Render failed with exception: " << e.what() << std::endl;
        }

        return Status::OK;
    }

    Status UpdateEdl(ServerContext* context, const audio_engine::UpdateEdlRequest* request,
                    audio_engine::UpdateEdlResponse* response) override {

        std::cout << "[gRPC] UpdateEdl request for EDL: " << request->edl().id() << std::endl;

        const auto& edl = request->edl();

        // Validate and store EDL
        juceaudioservice::EdlStore::Snapshot snapshot;
        std::string error;

        std::cout << "[EDL][Validate] Starting validation for EDL: " << edl.id() << std::endl;

        bool success = edlStore_.replace(edl, snapshot, error);

        if (!success) {
            std::cout << "[EDL][Validate] Failed for EDL " << edl.id() << ": " << error << std::endl;

            // Broadcast error event
            audio_engine::EngineEvent errorEvent;
            auto* edlError = errorEvent.mutable_edl_error();
            edlError->set_edl_id(edl.id());
            edlError->set_reason(error);
            eventBroadcaster_.broadcast(errorEvent);

            return Status(StatusCode::INVALID_ARGUMENT, error);
        }

        std::cout << "[EDL][Apply] Successfully applied EDL: " << snapshot.edl.id()
                  << " revision: " << snapshot.revision
                  << " tracks: " << snapshot.track_count
                  << " clips: " << snapshot.clip_count << std::endl;

        // Populate response
        response->set_edl_id(snapshot.edl.id());
        response->set_revision(snapshot.revision);
        response->set_track_count(snapshot.track_count);
        response->set_clip_count(snapshot.clip_count);

        // Broadcast success event
        audio_engine::EngineEvent appliedEvent;
        auto* edlApplied = appliedEvent.mutable_edl_applied();
        edlApplied->set_edl_id(snapshot.edl.id());
        edlApplied->set_revision(snapshot.revision);
        edlApplied->set_track_count(snapshot.track_count);
        edlApplied->set_clip_count(snapshot.clip_count);
        eventBroadcaster_.broadcast(appliedEvent);

        return Status::OK;
    }

    Status RenderEdlWindow(ServerContext* context, const audio_engine::RenderEdlWindowRequest* request,
                           ServerWriter<audio_engine::EngineEvent>* writer) override {

        std::cout << "[gRPC] RenderEdlWindow request for EDL: " << request->edl_id()
                  << " range: " << request->range().start_samples() << "-"
                  << (request->range().start_samples() + request->range().duration_samples()) << std::endl;

        // Get current EDL snapshot
        auto edlSnapshot = edlStore_.get();
        if (!edlSnapshot) {
            audio_engine::EngineEvent errorEvent;
            auto* edlError = errorEvent.mutable_edl_error();
            edlError->set_edl_id(request->edl_id());
            edlError->set_reason("No EDL currently loaded");
            writer->Write(errorEvent);
            return Status(StatusCode::NOT_FOUND, "No EDL currently loaded");
        }

        if (edlSnapshot->edl.id() != request->edl_id()) {
            audio_engine::EngineEvent errorEvent;
            auto* edlError = errorEvent.mutable_edl_error();
            edlError->set_edl_id(request->edl_id());
            edlError->set_reason("EDL ID mismatch: requested '" + request->edl_id() +
                               "' but current is '" + edlSnapshot->edl.id() + "'");
            writer->Write(errorEvent);
            return Status(StatusCode::NOT_FOUND, "EDL ID mismatch");
        }

        // Compile EDL
        juceaudioservice::EdlCompiler::CompiledEdl compiledEdl;
        std::string error;

        std::cout << "[EDL][Compile] Starting compilation for render..." << std::endl;

        if (!edlCompiler_.compile(*edlSnapshot, compiledEdl, error)) {
            std::cout << "[EDL][Compile] Failed: " << error << std::endl;

            audio_engine::EngineEvent errorEvent;
            auto* edlError = errorEvent.mutable_edl_error();
            edlError->set_edl_id(request->edl_id());
            edlError->set_reason("Compilation failed: " + error);
            writer->Write(errorEvent);
            return Status(StatusCode::INTERNAL, "EDL compilation failed: " + error);
        }

        // Determine bit depth
        juceaudioservice::EdlRenderer::BitDepth bitDepth = juceaudioservice::EdlRenderer::BitDepth::Float32;
        switch (request->bit_depth()) {
            case 16: bitDepth = juceaudioservice::EdlRenderer::BitDepth::Int16; break;
            case 24: bitDepth = juceaudioservice::EdlRenderer::BitDepth::Int24; break;
            case 32: bitDepth = juceaudioservice::EdlRenderer::BitDepth::Float32; break;
            default:
                std::cout << "[EDL][Render] Invalid bit depth " << request->bit_depth() << ", using 32-bit float" << std::endl;
                break;
        }

        // Setup progress callback
        auto progressCallback = [writer, this](double fraction) {
            audio_engine::EngineEvent progressEvent;
            auto* progress = progressEvent.mutable_progress();
            progress->set_fraction(fraction);

            // Calculate ETA
            static auto startTime = std::chrono::steady_clock::now();
            if (fraction > 0.01) { // Only calculate ETA after 1%
                auto elapsed = std::chrono::steady_clock::now() - startTime;
                auto totalTime = elapsed / fraction;
                auto remaining = totalTime - elapsed;
                auto remainingSeconds = std::chrono::duration<double>(remaining).count();

                std::ostringstream ss;
                ss << std::fixed << std::setprecision(1) << remainingSeconds << "s";
                progress->set_eta(ss.str());
            }

            writer->Write(progressEvent);
        };

        // Render to WAV file
        std::cout << "[EDL][Render] Starting render to: " << request->out_path() << std::endl;

        bool renderSuccess = edlRenderer_.renderToWav(compiledEdl, request->range(),
                                                     request->out_path(), bitDepth,
                                                     progressCallback, error);

        if (!renderSuccess) {
            std::cout << "[EDL][Render] Failed: " << error << std::endl;
            return Status(StatusCode::INTERNAL, "Render failed: " + error);
        }

        // Calculate output file info
        juce::File outputFile(request->out_path());
        double durationSeconds = static_cast<double>(request->range().duration_samples()) / compiledEdl.sample_rate;
        std::string sha256Hash = calculateSHA256(request->out_path());

        // Send completion event
        audio_engine::EngineEvent completeEvent;
        auto* complete = completeEvent.mutable_complete();
        complete->set_out_path(request->out_path());
        complete->set_duration_sec(durationSeconds);
        complete->set_sha256(sha256Hash);
        writer->Write(completeEvent);

        std::cout << "[EDL][Render] Completed successfully: " << request->out_path()
                  << " (" << durationSeconds << "s, SHA256: " << sha256Hash.substr(0, 16) << "...)" << std::endl;

        return Status::OK;
    }

    Status Subscribe(ServerContext* context, const audio_engine::SubscribeRequest* request,
                    ServerWriter<audio_engine::EngineEvent>* writer) override {

        std::cout << "[gRPC] Subscribe request for session: " << request->session() << std::endl;

        // Register subscriber
        eventBroadcaster_.subscribe(writer);

        // Send initial backend status
        audio_engine::EngineEvent statusEvent;
        auto* backend = statusEvent.mutable_backend();
        backend->set_status("ready");
        writer->Write(statusEvent);

        // Send current EDL status if available
        auto edlSnapshot = edlStore_.get();
        if (edlSnapshot) {
            audio_engine::EngineEvent edlEvent;
            auto* edlApplied = edlEvent.mutable_edl_applied();
            edlApplied->set_edl_id(edlSnapshot->edl.id());
            edlApplied->set_revision(edlSnapshot->revision);
            edlApplied->set_track_count(edlSnapshot->track_count);
            edlApplied->set_clip_count(edlSnapshot->clip_count);
            writer->Write(edlEvent);
        }

        std::cout << "[gRPC][Event] Subscriber registered for session: " << request->session() << std::endl;

        // Keep connection alive with periodic heartbeats
        auto lastHeartbeat = std::chrono::steady_clock::now();
        const auto heartbeatInterval = std::chrono::seconds(2);

        while (!context->IsCancelled() && running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            auto now = std::chrono::steady_clock::now();
            if (now - lastHeartbeat >= heartbeatInterval) {
                audio_engine::EngineEvent heartbeatEvent;
                auto* heartbeat = heartbeatEvent.mutable_heartbeat();
                auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()).count();
                heartbeat->set_monotonic_ms(timestamp);

                if (!writer->Write(heartbeatEvent)) {
                    break; // Client disconnected
                }

                lastHeartbeat = now;
            }
        }

        // Unregister subscriber
        eventBroadcaster_.unsubscribe(writer);
        std::cout << "[gRPC][Event] Subscriber disconnected for session: " << request->session() << std::endl;

        return Status::OK;
    }
};

void RunServer(int port = 50051) {
    std::string server_address = "0.0.0.0:" + std::to_string(port);
    AudioEngineServiceImpl service;

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    if (!server) {
        std::cerr << "[gRPC] Failed to start server on " << server_address << std::endl;
        return;
    }

    std::cout << "[gRPC] Server is listening on " << server_address << std::endl;
    std::cout << "[gRPC] Listening" << std::endl;  // Required by smoke test script

    server->Wait();
}

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --port <port>       Server port (default: 50051)" << std::endl;
    std::cout << "  --help, -h          Show this help message" << std::endl;
    std::cout << std::endl;
}

int main(int argc, char** argv) {
    int port = 50051;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            try {
                port = std::stoi(argv[++i]);
                if (port <= 0 || port > 65535) {
                    std::cerr << "Error: invalid port number: " << port << std::endl;
                    return 1;
                }
            } catch (...) {
                std::cerr << "Error: invalid port argument: " << argv[i] << std::endl;
                return 1;
            }
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Error: unknown argument: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    // Initialize JUCE

    try {
        RunServer(port);
    } catch (const std::exception& e) {
        std::cerr << "[gRPC] Server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}