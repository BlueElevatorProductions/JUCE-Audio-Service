#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace juceaudioservice
{

/**
    JUCE-based audio service with audio generation and file writing capabilities.

    The class provides audio synthesis and file output functionality for testing
    and audio processing applications. It supports sine wave generation and
    writing audio files in various formats.
*/
class AudioService
{
public:
    AudioService() = default;
    ~AudioService() = default;

    /** Returns the human-readable name for the audio service. */
    [[nodiscard]] juce::String getServiceName() const noexcept;

    /** Reports whether the service has been initialised. */
    [[nodiscard]] bool isInitialised() const noexcept { return initialised; }

    /**
        Perform basic initialisation logic for the audio service.

        For the initial scaffold this flips an internal flag, but the method
        exists so downstream patches can attach audio-device setup and other
        resource acquisition tasks.
    */
    void initialise();

    /**
        Generate a sine wave and return it as an AudioBuffer.

        @param frequency The frequency of the sine wave in Hz
        @param durationSeconds The duration of the sine wave in seconds
        @param sampleRate The sample rate in Hz
        @param numChannels The number of audio channels
        @returns An AudioBuffer containing the generated sine wave
    */
    [[nodiscard]] juce::AudioBuffer<float> generateSineWave(
        double frequency,
        double durationSeconds,
        double sampleRate,
        int numChannels = 1) const;

    /**
        Write an AudioBuffer to an audio file.

        @param buffer The audio buffer to write
        @param outputFile The file to write to
        @param sampleRate The sample rate of the audio
        @param bitDepth The bit depth (16, 24, or 32)
        @returns true if the file was written successfully, false otherwise
    */
    bool writeAudioFile(
        const juce::AudioBuffer<float>& buffer,
        const juce::File& outputFile,
        double sampleRate,
        int bitDepth = 16) const;

private:
    bool initialised { false };
};

} // namespace juceaudioservice

