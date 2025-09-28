cmake_minimum_required(VERSION 3.20)

# render_cli tool
add_executable(render_cli
    render_cli.cpp
)

target_link_libraries(render_cli
    PRIVATE
        JuceAudioService::JuceAudioService
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_formats
)

target_compile_features(render_cli PRIVATE cxx_std_20)

target_compile_definitions(render_cli
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

# make_fixture_cli tool
add_executable(make_fixture_cli
    make_fixture_cli.cpp
)

target_link_libraries(make_fixture_cli
    PRIVATE
        JuceAudioService::JuceAudioService
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_formats
)

target_compile_features(make_fixture_cli PRIVATE cxx_std_20)

target_compile_definitions(make_fixture_cli
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

# Set output directory for tools
set_target_properties(render_cli make_fixture_cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
)