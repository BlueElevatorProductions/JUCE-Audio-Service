#include <JuceAudioService/AudioService.h>
#include <juce_core/juce_core.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <sstream>

class GoldenFileTest
{
public:
    static bool runSineWaveTest()
    {
        std::cout << "Running golden file test for sine wave generation..." << std::endl;

        // Test parameters
        const double frequency = 1000.0;
        const double duration = 2.0;
        const double sampleRate = 48000.0;
        const int channels = 1;
        const int bitDepth = 16;

        // File paths - find project root by looking for CMakeLists.txt
        auto projectRoot = juce::File::getCurrentWorkingDirectory();
        while (!projectRoot.getChildFile("CMakeLists.txt").exists() && projectRoot.getParentDirectory() != projectRoot)
        {
            projectRoot = projectRoot.getParentDirectory();
        }

        const auto testOutputDir = projectRoot.getChildFile("tests/data/output");
        const auto testOutputFile = testOutputDir.getChildFile("sine_1k_2s_48k.wav");
        const auto goldenHashFile = projectRoot.getChildFile("tests/data/golden/sine_1k_2s_48k.sha256");

        // Ensure output directory exists
        testOutputDir.createDirectory();

        // Generate audio file
        juceaudioservice::AudioService audioService;
        audioService.initialise();

        auto buffer = audioService.generateSineWave(frequency, duration, sampleRate, channels);

        if (!audioService.writeAudioFile(buffer, testOutputFile, sampleRate, bitDepth))
        {
            std::cerr << "Failed to write test audio file" << std::endl;
            return false;
        }

        // Calculate SHA256 of generated file
        auto actualHash = calculateSHA256(testOutputFile);
        if (actualHash.isEmpty())
        {
            std::cerr << "Failed to calculate SHA256 of generated file" << std::endl;
            return false;
        }

        // Read expected hash from golden file
        auto expectedHash = readGoldenHash(goldenHashFile);
        if (expectedHash.isEmpty())
        {
            std::cerr << "Failed to read golden hash file: " << goldenHashFile.getFullPathName() << std::endl;
            return false;
        }

        // Compare hashes
        if (actualHash.trim() == expectedHash.trim())
        {
            std::cout << "✓ Golden file test passed: hashes match" << std::endl;
            std::cout << "  Expected: " << expectedHash.trim() << std::endl;
            std::cout << "  Actual:   " << actualHash.trim() << std::endl;
            return true;
        }
        else
        {
            std::cerr << "✗ Golden file test failed: hash mismatch" << std::endl;
            std::cerr << "  Expected: " << expectedHash.trim() << std::endl;
            std::cerr << "  Actual:   " << actualHash.trim() << std::endl;
            return false;
        }
    }

private:
    static juce::String calculateSHA256(const juce::File& file)
    {
        if (!file.exists())
            return {};

        // Use system command to calculate SHA256
        auto command = "shasum -a 256 \"" + file.getFullPathName() + "\"";

        std::string result;
        std::array<char, 128> buffer;

        FILE* pipe = popen(command.toRawUTF8(), "r");
        if (!pipe)
            return {};

        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
        {
            result += buffer.data();
        }

        pclose(pipe);

        // Extract just the hash part (before the filename)
        auto resultString = juce::String(result);
        auto spaceIndex = resultString.indexOfChar(' ');
        if (spaceIndex > 0)
            return resultString.substring(0, spaceIndex);

        return resultString.trim();
    }

    static juce::String readGoldenHash(const juce::File& goldenHashFile)
    {
        if (!goldenHashFile.exists())
            return {};

        auto content = goldenHashFile.loadFileAsString();

        // Extract hash from "hash  filename" format
        auto spaceIndex = content.indexOfChar(' ');
        if (spaceIndex > 0)
            return content.substring(0, spaceIndex);

        return content.trim();
    }
};

int main()
{
    if (GoldenFileTest::runSineWaveTest())
    {
        std::cout << "All golden file tests passed" << std::endl;
        return 0;
    }
    else
    {
        std::cerr << "Golden file tests failed" << std::endl;
        return 1;
    }
}