#include <JuceAudioService/AudioService.h>
#include <JuceAudioService/AudioFileSource.h>
#include <JuceAudioService/OfflineRenderer.h>
#include <juce_core/juce_core.h>
#include <iostream>
#include <iomanip>

enum class RenderMode
{
    Sine,
    File
};

struct RenderOptions
{
    RenderMode mode = RenderMode::Sine;

    // Sine wave generation options
    double frequency = 440.0;

    // File input options
    juce::String inputFile;
    double startTime = 0.0;

    // Common options
    double duration = 1.0;
    double sampleRate = 44100.0;
    int channels = 1;
    int bitDepth = 16;
    juce::String outputFile;
    bool logJson = false;
};

void printUsage(const char* programName)
{
    std::cout << "Usage: " << programName << " [options]\n"
              << "\nSine wave generation mode:\n"
              << "  --sine              Generate sine wave\n"
              << "  --freq <Hz>         Frequency in Hz (default: 440)\n"
              << "  --dur <seconds>     Duration in seconds (default: 1.0)\n"
              << "  --sr <rate>         Output sample rate (default: 44100)\n"
              << "  --ch <channels>     Number of channels (default: 1)\n"
              << "\nFile windowing mode:\n"
              << "  --in <file>         Input audio file\n"
              << "  --start <seconds>   Start time in seconds (default: 0.0)\n"
              << "  --dur <seconds>     Duration in seconds (required)\n"
              << "  --sr <rate>         Output sample rate (default: match input)\n"
              << "\nCommon options:\n"
              << "  --bit-depth <bits>  Bit depth: 16, 24, or 32 (default: 16)\n"
              << "  --out <file>        Output file path (required)\n"
              << "  --log-json          Enable JSON logging with PCM hash\n"
              << "  --help              Show this help message\n";
}

