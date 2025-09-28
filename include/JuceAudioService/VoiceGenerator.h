#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace juceaudioservice
{

/**
    Deterministic voice-like synthesis AudioSource.

    Generates a sum of harmonics with ADSR envelope and vibrato
    for testing purposes. All parameters are fixed to ensure
    deterministic, reproducible output across platforms.
*/
class VoiceGenerator : public juce::AudioSource
{
public:
    /**
        Create a voice generator with fixed parameters.

        @param sampleRate The sample rate for synthesis
        @param durationSeconds Total duration of the voice
    */
    VoiceGenerator(double sampleRate, double durationSeconds);

    ~VoiceGenerator() override = default;

    // AudioSource interface
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

    /** Get the total length in samples */
    juce::int64 getTotalLength() const noexcept { return totalSamples; }

    /** Check if synthesis is complete */
    bool hasFinished() const noexcept { return currentSample >= totalSamples; }

private:
    // Fixed synthesis parameters for determinism
    static constexpr double fundamentalFreq = 150.0;        // Hz
    static constexpr double amplitude = 0.25;               // Fixed amplitude
    static constexpr double vibratoFreq = 5.0;              // Hz
    static constexpr double vibratoDepth = 0.01;            // Modulation depth

    // ADSR envelope (normalized times)
    static constexpr double attackTime = 0.01;             // 10ms
    static constexpr double decayTime = 0.1;               // 100ms
    static constexpr double sustainLevel = 0.7;            // 70%
    static constexpr double releaseTime = 0.1;             // 100ms (from end)

    // Harmonic content (amplitude ratios)
    static constexpr int numHarmonics = 5;
    static constexpr double harmonicAmps[numHarmonics] = {
        1.0,    // Fundamental
        0.5,    // 2nd harmonic
        0.25,   // 3rd harmonic
        0.125,  // 4th harmonic
        0.0625  // 5th harmonic
    };

    double currentSampleRate { 44100.0 };
    juce::int64 totalSamples { 0 };
    juce::int64 currentSample { 0 };

    // Oscillator phases for each harmonic
    double harmonicPhases[numHarmonics] { 0.0 };
    double vibratoPhase { 0.0 };

    // Calculate ADSR envelope value at current position
    double calculateEnvelope(juce::int64 sampleIndex) const;

    // Calculate vibrato modulation
    double calculateVibrato(juce::int64 sampleIndex) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceGenerator)
};

} // namespace juceaudioservice