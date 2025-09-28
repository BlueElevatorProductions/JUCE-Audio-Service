#include "JuceAudioService/OfflineRenderer.h"
#include "JuceAudioService/AudioFileSource.h"

namespace juceaudioservice
{

OfflineRenderer::OfflineRenderer()
{
}

juce::AudioBuffer<float> OfflineRenderer::renderToBuffer(
    juce::AudioSource& source,
    double sampleRate,
    int numChannels,
    int numSamples)
{
    juce::ScopedNoDenormals noDenormals;

    // Prepare the source
    source.prepareToPlay(renderBlockSize, sampleRate);

    // Create output buffer
    juce::AudioBuffer<float> outputBuffer(numChannels, numSamples);
    outputBuffer.clear();

    // Render in chunks
    int samplesRemaining = numSamples;
    int currentSample = 0;

    while (samplesRemaining > 0)
    {
        const int samplesToRender = juce::jmin(samplesRemaining, renderBlockSize);

        juce::AudioSourceChannelInfo channelInfo;
        channelInfo.buffer = &outputBuffer;
        channelInfo.startSample = currentSample;
        channelInfo.numSamples = samplesToRender;

        source.getNextAudioBlock(channelInfo);

        currentSample += samplesToRender;
        samplesRemaining -= samplesToRender;
    }

    source.releaseResources();

    return outputBuffer;
}

juce::AudioBuffer<float> OfflineRenderer::renderWindow(
    juce::AudioSource& source,
    juce::int64 startFrame,
    int numFrames,
    double sourceSampleRate,
    double outputSampleRate,
    int numChannels)
{
    juce::ScopedNoDenormals noDenormals;

    // Position the source at the start frame
    if (auto* fileSource = dynamic_cast<AudioFileSource*>(&source))
    {
        fileSource->setPosition(startFrame);
    }

    // Render at source sample rate first
    auto sourceBuffer = renderToBuffer(source, sourceSampleRate, numChannels, numFrames);

    // Convert sample rate if needed
    if (juce::approximatelyEqual(sourceSampleRate, outputSampleRate))
    {
        return sourceBuffer;
    }
    else
    {
        return convertSampleRate(sourceBuffer, sourceSampleRate, outputSampleRate);
    }
}

juce::AudioBuffer<float> OfflineRenderer::convertSampleRate(
    const juce::AudioBuffer<float>& inputBuffer,
    double inputSampleRate,
    double outputSampleRate)
{
    juce::ScopedNoDenormals noDenormals;

    if (juce::approximatelyEqual(inputSampleRate, outputSampleRate))
    {
        return inputBuffer; // No conversion needed
    }

    // Calculate the conversion ratio
    const double ratio = outputSampleRate / inputSampleRate;
    const int outputLength = static_cast<int>(inputBuffer.getNumSamples() * ratio);

    // Create output buffer
    juce::AudioBuffer<float> outputBuffer(inputBuffer.getNumChannels(), outputLength);

    // Simple linear interpolation resampling
    // Note: For production use, consider using JUCE's ResamplingAudioSource
    // but for deterministic testing, this simple approach is sufficient
    for (int channel = 0; channel < inputBuffer.getNumChannels(); ++channel)
    {
        const float* inputData = inputBuffer.getReadPointer(channel);
        float* outputData = outputBuffer.getWritePointer(channel);

        for (int i = 0; i < outputLength; ++i)
        {
            const double sourceIndex = i / ratio;
            const int sourceIndexInt = static_cast<int>(sourceIndex);
            const double fraction = sourceIndex - sourceIndexInt;

            if (sourceIndexInt < inputBuffer.getNumSamples() - 1)
            {
                // Linear interpolation
                const float sample1 = inputData[sourceIndexInt];
                const float sample2 = inputData[sourceIndexInt + 1];
                outputData[i] = sample1 + static_cast<float>(fraction) * (sample2 - sample1);
            }
            else if (sourceIndexInt < inputBuffer.getNumSamples())
            {
                // Last sample
                outputData[i] = inputData[sourceIndexInt];
            }
            else
            {
                // Beyond input range
                outputData[i] = 0.0f;
            }
        }
    }

    return outputBuffer;
}

} // namespace juceaudioservice