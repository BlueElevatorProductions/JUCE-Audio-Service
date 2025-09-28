#include "JuceAudioService/AudioService.h"

namespace juceaudioservice
{

juce::String AudioService::getServiceName() const noexcept
{
    return "JUCE Audio Service";
}

void AudioService::initialise()
{
    initialised = true;
}

juce::AudioBuffer<float> AudioService::generateSineWave(
    double frequency,
    double durationSeconds,
    double sampleRate,
    int numChannels) const
{
    const auto numSamples = static_cast<int>(durationSeconds * sampleRate);
    juce::AudioBuffer<float> buffer(numChannels, numSamples);

    const auto angleDelta = juce::MathConstants<double>::twoPi * frequency / sampleRate;

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        auto currentAngle = 0.0;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            channelData[sample] = static_cast<float>(std::sin(currentAngle));
            currentAngle += angleDelta;
        }
    }

    return buffer;
}

bool AudioService::writeAudioFile(
    const juce::AudioBuffer<float>& buffer,
    const juce::File& outputFile,
    double sampleRate,
    int bitDepth) const
{
    // Ensure the output directory exists
    outputFile.getParentDirectory().createDirectory();

    // Create a WAV format writer
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer;

    {
        auto outputStream = std::make_unique<juce::FileOutputStream>(outputFile);
        if (!outputStream->openedOk())
            return false;

        writer.reset(wavFormat.createWriterFor(
            outputStream.release(),
            sampleRate,
            static_cast<unsigned int>(buffer.getNumChannels()),
            bitDepth,
            {},
            0));
    }

    if (writer == nullptr)
        return false;

    // Write the buffer to the file
    return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

} // namespace juceaudioservice

