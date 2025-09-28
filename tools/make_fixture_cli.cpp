#include <JuceAudioService/AudioService.h>
#include <JuceAudioService/VoiceGenerator.h>
#include <JuceAudioService/OfflineRenderer.h>
#include <juce_core/juce_core.h>
#include <iostream>

struct FixtureOptions
{
    juce::String outputFile;
};

void printUsage(const char* programName)
{
    std::cout << "Usage: " << programName << " [options]\n"
              << "Options:\n"
              << "  --out <file>    Output file path (required)\n"
              << "  --help          Show this help message\n"
              << "\n"
              << "Generates a 0.5s mono voice-like audio fixture at 48kHz for testing.\n";
}

bool parseArguments(int argc, char* argv[], FixtureOptions& options)
{
    for (int i = 1; i < argc; ++i)
    {
        juce::String arg(argv[i]);

        if (arg == "--out" && i + 1 < argc)
        {
            options.outputFile = juce::String(argv[++i]);
        }
        else if (arg == "--help")
        {
            return false;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << std::endl;
            return false;
        }
    }

    return true;
}

bool validateOptions(const FixtureOptions& options)
{
    if (options.outputFile.isEmpty())
    {
        std::cerr << "Error: Output file must be specified with --out" << std::endl;
        return false;
    }

    return true;
}

int main(int argc, char* argv[])
{
    FixtureOptions options;

    if (!parseArguments(argc, argv, options))
    {
        printUsage(argv[0]);
        return 1;
    }

    if (!validateOptions(options))
    {
        return 1;
    }

    try
    {
        // Fixed parameters for deterministic voice fixture
        constexpr double sampleRate = 48000.0;
        constexpr double duration = 0.5;  // 0.5 seconds
        constexpr int numChannels = 1;    // Mono
        constexpr int bitDepth = 16;      // 16-bit

        std::cout << "[Generate] Creating voice fixture: " << duration << "s @ "
                  << static_cast<int>(sampleRate) << "Hz mono" << std::endl;

        // Create voice generator
        juceaudioservice::VoiceGenerator voiceGen(sampleRate, duration);

        // Render to buffer using offline renderer
        juceaudioservice::OfflineRenderer renderer;
        const int totalSamples = static_cast<int>(duration * sampleRate);

        auto buffer = renderer.renderToBuffer(voiceGen, sampleRate, numChannels, totalSamples);

        // Write to file
        juceaudioservice::AudioService audioService;
        audioService.initialise();

        juce::File outputFile(options.outputFile);
        bool success = audioService.writeAudioFile(buffer, outputFile, sampleRate, bitDepth);

        if (success)
        {
            std::cout << "[Generate] Wrote: " << options.outputFile << std::endl;
            return 0;
        }
        else
        {
            std::cerr << "Error: Failed to write fixture file" << std::endl;
            return 1;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: Exception during fixture generation: " << e.what() << std::endl;
        return 1;
    }
}