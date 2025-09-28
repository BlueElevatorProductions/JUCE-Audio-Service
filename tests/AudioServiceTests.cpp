#include <JuceAudioService/AudioService.h>

#include <iostream>

int main()
{
    juceaudioservice::AudioService service;

    if (service.isInitialised())
    {
        std::cerr << "Service should not be initialised on construction" << std::endl;
        return 1;
    }

    if (service.getServiceName() != juce::String { "JUCE Audio Service" })
    {
        std::cerr << "Unexpected service name" << std::endl;
        return 1;
    }

    service.initialise();

    if (! service.isInitialised())
    {
        std::cerr << "Service should report as initialised after calling initialise()" << std::endl;
        return 1;
    }

    std::cout << "All JuceAudioService tests passed" << std::endl;
    return 0;
}

