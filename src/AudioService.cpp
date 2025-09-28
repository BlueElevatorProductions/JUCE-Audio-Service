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

} // namespace juceaudioservice

