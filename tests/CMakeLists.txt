cmake_minimum_required(VERSION 3.20)

set(TEST_TARGET JuceAudioServiceTests)
set(GOLDEN_TEST_TARGET GoldenFileTest)

add_executable(${TEST_TARGET}
    AudioServiceTests.cpp
)

target_link_libraries(${TEST_TARGET}
    PRIVATE
        JuceAudioService::JuceAudioService
        juce::juce_core
)

target_compile_features(${TEST_TARGET} PRIVATE cxx_std_20)

add_test(NAME ${TEST_TARGET} COMMAND ${TEST_TARGET})

# Golden file test
add_executable(${GOLDEN_TEST_TARGET}
    GoldenFileTest.cpp
)

target_link_libraries(${GOLDEN_TEST_TARGET}
    PRIVATE
        JuceAudioService::JuceAudioService
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_formats
)

target_compile_features(${GOLDEN_TEST_TARGET} PRIVATE cxx_std_20)

target_compile_definitions(${GOLDEN_TEST_TARGET}
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

add_test(NAME ${GOLDEN_TEST_TARGET} COMMAND ${GOLDEN_TEST_TARGET})

