cmake_minimum_required(VERSION 3.20)

project(JuceAudioService VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(JUCE_AUDIO_SERVICE_ENABLE_TESTS "Build the JuceAudioService tests" ${PROJECT_IS_TOP_LEVEL})
option(USE_LOCAL_JUCE "Use JUCE from third_party/JUCE" ON)
option(ENABLE_GRPC "Build C++ gRPC server/client" OFF)

# Enforce Apple Silicon only
if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        message(FATAL_ERROR "This project only supports Apple Silicon (arm64). Set CMAKE_OSX_ARCHITECTURES=arm64")
    endif()
elseif(ENABLE_GRPC)
    message(FATAL_ERROR "gRPC support is only available on Apple Silicon macOS")
endif()

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

# gRPC setup when enabled
if(ENABLE_GRPC)
    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/third_party/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    endif()

    find_package(Protobuf REQUIRED)
    find_package(gRPC REQUIRED)

    # Proto generation
    set(PROTO_FILES
        ${CMAKE_SOURCE_DIR}/proto/audio_engine.proto
    )

    set(PROTO_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY ${PROTO_GENERATED_DIR})

    foreach(PROTO_FILE ${PROTO_FILES})
        get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
        set(PROTO_SRCS "${PROTO_GENERATED_DIR}/${PROTO_NAME}.pb.cc")
        set(PROTO_HDRS "${PROTO_GENERATED_DIR}/${PROTO_NAME}.pb.h")
        set(GRPC_SRCS "${PROTO_GENERATED_DIR}/${PROTO_NAME}.grpc.pb.cc")
        set(GRPC_HDRS "${PROTO_GENERATED_DIR}/${PROTO_NAME}.grpc.pb.h")

        add_custom_command(
            OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
            COMMAND protobuf::protoc
            ARGS --grpc_out ${PROTO_GENERATED_DIR}
                 --cpp_out ${PROTO_GENERATED_DIR}
                 -I ${CMAKE_SOURCE_DIR}/proto
                 --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
                 ${PROTO_FILE}
            DEPENDS ${PROTO_FILE} protobuf::protoc gRPC::grpc_cpp_plugin
            COMMENT "Generating protocol buffer and gRPC sources from ${PROTO_FILE}"
        )

        list(APPEND ALL_PROTO_SRCS ${PROTO_SRCS} ${GRPC_SRCS})
        list(APPEND ALL_PROTO_HDRS ${PROTO_HDRS} ${GRPC_HDRS})
    endforeach()

    # Create a library for generated protobuf code
    add_library(audio_engine_proto ${ALL_PROTO_SRCS} ${ALL_PROTO_HDRS})
    target_link_libraries(audio_engine_proto
        PUBLIC
            protobuf::libprotobuf
            gRPC::grpc++
    )
    target_include_directories(audio_engine_proto
        PUBLIC
            ${PROTO_GENERATED_DIR}
    )
endif()

add_library(JuceAudioService
    src/AudioService.cpp
    src/VoiceGenerator.cpp
    src/AudioFileSource.cpp
    src/OfflineRenderer.cpp
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

# gRPC targets
if(ENABLE_GRPC)
    # gRPC server executable
    add_executable(audio_engine_server
        src/grpc_server.cpp
    )
    target_link_libraries(audio_engine_server
        PRIVATE
            JuceAudioService
            audio_engine_proto
    )

    # Set output directory for server binary
    set_target_properties(audio_engine_server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    )

    # gRPC client CLI executable
    add_executable(grpc_client_cli
        src/grpc_client_cli.cpp
    )
    target_link_libraries(grpc_client_cli
        PRIVATE
            audio_engine_proto
    )

    # Set output directory for client binary (put in tools to match expectation)
    set_target_properties(grpc_client_cli PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/tools"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/tools"
    )
endif()

