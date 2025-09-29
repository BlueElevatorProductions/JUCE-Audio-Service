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

target_compile_definitions(${TEST_TARGET}
    PRIVATE
        PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

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

# gRPC tests (when enabled)
if(ENABLE_GRPC)
    set(GRPC_TEST_TARGET GrpcSmokeTests)

    add_executable(${GRPC_TEST_TARGET}
        GrpcSmokeTests.cpp
    )

    target_link_libraries(${GRPC_TEST_TARGET}
        PRIVATE
            JuceAudioService::JuceAudioService
            audio_engine_proto
            juce::juce_core
            juce::juce_audio_basics
            juce::juce_audio_formats
    )

    target_compile_features(${GRPC_TEST_TARGET} PRIVATE cxx_std_20)

    target_compile_definitions(${GRPC_TEST_TARGET}
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    add_test(NAME ${GRPC_TEST_TARGET} COMMAND ${GRPC_TEST_TARGET})
    set_tests_properties(${GRPC_TEST_TARGET} PROPERTIES LABELS "grpc")
endif()