bool parseArguments(int argc, char* argv[], RenderOptions& options)
{
    for (int i = 1; i < argc; ++i)
    {
        juce::String arg(argv[i]);

        if (arg == "--sine")
        {
            options.mode = RenderMode::Sine;
        }
        else if (arg == "--in" && i + 1 < argc)
        {
            options.mode = RenderMode::File;
            options.inputFile = juce::String(argv[++i]);
        }
        else if (arg == "--start" && i + 1 < argc)
        {
            options.startTime = std::atof(argv[++i]);
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
    if (options.outputFile.isEmpty())
    {
        std::cerr << "Error: Output file must be specified with --out" << std::endl;
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

    if (options.bitDepth != 16 && options.bitDepth != 24 && options.bitDepth != 32)
    {
        std::cerr << "Error: Bit depth must be 16, 24, or 32" << std::endl;
        return false;
    }

    // Mode-specific validation
    if (options.mode == RenderMode::Sine)
    {
        if (options.frequency <= 0.0)
        {
            std::cerr << "Error: Frequency must be positive" << std::endl;
            return false;
        }

        if (options.channels < 1 || options.channels > 8)
        {
            std::cerr << "Error: Channels must be between 1 and 8" << std::endl;
            return false;
        }
    }
    else if (options.mode == RenderMode::File)
    {
        if (options.inputFile.isEmpty())
        {
            std::cerr << "Error: Input file must be specified with --in" << std::endl;
            return false;
        }

        if (options.startTime < 0.0)
        {
            std::cerr << "Error: Start time must be non-negative" << std::endl;
            return false;
        }
    }

    return true;
}

void logJsonSine(const RenderOptions& options, bool success, const juce::String& pcmHash = {}, const juce::String& errorMessage = {})
{
    if (!options.logJson)
        return;

    std::cout << "{\n";
    std::cout << "  \"mode\": \"sine\",\n";
    std::cout << "  \"frequency\": " << options.frequency << ",\n";
    std::cout << "  \"duration\": " << options.duration << ",\n";
    std::cout << "  \"sample_rate\": " << options.sampleRate << ",\n";
    std::cout << "  \"channels\": " << options.channels << ",\n";
    std::cout << "  \"bit_depth\": " << options.bitDepth << ",\n";
    std::cout << "  \"output_file\": \"" << options.outputFile << "\",\n";
    std::cout << "  \"success\": " << (success ? "true" : "false");

    if (success && pcmHash.isNotEmpty())
    {
        std::cout << ",\n  \"pcm_checksum\": \"" << pcmHash << "\"";
    }

    if (!success && errorMessage.isNotEmpty())
    {
        std::cout << ",\n  \"error\": \"" << errorMessage << "\"";
    }

    std::cout << "\n}\n";
}

void logJsonFile(const RenderOptions& options, bool success,
                 double srcSampleRate, int srcChannels, juce::int64 startFrame, int frames,
                 const juce::String& pcmHash = {}, const juce::String& errorMessage = {})
{
    if (!options.logJson)
        return;

    std::cout << "{\n";
    std::cout << "  \"mode\": \"file\",\n";
    std::cout << "  \"source\": \"" << options.inputFile << "\",\n";
    std::cout << "  \"start_sec\": " << std::fixed << std::setprecision(2) << options.startTime << ",\n";
    std::cout << "  \"dur_sec\": " << std::fixed << std::setprecision(2) << options.duration << ",\n";
    std::cout << "  \"start_frame\": " << startFrame << ",\n";
    std::cout << "  \"frames\": " << frames << ",\n";
    std::cout << "  \"src_sr\": " << static_cast<int>(srcSampleRate) << ",\n";
    std::cout << "  \"out_sr\": " << static_cast<int>(options.sampleRate) << ",\n";
    std::cout << "  \"channels\": " << srcChannels << ",\n";
    std::cout << "  \"bit_depth\": " << options.bitDepth << ",\n";
    std::cout << "  \"success\": " << (success ? "true" : "false");

    if (success && pcmHash.isNotEmpty())
    {
        std::cout << ",\n  \"pcm_checksum\": \"" << pcmHash << "\"";
    }

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

    try
    {
        // Create audio service
        juceaudioservice::AudioService audioService;
        audioService.initialise();

        juce::AudioBuffer<float> buffer;

        if (options.mode == RenderMode::Sine)
        {
            // Sine wave generation mode
            if (!options.logJson)
            {
                std::cout << "Generating " << options.frequency << " Hz sine wave for "
                          << options.duration << " seconds..." << std::endl;
            }

            buffer = audioService.generateSineWave(
                options.frequency,
                options.duration,
                options.sampleRate,
                options.channels);

            // Write to file
            juce::File outputFile(options.outputFile);
            bool writeSuccess = audioService.writeAudioFile(buffer, outputFile, options.sampleRate, options.bitDepth);

            if (writeSuccess)
            {
                auto pcmHash = audioService.computePCMHash(buffer, options.bitDepth);

                if (!options.logJson)
                {
                    std::cout << "Successfully generated audio file: " << options.outputFile << std::endl;
                }

                logJsonSine(options, true, pcmHash);
                return 0;
            }
            else
            {
                const auto errorMsg = "Failed to write audio file";
                if (!options.logJson)
                {
                    std::cerr << "Error: " << errorMsg << std::endl;
                }
                logJsonSine(options, false, {}, errorMsg);
                return 1;
            }
        }
        else // RenderMode::File
        {
            // File windowing mode
            juceaudioservice::AudioFileSource fileSource;

            // Load input file
            juce::File inputFile(options.inputFile);
            if (!fileSource.loadFile(inputFile))
            {
                const auto errorMsg = "Failed to load input file: " + options.inputFile;
                if (!options.logJson)
                {
                    std::cerr << "Error: " << errorMsg << std::endl;
                }
                logJsonFile(options, false, 0, 0, 0, 0, {}, errorMsg);
                return 1;
            }

            const double srcSampleRate = fileSource.getSampleRate();
            const int srcChannels = fileSource.getNumChannels();
            const auto totalLength = fileSource.getTotalLength();

            // Log file info
            if (!options.logJson)
            {
                std::cout << "[Render] file { path: " << options.inputFile
                          << ", src_sr: " << static_cast<int>(srcSampleRate)
                          << ", ch: " << srcChannels << " }" << std::endl;
            }

            // Calculate window parameters
            const auto startFrame = static_cast<juce::int64>(juce::roundToInt(options.startTime * srcSampleRate));
            const auto requestedFrames = juce::roundToInt(options.duration * srcSampleRate);

            // Validate window bounds
            if (startFrame >= totalLength)
            {
                const auto errorMsg = "Start time is beyond file length";
                if (!options.logJson)
                {
                    std::cerr << "Error: " << errorMsg << std::endl;
                }
                logJsonFile(options, false, srcSampleRate, srcChannels, startFrame, requestedFrames, {}, errorMsg);
                return 1;
            }

            // Clamp to file length (truncate if window overruns)
            const auto actualFrames = juce::jmin(requestedFrames, static_cast<int>(totalLength - startFrame));

            if (!options.logJson)
            {
                std::cout << "[Render] window { start_sec: " << std::fixed << std::setprecision(2) << options.startTime
                          << ", dur_sec: " << options.duration
                          << ", start_frame: " << startFrame
                          << ", frames: " << actualFrames << " }" << std::endl;
            }

            // Use default sample rate if not specified
            double outputSampleRate = options.sampleRate;
            if (juce::approximatelyEqual(outputSampleRate, 44100.0)) // Default value
            {
                outputSampleRate = srcSampleRate; // Match input
            }

            // Render windowed audio
            juceaudioservice::OfflineRenderer renderer;
            buffer = renderer.renderWindow(fileSource, startFrame, actualFrames,
                                          srcSampleRate, outputSampleRate, srcChannels);

            // Write to file
            juce::File outputFile(options.outputFile);
            bool writeSuccess = audioService.writeAudioFile(buffer, outputFile, outputSampleRate, options.bitDepth);

            if (writeSuccess)
            {
                auto pcmHash = audioService.computePCMHash(buffer, options.bitDepth);

                if (!options.logJson)
                {
                    std::cout << "[Render] wrote wav { out: " << options.outputFile
                              << ", frames_out: " << buffer.getNumSamples()
                              << ", out_sr: " << static_cast<int>(outputSampleRate)
                              << ", bit_depth: " << options.bitDepth << " }" << std::endl;
                }

                logJsonFile(options, true, srcSampleRate, srcChannels, startFrame, actualFrames, pcmHash);
                return 0;
            }
            else
            {
                const auto errorMsg = "Failed to write output file";
                if (!options.logJson)
                {
                    std::cerr << "Error: " << errorMsg << std::endl;
                }
                logJsonFile(options, false, srcSampleRate, srcChannels, startFrame, actualFrames, {}, errorMsg);
                return 1;
            }
        }
    }
    catch (const std::exception& e)
    {
        const auto errorMsg = juce::String("Exception: ") + e.what();
        if (!options.logJson)
        {
            std::cerr << "Error: " << errorMsg << std::endl;
        }

        if (options.mode == RenderMode::Sine)
        {
            logJsonSine(options, false, {}, errorMsg);
        }
        else
        {
            logJsonFile(options, false, 0, 0, 0, 0, {}, errorMsg);
        }

        return 1;
    }
}