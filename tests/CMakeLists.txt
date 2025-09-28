cmake_minimum_required(VERSION 3.20)

set(TEST_TARGET JuceAudioServiceTests)

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

