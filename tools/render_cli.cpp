#include <JuceAudioService/AudioService.h>
#include <juce_core/juce_core.h>
#include <iostream>
#include <iomanip>

struct RenderOptions
{
    double frequency = 440.0;
    double duration = 1.0;
    double sampleRate = 44100.0;
    int channels = 1;
    int bitDepth = 16;
    juce::String outputFile;
    bool logJson = false;
    bool sine = false;
};

void printUsage(const char* programName)
{
    std::cout << "Usage: " << programName << " [options]\n"
              << "Options:\n"
              << "  --sine              Generate sine wave\n"
              << "  --freq <Hz>         Frequency in Hz (default: 440)\n"
              << "  --dur <seconds>     Duration in seconds (default: 1.0)\n"
              << "  --sr <rate>         Sample rate in Hz (default: 44100)\n"
              << "  --ch <channels>     Number of channels (default: 1)\n"
              << "  --bit-depth <bits>  Bit depth: 16, 24, or 32 (default: 16)\n"
              << "  --out <file>        Output file path (required)\n"
              << "  --log-json          Enable JSON logging\n"
              << "  --help              Show this help message\n";
}

bool parseArguments(int argc, char* argv[], RenderOptions& options)
{
    for (int i = 1; i < argc; ++i)
    {
        juce::String arg(argv[i]);

        if (arg == "--sine")
        {
            options.sine = true;
        }
        else if (arg == "--freq" && i + 1 < argc)
        {
            options.frequency = std::atof(argv[++i]);
        }
        else if (arg == "--dur" && i + 1 < argc)
        {
            options.duration = std::atof(argv[++i]);
        }
        else if (arg == "--sr" && i + 1 < argc)
        {
            options.sampleRate = std::atof(argv[++i]);
        }
        else if (arg == "--ch" && i + 1 < argc)
        {
            options.channels = std::atoi(argv[++i]);
        }
        else if (arg == "--bit-depth" && i + 1 < argc)
        {
            options.bitDepth = std::atoi(argv[++i]);
        }
        else if (arg == "--out" && i + 1 < argc)
        {
            options.outputFile = juce::String(argv[++i]);
        }
        else if (arg == "--log-json")
        {
            options.logJson = true;
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

bool validateOptions(const RenderOptions& options)
{
    if (!options.sine)
    {
        std::cerr << "Error: Must specify --sine (only sine wave generation is currently supported)" << std::endl;
        return false;
    }

    if (options.outputFile.isEmpty())
    {
        std::cerr << "Error: Output file must be specified with --out" << std::endl;
        return false;
    }

    if (options.frequency <= 0.0)
    {
        std::cerr << "Error: Frequency must be positive" << std::endl;
        return false;
    }

    if (options.duration <= 0.0)
    {
        std::cerr << "Error: Duration must be positive" << std::endl;
        return false;
    }

    if (options.sampleRate <= 0.0)
    {
        std::cerr << "Error: Sample rate must be positive" << std::endl;
        return false;
    }

    if (options.channels < 1 || options.channels > 8)
    {
        std::cerr << "Error: Channels must be between 1 and 8" << std::endl;
        return false;
    }

    if (options.bitDepth != 16 && options.bitDepth != 24 && options.bitDepth != 32)
    {
        std::cerr << "Error: Bit depth must be 16, 24, or 32" << std::endl;
        return false;
    }

    return true;
}

void logJson(const RenderOptions& options, bool success, const juce::String& errorMessage = {})
{
    if (!options.logJson)
        return;

    std::cout << "{\n";
    std::cout << "  \"operation\": \"sine_wave_generation\",\n";
    std::cout << "  \"parameters\": {\n";
    std::cout << "    \"frequency\": " << options.frequency << ",\n";
    std::cout << "    \"duration\": " << options.duration << ",\n";
    std::cout << "    \"sample_rate\": " << options.sampleRate << ",\n";
    std::cout << "    \"channels\": " << options.channels << ",\n";
    std::cout << "    \"bit_depth\": " << options.bitDepth << ",\n";
    std::cout << "    \"output_file\": \"" << options.outputFile << "\"\n";
    std::cout << "  },\n";
    std::cout << "  \"success\": " << (success ? "true" : "false");

    if (!success && errorMessage.isNotEmpty())
    {
        std::cout << ",\n  \"error\": \"" << errorMessage << "\"";
    }

    std::cout << "\n}\n";
}

int main(int argc, char* argv[])
{
    RenderOptions options;

    if (!parseArguments(argc, argv, options))
    {
        printUsage(argv[0]);
        return 1;
    }

    if (!validateOptions(options))
    {
        return 1;
    }

    // Initialize JUCE (minimal setup for command-line tool)

    // Create audio service
    juceaudioservice::AudioService audioService;
    audioService.initialise();

    if (options.logJson)
    {
        std::cout << "Generating " << options.frequency << " Hz sine wave for "
                  << options.duration << " seconds..." << std::endl;
    }

    try
    {
        // Generate sine wave
        auto buffer = audioService.generateSineWave(
            options.frequency,
            options.duration,
            options.sampleRate,
            options.channels);

        // Write to file
        juce::File outputFile(options.outputFile);
        bool success = audioService.writeAudioFile(buffer, outputFile, options.sampleRate, options.bitDepth);

        if (success)
        {
            if (!options.logJson)
            {
                std::cout << "Successfully generated audio file: " << options.outputFile << std::endl;
            }
            logJson(options, true);
            return 0;
        }
        else
        {
            const auto errorMsg = "Failed to write audio file";
            if (!options.logJson)
            {
                std::cerr << "Error: " << errorMsg << std::endl;
            }
            logJson(options, false, errorMsg);
            return 1;
        }
    }
    catch (const std::exception& e)
    {
        const auto errorMsg = juce::String("Exception: ") + e.what();
        if (!options.logJson)
        {
            std::cerr << "Error: " << errorMsg << std::endl;
        }
        logJson(options, false, errorMsg);
        return 1;
    }
}