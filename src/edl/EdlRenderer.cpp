#include "EdlRenderer.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>

namespace juceaudioservice {

EdlRenderer::EdlRenderer() {
    formatManager_.registerBasicFormats();
}

bool EdlRenderer::renderToWav(const EdlCompiler::CompiledEdl& compiledEdl,
                              const audio_engine::TimeRange& range,
                              const std::string& outputPath,
                              BitDepth bitDepth,
                              ProgressCallback progressCallback,
                              std::string& error) {

    juce::AudioBuffer<float> outputBuffer;
    if (!renderToBuffer(compiledEdl, range, outputBuffer, progressCallback, error)) {
        return false;
    }

    return writeWavFile(outputBuffer, compiledEdl.sample_rate, outputPath, bitDepth, error);
}

bool EdlRenderer::renderToBuffer(const EdlCompiler::CompiledEdl& compiledEdl,
                                 const audio_engine::TimeRange& range,
                                 juce::AudioBuffer<float>& outputBuffer,
                                 ProgressCallback progressCallback,
                                 std::string& error) {

    std::cout << "[EDL][Render] Starting render: start=" << range.start_samples()
              << " duration=" << range.duration_samples() << " samples" << std::endl;

    return renderTimeRange(compiledEdl, range, outputBuffer, progressCallback, error);
}

bool EdlRenderer::renderTimeRange(const EdlCompiler::CompiledEdl& compiledEdl,
                                  const audio_engine::TimeRange& range,
                                  juce::AudioBuffer<float>& outputBuffer,
                                  ProgressCallback progressCallback,
                                  std::string& error) {

    int64_t rangeStart = range.start_samples();
    int64_t rangeEnd = rangeStart + range.duration_samples();
    int64_t totalSamples = range.duration_samples();

    if (totalSamples <= 0) {
        error = "Invalid render range: duration must be positive";
        return false;
    }

    // Determine max channels needed
    int maxChannels = 2; // Default stereo
    for (const auto& track : compiledEdl.tracks) {
        for (const auto& clip : track.clips) {
            if (clip.media && clip.media->channels() > maxChannels) {
                maxChannels = clip.media->channels();
            }
        }
    }

    // Initialize output buffer
    ensureBufferSize(outputBuffer, maxChannels, static_cast<int>(totalSamples));
    outputBuffer.clear();

    // Render in blocks for progress updates
    int64_t samplesRendered = 0;
    juce::AudioBuffer<float> mixBuffer;
    ensureBufferSize(mixBuffer, maxChannels, blockSize_);

    while (samplesRendered < totalSamples) {
        int64_t blockStart = rangeStart + samplesRendered;
        int64_t blockSamples = std::min(static_cast<int64_t>(blockSize_), totalSamples - samplesRendered);
        int64_t blockEnd = blockStart + blockSamples;

        // Clear mix buffer for this block
        ensureBufferSize(mixBuffer, maxChannels, static_cast<int>(blockSamples));
        mixBuffer.clear();

        // Render each track into mix buffer
        for (const auto& track : compiledEdl.tracks) {
            if (!track.muted) {
                renderTrack(track, blockStart, blockEnd, mixBuffer, 0);
            }
        }

        // Copy block to output buffer
        for (int ch = 0; ch < maxChannels; ++ch) {
            if (ch < mixBuffer.getNumChannels()) {
                outputBuffer.copyFrom(ch, static_cast<int>(samplesRendered),
                                    mixBuffer, ch, 0, static_cast<int>(blockSamples));
            }
        }

        samplesRendered += blockSamples;

        // Report progress
        if (progressCallback) {
            double fraction = static_cast<double>(samplesRendered) / totalSamples;
            progressCallback(fraction);
        }
    }

    std::cout << "[EDL][Render] Completed render: " << samplesRendered << " samples" << std::endl;
    return true;
}

void EdlRenderer::renderTrack(const EdlCompiler::CompiledTrack& track,
                             int64_t rangeStart, int64_t rangeEnd,
                             juce::AudioBuffer<float>& mixBuffer,
                             int64_t bufferOffset) {

    auto clipsInRange = getClipsInRange(track, rangeStart, rangeEnd);
    if (clipsInRange.empty()) {
        return;
    }

    juce::AudioBuffer<float> clipBuffer;
    int numChannels = mixBuffer.getNumChannels();
    int blockSamples = static_cast<int>(rangeEnd - rangeStart);

    for (const auto& clip : clipsInRange) {
        ensureBufferSize(clipBuffer, numChannels, blockSamples);
        clipBuffer.clear();

        renderClip(clip, rangeStart, rangeEnd, clipBuffer, bufferOffset);

        // Apply track gain
        if (track.gain_linear != 1.0f) {
            applyGain(clipBuffer, track.gain_linear);
        }

        // Add to mix
        addToMixBuffer(mixBuffer, clipBuffer);
    }
}

void EdlRenderer::renderClip(const EdlCompiler::CompiledClip& clip,
                            int64_t rangeStart, int64_t rangeEnd,
                            juce::AudioBuffer<float>& clipBuffer,
                            int64_t bufferOffset) {

    // Calculate intersection
    int64_t clipStart = std::max(clip.t0, rangeStart);
    int64_t clipEnd = std::min(clip.t1, rangeEnd);

    if (clipStart >= clipEnd) {
        return; // No intersection
    }

    // Get audio reader
    juce::AudioFormatReader* reader = getReader(clip.media->path());
    if (!reader) {
        std::cerr << "[EDL][Render] Failed to get reader for: " << clip.media->path() << std::endl;
        return;
    }

    // Calculate source positions
    int64_t sourceStart = clip.clip->start_in_media() + (clipStart - clip.t0);
    int64_t sourceSamples = clipEnd - clipStart;
    int bufferStart = static_cast<int>(clipStart - rangeStart + bufferOffset);

    // Read audio data
    if (sourceStart >= 0 && sourceStart < reader->lengthInSamples &&
        sourceSamples > 0 && bufferStart >= 0) {

        int readSamples = static_cast<int>(std::min(sourceSamples,
            static_cast<int64_t>(clipBuffer.getNumSamples() - bufferStart)));

        if (readSamples > 0) {
            reader->read(&clipBuffer, bufferStart, readSamples,
                        sourceStart, true, true);

            // Apply clip gain
            if (clip.gain_linear != 1.0f) {
                for (int ch = 0; ch < clipBuffer.getNumChannels(); ++ch) {
                    juce::FloatVectorOperations::multiply(
                        clipBuffer.getWritePointer(ch, bufferStart),
                        clip.gain_linear, readSamples);
                }
            }

            // Apply fades
            if (!clip.fade_in.isEmpty()) {
                applyFade(clipBuffer, clip.fade_in, clip.t0, clip.t1,
                         clipStart, clipEnd, true);
            }

            if (!clip.fade_out.isEmpty()) {
                applyFade(clipBuffer, clip.fade_out, clip.t0, clip.t1,
                         clipStart, clipEnd, false);
            }
        }
    }
}

void EdlRenderer::applyGain(juce::AudioBuffer<float>& buffer, float gainLinear) {
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        juce::FloatVectorOperations::multiply(buffer.getWritePointer(ch),
                                            gainLinear, buffer.getNumSamples());
    }
}

