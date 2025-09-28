#include "JuceAudioService/AudioFileSource.h"

namespace juceaudioservice
{

AudioFileSource::AudioFileSource()
{
    // Register standard audio formats
    formatManager.registerBasicFormats();
}

AudioFileSource::~AudioFileSource()
{
    releaseResources();
}

bool AudioFileSource::loadFile(const juce::File& file)
{
    // Release any existing reader
    reader.reset();
    currentPosition = 0;

    if (!file.exists())
        return false;

    // Create a new reader for the file
    reader.reset(formatManager.createReaderFor(file));

    return reader != nullptr;
}

void AudioFileSource::setPosition(juce::int64 newPosition)
{
    if (reader != nullptr)
    {
        currentPosition = juce::jlimit<juce::int64>(0, reader->lengthInSamples, newPosition);
    }
    else
    {
        currentPosition = 0;
    }
}

juce::int64 AudioFileSource::getTotalLength() const noexcept
{
    return reader != nullptr ? reader->lengthInSamples : 0;
}

double AudioFileSource::getSampleRate() const noexcept
{
    return reader != nullptr ? reader->sampleRate : 0.0;
}

int AudioFileSource::getNumChannels() const noexcept
{
    return reader != nullptr ? static_cast<int>(reader->numChannels) : 0;
}

void AudioFileSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    juce::ignoreUnused(samplesPerBlockExpected, sampleRate);
    // Nothing specific to prepare for file reading
}

void AudioFileSource::releaseResources()
{
    reader.reset();
    currentPosition = 0;
}

void AudioFileSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Clear the buffer first
    bufferToFill.clearActiveBufferRegion();

    if (reader == nullptr)
        return;

    const auto startSample = bufferToFill.startSample;
    const auto numSamples = bufferToFill.numSamples;
    const auto totalLength = reader->lengthInSamples;

    // Check if we're at or past the end of the file
    if (currentPosition >= totalLength)
        return;

    // Calculate how many samples we can actually read
    const auto samplesToRead = juce::jmin(numSamples,
                                         static_cast<int>(totalLength - currentPosition));

    if (samplesToRead <= 0)
        return;

    // Read from the file
    const bool readSuccess = reader->read(bufferToFill.buffer,
                                        startSample,
                                        samplesToRead,
                                        currentPosition,
                                        true,  // useLeftChan
                                        true); // useRightChan

    if (readSuccess)
    {
        currentPosition += samplesToRead;
    }

    // If we read fewer samples than requested, the rest of the buffer
    // remains cleared (silence)
}

} // namespace juceaudioservice