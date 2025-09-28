#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace juceaudioservice
{

/**
    AudioSource that loads and plays back audio files.

    Provides sample-accurate positioning and metadata access
    for use in windowed rendering operations.
*/
class AudioFileSource : public juce::AudioSource
{
public:
    AudioFileSource();
    ~AudioFileSource() override;

    /**
        Load an audio file.

        @param file The audio file to load
        @returns true if the file was loaded successfully
    */
    bool loadFile(const juce::File& file);

    /**
        Set the playback position in samples.

        @param newPosition The new position in samples from the start of the file
    */
    void setPosition(juce::int64 newPosition);

    /**
        Get the current playback position in samples.

        @returns The current position in samples from the start of the file
    */
    juce::int64 getPosition() const noexcept { return currentPosition; }

    /**
        Get the total length of the loaded file in samples.

        @returns The total length in samples, or 0 if no file is loaded
    */
    juce::int64 getTotalLength() const noexcept;

    /**
        Get the sample rate of the loaded file.

        @returns The sample rate in Hz, or 0 if no file is loaded
    */
    double getSampleRate() const noexcept;

    /**
        Get the number of channels in the loaded file.

        @returns The number of channels, or 0 if no file is loaded
    */
    int getNumChannels() const noexcept;

    /**
        Check if a file is currently loaded.

        @returns true if a file is loaded and ready for playback
    */
    bool isLoaded() const noexcept { return reader != nullptr; }

    // AudioSource interface
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

private:
    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::AudioFormatReader> reader;
    juce::int64 currentPosition { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioFileSource)
};

} // namespace juceaudioservice