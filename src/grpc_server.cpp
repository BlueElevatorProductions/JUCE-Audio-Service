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

#include <openssl/sha.h>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;
using grpc::StatusCode;

namespace fs = std::filesystem;

class AudioEngineServiceImpl final : public audio_engine::AudioEngine::Service {
private:
    std::unique_ptr<juceaudioservice::AudioFileSource> currentAudioSource;
    std::unique_ptr<juceaudioservice::OfflineRenderer> renderer;

    std::string calculateSHA256(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::binary);
        if (!file) return "";

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

        SHA256_Final(hash, &sha256);

        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }
        return ss.str();
    }

public:
    AudioEngineServiceImpl() : renderer(std::make_unique<juceaudioservice::OfflineRenderer>()) {
        std::cout << "[gRPC] AudioEngine service initialized" << std::endl;
    }

    Status LoadFile(ServerContext* context, const audio_engine::LoadFileRequest* request,
                   audio_engine::LoadFileResponse* response) override {

        std::cout << "[gRPC] LoadFile request for: " << request->file_path() << std::endl;

        const std::string& filePath = request->file_path();

        // Check if file exists
        if (!fs::exists(filePath)) {
            response->set_success(false);
            response->set_message("File does not exist: " + filePath);
            std::cout << "[gRPC] LoadFile failed: file does not exist" << std::endl;
            return Status::OK;
        }

        // Create new audio source
        currentAudioSource = std::make_unique<juceaudioservice::AudioFileSource>();

        juce::File file(filePath);
        bool loaded = currentAudioSource->loadFile(file);

        if (!loaded) {
            response->set_success(false);
            response->set_message("Failed to load audio file: " + filePath);
            currentAudioSource.reset();
            std::cout << "[gRPC] LoadFile failed: could not load audio file" << std::endl;
            return Status::OK;
        }

        // Success - populate response with file info
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

        std::cout << "[gRPC] LoadFile successful: " << filePath << " ("
                  << fileInfo->duration_seconds() << "s, "
                  << fileInfo->sample_rate() << "Hz, "
                  << fileInfo->num_channels() << " channels)" << std::endl;

        return Status::OK;
    }

    Status Render(ServerContext* context, const audio_engine::RenderRequest* request,
                 ServerWriter<audio_engine::RenderResponse>* writer) override {

        std::cout << "[gRPC] Render request: " << request->input_file()
                  << " -> " << request->output_file() << std::endl;

        if (!currentAudioSource || !currentAudioSource->isLoaded()) {
            audio_engine::RenderResponse response;
            auto* error = response.mutable_error();
            error->set_error_code("NO_FILE_LOADED");
            error->set_error_message("No audio file is currently loaded. Call LoadFile first.");
            writer->Write(response);
            std::cout << "[gRPC] Render failed: no file loaded" << std::endl;
            return Status::OK;
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
        std::cout << "[gRPC] UpdateEdl called (UNIMPLEMENTED)" << std::endl;
        return Status(StatusCode::UNIMPLEMENTED, "UpdateEdl is not implemented");
    }

    Status Subscribe(ServerContext* context, const audio_engine::SubscribeRequest* request,
                    ServerWriter<audio_engine::SubscribeResponse>* writer) override {
        std::cout << "[gRPC] Subscribe called (UNIMPLEMENTED)" << std::endl;
        return Status(StatusCode::UNIMPLEMENTED, "Subscribe is not implemented");
    }
};

void RunServer() {
    std::string server_address("0.0.0.0:50051");
    AudioEngineServiceImpl service;

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "[gRPC] Server listening on " << server_address << std::endl;

    server->Wait();
}

int main(int argc, char** argv) {
    // Initialize JUCE
    juce::initialiseJuce_GUI();

    try {
        RunServer();
    } catch (const std::exception& e) {
        std::cerr << "[gRPC] Server error: " << e.what() << std::endl;
        return 1;
    }

    juce::shutdownJuce_GUI();
    return 0;
}