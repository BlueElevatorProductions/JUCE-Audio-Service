cmake_minimum_required(VERSION 3.20)

project(JuceAudioService VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(JUCE_AUDIO_SERVICE_ENABLE_TESTS "Build the JuceAudioService tests" ${PROJECT_IS_TOP_LEVEL})
option(USE_LOCAL_JUCE "Use JUCE from third_party/JUCE" ON)

set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)

# Disable problematic modules for better macOS compatibility
set(JUCE_MODULES_ONLY ON CACHE BOOL "" FORCE)

# Use local JUCE from third_party/JUCE
if(USE_LOCAL_JUCE)
    set(_juce_source "${CMAKE_SOURCE_DIR}/third_party/JUCE")
    if(EXISTS "${_juce_source}/CMakeLists.txt")
        message(STATUS "Using local JUCE from ${_juce_source}")
        add_subdirectory("${_juce_source}" juce EXCLUDE_FROM_ALL)
    else()
        message(FATAL_ERROR "JUCE not found at ${_juce_source}. Run scripts/get_deps.sh or initialize submodules with: git submodule update --init --recursive")
    endif()
else()
    # Fallback to user-provided JUCE_SOURCE_DIR
    set(_juce_source "")
    if(DEFINED JUCE_SOURCE_DIR)
        set(_juce_source "${JUCE_SOURCE_DIR}")
    elseif(DEFINED ENV{JUCE_SOURCE_DIR})
        set(_juce_source "$ENV{JUCE_SOURCE_DIR}")
    endif()

    if(_juce_source)
        if(NOT IS_ABSOLUTE "${_juce_source}")
            get_filename_component(_juce_source "${_juce_source}" REALPATH BASE_DIR "${CMAKE_SOURCE_DIR}")
        endif()
        message(STATUS "Using external JUCE from ${_juce_source}")
        add_subdirectory("${_juce_source}" juce)
    else()
        message(FATAL_ERROR "JUCE not found. Either enable USE_LOCAL_JUCE or set JUCE_SOURCE_DIR")
    endif()
endif()

add_library(JuceAudioService
    src/AudioService.cpp
)

add_library(JuceAudioService::JuceAudioService ALIAS JuceAudioService)

target_include_directories(JuceAudioService
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(JuceAudioService
    PUBLIC
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_formats
)

target_compile_features(JuceAudioService PUBLIC cxx_std_20)

target_compile_definitions(JuceAudioService
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

add_subdirectory(tools)

if(JUCE_AUDIO_SERVICE_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