void EdlRenderer::applyFade(juce::AudioBuffer<float>& buffer, const EdlCompiler::FadeSpec& fade,
                           int64_t clipStart, int64_t clipEnd, int64_t renderStart, int64_t renderEnd,
                           bool isFadeIn) {

    int64_t fadeStart, fadeEnd;
    if (isFadeIn) {
        fadeStart = clipStart;
        fadeEnd = clipStart + fade.length_samples;
    } else {
        fadeStart = clipEnd - fade.length_samples;
        fadeEnd = clipEnd;
    }

    // Calculate intersection with render range
    int64_t effectiveStart = std::max(fadeStart, renderStart);
    int64_t effectiveEnd = std::min(fadeEnd, renderEnd);

    if (effectiveStart >= effectiveEnd) {
        return; // No intersection
    }

    int bufferStart = static_cast<int>(effectiveStart - renderStart);
    int numSamples = static_cast<int>(effectiveEnd - effectiveStart);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        float* samples = buffer.getWritePointer(ch, bufferStart);

        for (int i = 0; i < numSamples; ++i) {
            int64_t samplePos = effectiveStart + i;
            float fadePos = static_cast<float>(samplePos - fadeStart) / fade.length_samples;

            if (!isFadeIn) {
                fadePos = 1.0f - fadePos;
            }

            fadePos = std::max(0.0f, std::min(1.0f, fadePos));
            float gain = calculateFadeGain(fade.shape, fadePos);
            samples[i] *= gain;
        }
    }
}

