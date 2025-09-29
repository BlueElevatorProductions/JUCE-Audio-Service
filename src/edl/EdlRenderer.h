#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include "EdlCompiler.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace juceaudioservice {

/**
 * Offline renderer for compiled EDL timelines.
 *
 * Renders time ranges from compiled EDL to audio buffers with
 * proper gain, fade, and crossfade handling. Supports multiple
 * bit depths for WAV output.
 */
class EdlRenderer {
public:
    using ProgressCallback = std::function<void(double fraction)>;

    enum class BitDepth {
        Int16 = 16,
        Int24 = 24,
        Float32 = 32
    };

    EdlRenderer();
    ~EdlRenderer() = default;

    /**
     * Render a time range from compiled EDL to WAV file.
     *
     * @param compiledEdl The compiled EDL timeline
     * @param range Time range to render
     * @param outputPath Output WAV file path
     * @param bitDepth Output bit depth
     * @param progressCallback Optional progress callback (0.0 to 1.0)
     * @param error Output parameter for error message
     * @return true if rendering succeeded
     */
    bool renderToWav(const EdlCompiler::CompiledEdl& compiledEdl,
                     const audio_engine::TimeRange& range,
                     const std::string& outputPath,
                     BitDepth bitDepth,
                     ProgressCallback progressCallback,
                     std::string& error);

    /**
     * Render a time range from compiled EDL to audio buffer.
     *
     * @param compiledEdl The compiled EDL timeline
     * @param range Time range to render
     * @param outputBuffer Output buffer (will be resized)
     * @param progressCallback Optional progress callback (0.0 to 1.0)
     * @param error Output parameter for error message
     * @return true if rendering succeeded
     */
    bool renderToBuffer(const EdlCompiler::CompiledEdl& compiledEdl,
                       const audio_engine::TimeRange& range,
                       juce::AudioBuffer<float>& outputBuffer,
                       ProgressCallback progressCallback,
                       std::string& error);

private:
    static constexpr int blockSize_ = 4096;

    juce::AudioFormatManager formatManager_;
    std::unordered_map<std::string, std::unique_ptr<juce::AudioFormatReader>> readerCache_;

    // Core rendering methods
    bool renderTimeRange(const EdlCompiler::CompiledEdl& compiledEdl,
                        const audio_engine::TimeRange& range,
                        juce::AudioBuffer<float>& outputBuffer,
                        ProgressCallback progressCallback,
                        std::string& error);

    void renderTrack(const EdlCompiler::CompiledTrack& track,
                    int64_t rangeStart, int64_t rangeEnd,
                    juce::AudioBuffer<float>& mixBuffer,
                    int64_t bufferOffset);

    void renderClip(const EdlCompiler::CompiledClip& clip,
                   int64_t rangeStart, int64_t rangeEnd,
                   juce::AudioBuffer<float>& clipBuffer,
                   int64_t bufferOffset);

    // Audio processing
    void applyGain(juce::AudioBuffer<float>& buffer, float gainLinear);
    void applyFade(juce::AudioBuffer<float>& buffer, const EdlCompiler::FadeSpec& fade,
                  int64_t clipStart, int64_t clipEnd, int64_t renderStart, int64_t renderEnd,
                  bool isFadeIn);

    float calculateFadeGain(EdlCompiler::FadeShape shape, float position);
    void addToMixBuffer(juce::AudioBuffer<float>& mixBuffer, const juce::AudioBuffer<float>& clipBuffer);

    // File I/O
    juce::AudioFormatReader* getReader(const std::string& filePath);
    bool writeWavFile(const juce::AudioBuffer<float>& buffer, int sampleRate,
                     const std::string& outputPath, BitDepth bitDepth, std::string& error);

    // Helper methods
    std::vector<EdlCompiler::CompiledClip> getClipsInRange(const EdlCompiler::CompiledTrack& track,
                                                          int64_t rangeStart, int64_t rangeEnd);
    void ensureBufferSize(juce::AudioBuffer<float>& buffer, int numChannels, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EdlRenderer)
};

} // namespace juceaudioservice