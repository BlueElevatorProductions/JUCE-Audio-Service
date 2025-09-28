#include "JuceAudioService/VoiceGenerator.h"
#include <juce_core/juce_core.h>

namespace juceaudioservice
{

VoiceGenerator::VoiceGenerator(double sampleRate, double durationSeconds)
    : currentSampleRate(sampleRate)
    , totalSamples(static_cast<juce::int64>(durationSeconds * sampleRate))
{
    // Initialize all oscillator phases to zero for determinism
    for (int i = 0; i < numHarmonics; ++i)
        harmonicPhases[i] = 0.0;

    vibratoPhase = 0.0;
    currentSample = 0;
}

void VoiceGenerator::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    juce::ignoreUnused(samplesPerBlockExpected);
    currentSampleRate = sampleRate;
}

void VoiceGenerator::releaseResources()
{
    // Nothing to release
}

void VoiceGenerator::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Use ScopedNoDenormals for deterministic floating-point behavior
    juce::ScopedNoDenormals noDenormals;

    auto* buffer = bufferToFill.buffer;
    const auto startSample = bufferToFill.startSample;
    const auto numSamples = bufferToFill.numSamples;

    // Clear the buffer first
    buffer->clear(startSample, numSamples);

    // Generate samples only if we haven't finished
    for (int sample = 0; sample < numSamples; ++sample)
    {
        if (currentSample >= totalSamples)
            break;

        float outputSample = 0.0f;

        // Calculate vibrato modulation
        const double vibrato = calculateVibrato(currentSample);
        const double envelope = calculateEnvelope(currentSample);

        // Generate harmonics
        for (int harmonic = 0; harmonic < numHarmonics; ++harmonic)
        {
            const double freq = fundamentalFreq * (harmonic + 1) * (1.0 + vibrato);
            const double phase = harmonicPhases[harmonic];
            const double harmonicAmp = harmonicAmps[harmonic];

            // Generate sine wave for this harmonic
            outputSample += static_cast<float>(
                harmonicAmp * std::sin(phase) * envelope * amplitude
            );

            // Update phase
            const double angleDelta = juce::MathConstants<double>::twoPi * freq / currentSampleRate;
            harmonicPhases[harmonic] += angleDelta;

            // Keep phase in reasonable range
            if (harmonicPhases[harmonic] >= juce::MathConstants<double>::twoPi)
                harmonicPhases[harmonic] -= juce::MathConstants<double>::twoPi;
        }

        // Update vibrato phase
        const double vibratoAngleDelta = juce::MathConstants<double>::twoPi * vibratoFreq / currentSampleRate;
        vibratoPhase += vibratoAngleDelta;
        if (vibratoPhase >= juce::MathConstants<double>::twoPi)
            vibratoPhase -= juce::MathConstants<double>::twoPi;

        // Write to all channels (mono source)
        for (int channel = 0; channel < buffer->getNumChannels(); ++channel)
        {
            buffer->addSample(channel, startSample + sample, outputSample);
        }

        ++currentSample;
    }
}

double VoiceGenerator::calculateEnvelope(juce::int64 sampleIndex) const
{
    const double position = static_cast<double>(sampleIndex) / static_cast<double>(totalSamples);

    // ADSR envelope calculation
    const double attackEnd = attackTime;
    const double decayEnd = attackTime + decayTime;
    const double releaseStart = 1.0 - releaseTime;

    if (position <= attackEnd)
    {
        // Attack phase: 0 to 1
        return position / attackEnd;
    }
    else if (position <= decayEnd)
    {
        // Decay phase: 1 to sustainLevel
        const double decayProgress = (position - attackEnd) / decayTime;
        return 1.0 - (1.0 - sustainLevel) * decayProgress;
    }
    else if (position <= releaseStart)
    {
        // Sustain phase: constant sustainLevel
        return sustainLevel;
    }
    else
    {
        // Release phase: sustainLevel to 0
        const double releaseProgress = (position - releaseStart) / releaseTime;
        return sustainLevel * (1.0 - releaseProgress);
    }
}

double VoiceGenerator::calculateVibrato(juce::int64 sampleIndex) const
{
    juce::ignoreUnused(sampleIndex);
    // Simple sinusoidal vibrato
    return vibratoDepth * std::sin(vibratoPhase);
}

} // namespace juceaudioservice