float EdlRenderer::calculateFadeGain(EdlCompiler::FadeShape shape, float position) {
    switch (shape) {
        case EdlCompiler::FadeShape::Linear:
            return position;
        case EdlCompiler::FadeShape::EqualPower:
            return std::sqrt(position);
        default:
            return position;
    }
}

void EdlRenderer::addToMixBuffer(juce::AudioBuffer<float>& mixBuffer, const juce::AudioBuffer<float>& clipBuffer) {
    int numChannels = std::min(mixBuffer.getNumChannels(), clipBuffer.getNumChannels());
    int numSamples = std::min(mixBuffer.getNumSamples(), clipBuffer.getNumSamples());

    for (int ch = 0; ch < numChannels; ++ch) {
        juce::FloatVectorOperations::add(mixBuffer.getWritePointer(ch),
                                       clipBuffer.getReadPointer(ch), numSamples);
    }
}

juce::AudioFormatReader* EdlRenderer::getReader(const std::string& filePath) {
    auto it = readerCache_.find(filePath);
    if (it != readerCache_.end()) {
        return it->second.get();
    }

    juce::File file(filePath);
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(file));
    if (!reader) {
        return nullptr;
    }

    juce::AudioFormatReader* rawReader = reader.get();
    readerCache_[filePath] = std::move(reader);
    return rawReader;
}

bool EdlRenderer::writeWavFile(const juce::AudioBuffer<float>& buffer, int sampleRate,
                              const std::string& outputPath, BitDepth bitDepth, std::string& error) {

    juce::File outputFile(outputPath);
    outputFile.getParentDirectory().createDirectory();

    if (outputFile.exists()) {
        outputFile.deleteFile();
    }

    std::unique_ptr<juce::FileOutputStream> outputStream(outputFile.createOutputStream());
    if (!outputStream) {
        error = "Cannot create output file: " + outputPath;
        return false;
    }

    juce::WavAudioFormat wavFormat;
    int bitsPerSample = static_cast<int>(bitDepth);

    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(outputStream.get(), sampleRate, buffer.getNumChannels(),
                                 bitsPerSample, {}, 0));

    if (!writer) {
        error = "Cannot create WAV writer for: " + outputPath;
        return false;
    }

    outputStream.release(); // Writer takes ownership

    bool success = writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer.reset(); // Ensure file is flushed and closed

    if (!success) {
        error = "Failed to write audio data to: " + outputPath;
        return false;
    }

    return true;
}

std::vector<EdlCompiler::CompiledClip> EdlRenderer::getClipsInRange(const EdlCompiler::CompiledTrack& track,
                                                                   int64_t rangeStart, int64_t rangeEnd) {
    std::vector<EdlCompiler::CompiledClip> result;

    for (const auto& clip : track.clips) {
        if (clip.t1 > rangeStart && clip.t0 < rangeEnd) {
            result.push_back(clip);
        }
    }

    return result;
}

void EdlRenderer::ensureBufferSize(juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) {
    if (buffer.getNumChannels() != numChannels || buffer.getNumSamples() != numSamples) {
        buffer.setSize(numChannels, numSamples, false, true, true);
    }
}

} // namespace juceaudioservice