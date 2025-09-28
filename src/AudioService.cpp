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

juce::String AudioService::computePCMHash(
    const juce::AudioBuffer<float>& buffer,
    int bitDepth) const
{
    juce::ScopedNoDenormals noDenormals;

    // Create a memory output stream to collect PCM data
    juce::MemoryOutputStream pcmStream;

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Convert and write PCM data based on bit depth
    for (int sample = 0; sample < numSamples; ++sample)
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float floatSample = buffer.getSample(channel, sample);

            if (bitDepth == 16)
            {
                // Convert to 16-bit signed integer
                const auto intSample = static_cast<juce::int16>(
                    juce::jlimit(-32768, 32767, static_cast<int>(floatSample * 32767.0f))
                );
                pcmStream.writeShort(intSample);
            }
            else if (bitDepth == 24)
            {
                // Convert to 24-bit signed integer (stored as 32-bit)
                const auto intSample = static_cast<juce::int32>(
                    juce::jlimit(-8388608, 8388607, static_cast<int>(floatSample * 8388607.0f))
                );
                // Write as 24-bit (3 bytes, little endian)
                pcmStream.writeByte(static_cast<char>(intSample & 0xFF));
                pcmStream.writeByte(static_cast<char>((intSample >> 8) & 0xFF));
                pcmStream.writeByte(static_cast<char>((intSample >> 16) & 0xFF));
            }
            else if (bitDepth == 32)
            {
                // Convert to 32-bit signed integer
                const auto intSample = static_cast<juce::int32>(
                    juce::jlimit(-2147483648LL, 2147483647LL,
                                static_cast<juce::int64>(floatSample * 2147483647.0f))
                );
                pcmStream.writeInt(intSample);
            }
        }
    }

    // Compute checksum of the PCM data
    const auto pcmData = pcmStream.getData();
    const auto pcmSize = pcmStream.getDataSize();

    // Simple CRC32-style checksum for deterministic verification
    juce::uint32 checksum = 0;
    const auto* bytes = static_cast<const juce::uint8*>(pcmData);

    for (size_t i = 0; i < pcmSize; ++i)
    {
        checksum = (checksum << 8) ^ bytes[i];
        checksum ^= (checksum >> 16);
    }

    return juce::String::toHexString(static_cast<int>(checksum)).paddedLeft('0', 8);
}

} // namespace juceaudioservice

