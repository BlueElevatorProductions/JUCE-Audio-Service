#pragma once

#include <juce_core/juce_core.h>

namespace juceaudioservice
{

/**
    Minimal JUCE-based audio service scaffold.

    The class currently exposes a simple API surface that will evolve as the
    project grows. It provides a JUCE-friendly identifier that ensures JUCE
    headers are wired correctly in both the library and the test harness.
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

private:
    bool initialised { false };
};

} // namespace juceaudioservice

