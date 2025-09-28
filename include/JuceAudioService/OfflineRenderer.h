#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace juceaudioservice
{

/**
    Offline renderer for AudioSource to AudioBuffer conversion.

    Provides frame-accurate windowing and sample rate conversion
    for deterministic audio processing.
*/
class OfflineRenderer
{
public:
    OfflineRenderer();
    ~OfflineRenderer() = default;

    /**
        Render an AudioSource to an AudioBuffer.

        @param source The AudioSource to render
        @param sampleRate The sample rate for rendering
        @param numChannels The number of channels to render
        @param numSamples The number of samples to render
        @returns An AudioBuffer containing the rendered audio
    */
    juce::AudioBuffer<float> renderToBuffer(
        juce::AudioSource& source,
        double sampleRate,
        int numChannels,
        int numSamples);

    /**
        Render a windowed section of an AudioSource.

        @param source The AudioSource to render from
        @param startFrame The starting frame in the source
        @param numFrames The number of frames to render
        @param sourceSampleRate The sample rate of the source
        @param outputSampleRate The desired output sample rate
        @param numChannels The number of channels
        @returns An AudioBuffer containing the windowed audio
    */
    juce::AudioBuffer<float> renderWindow(
        juce::AudioSource& source,
        juce::int64 startFrame,
        int numFrames,
        double sourceSampleRate,
        double outputSampleRate,
        int numChannels);

private:
    // Buffer size for rendering chunks
    static constexpr int renderBlockSize = 1024;

    /**
        Perform sample rate conversion if needed.

        @param inputBuffer The input buffer
        @param inputSampleRate The input sample rate
        @param outputSampleRate The desired output sample rate
        @returns The converted buffer (or original if no conversion needed)
    */
    juce::AudioBuffer<float> convertSampleRate(
        const juce::AudioBuffer<float>& inputBuffer,
        double inputSampleRate,
        double outputSampleRate);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OfflineRenderer)
};

} // namespace juceaudioservice