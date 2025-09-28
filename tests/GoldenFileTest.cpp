#include <JuceAudioService/AudioService.h>
#include <JuceAudioService/AudioFileSource.h>
#include <JuceAudioService/OfflineRenderer.h>
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

        // Calculate PCM checksum directly from buffer
        auto actualChecksum = audioService.computePCMHash(buffer, bitDepth);

        // Read expected checksum from golden file
        if (!goldenHashFile.exists())
        {
            std::cerr << "Golden checksum file not found: " << goldenHashFile.getFullPathName() << std::endl;
            std::cerr << "Expected checksum: " << actualChecksum << std::endl;
            return false;
        }

        auto expectedChecksum = goldenHashFile.loadFileAsString().trim();

        // Compare checksums
        if (actualChecksum.trim() == expectedChecksum.trim())
        {
            std::cout << "✓ Golden file test passed: checksums match" << std::endl;
            std::cout << "  Expected: " << expectedChecksum.trim() << std::endl;
            std::cout << "  Actual:   " << actualChecksum.trim() << std::endl;
            return true;
        }
        else
        {
            std::cerr << "✗ Golden file test failed: checksum mismatch" << std::endl;
            std::cerr << "  Expected: " << expectedChecksum.trim() << std::endl;
            std::cerr << "  Actual:   " << actualChecksum.trim() << std::endl;
            return false;
        }
    }

    static bool runWindowedRenderTest()
    {
        std::cout << "Running golden file test for windowed rendering..." << std::endl;

        // Find project root
        auto projectRoot = juce::File::getCurrentWorkingDirectory();
        while (!projectRoot.getChildFile("CMakeLists.txt").exists() && projectRoot.getParentDirectory() != projectRoot)
        {
            projectRoot = projectRoot.getParentDirectory();
        }

        // File paths
        const auto fixtureFile = projectRoot.getChildFile("fixtures/voice.wav");
        const auto testOutputDir = projectRoot.getChildFile("tests/data/output");
        const auto testOutputFile = testOutputDir.getChildFile("voice_0_250ms.wav");
        const auto goldenChecksumFile = projectRoot.getChildFile("tests/data/golden/voice_0_250ms.checksum");

        // Check if fixture exists
        if (!fixtureFile.exists())
        {
            std::cerr << "Fixture file not found: " << fixtureFile.getFullPathName() << std::endl;
            std::cerr << "Run: ./build/tools/make_fixture_cli --out fixtures/voice.wav" << std::endl;
            return false;
        }

        // Ensure output directory exists
        testOutputDir.createDirectory();

        // Load the fixture
        juceaudioservice::AudioFileSource fileSource;
        if (!fileSource.loadFile(fixtureFile))
        {
            std::cerr << "Failed to load fixture file" << std::endl;
            return false;
        }

        // Get fixture metadata
        const double srcSampleRate = fileSource.getSampleRate();
        const int srcChannels = fileSource.getNumChannels();

        // Windowing parameters (same as in the CLI test)
        const double startTime = 0.0;
        const double duration = 0.25;
        const double outputSampleRate = 48000.0;
        const int bitDepth = 16;

        // Calculate window parameters
        const auto startFrame = static_cast<juce::int64>(juce::roundToInt(startTime * srcSampleRate));
        const auto frames = juce::roundToInt(duration * srcSampleRate);

        // Render windowed audio
        juceaudioservice::OfflineRenderer renderer;
        auto buffer = renderer.renderWindow(fileSource, startFrame, frames,
                                          srcSampleRate, outputSampleRate, srcChannels);

        // Write to test output file
        juceaudioservice::AudioService audioService;
        audioService.initialise();

        if (!audioService.writeAudioFile(buffer, testOutputFile, outputSampleRate, bitDepth))
        {
            std::cerr << "Failed to write test output file" << std::endl;
            return false;
        }

        // Compute PCM checksum
        auto actualChecksum = audioService.computePCMHash(buffer, bitDepth);

        // Read expected checksum from golden file
        if (!goldenChecksumFile.exists())
        {
            std::cerr << "Golden checksum file not found: " << goldenChecksumFile.getFullPathName() << std::endl;
            std::cerr << "Expected checksum: " << actualChecksum << std::endl;
            return false;
        }

        auto expectedChecksum = goldenChecksumFile.loadFileAsString().trim();

        // Compare checksums
        if (actualChecksum.trim() == expectedChecksum.trim())
        {
            std::cout << "✓ Windowed rendering test passed: checksums match" << std::endl;
            std::cout << "  Expected: " << expectedChecksum.trim() << std::endl;
            std::cout << "  Actual:   " << actualChecksum.trim() << std::endl;
            return true;
        }
        else
        {
            std::cerr << "✗ Windowed rendering test failed: checksum mismatch" << std::endl;
            std::cerr << "  Expected: " << expectedChecksum.trim() << std::endl;
            std::cerr << "  Actual:   " << actualChecksum.trim() << std::endl;
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
    bool allTestsPassed = true;

    if (!GoldenFileTest::runSineWaveTest())
    {
        allTestsPassed = false;
    }

    if (!GoldenFileTest::runWindowedRenderTest())
    {
        allTestsPassed = false;
    }

    if (allTestsPassed)
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