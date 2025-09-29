#pragma once

#include <string>
#include "audio_engine.pb.h"

namespace juceaudioservice {

/**
 * Utility for converting between EDL protobuf messages and JSON.
 *
 * Provides conversion functions for EDL data interchange
 * using Google's protobuf JSON utility.
 */
class EdlJson {
public:
    /**
     * Parse EDL from JSON string.
     *
     * @param jsonString JSON representation of EDL
     * @param edl Output parameter for parsed EDL
     * @param error Output parameter for parse error message
     * @return true if parsing succeeded
     */
    static bool parseFromJson(const std::string& jsonString, audio_engine::Edl& edl, std::string& error);

    /**
     * Convert EDL to JSON string.
     *
     * @param edl EDL message to convert
     * @param jsonString Output parameter for JSON string
     * @param error Output parameter for conversion error message
     * @return true if conversion succeeded
     */
    static bool toJson(const audio_engine::Edl& edl, std::string& jsonString, std::string& error);

    /**
     * Convert EngineEvent to JSON string for streaming.
     *
     * @param event EngineEvent message to convert
     * @param jsonString Output parameter for JSON string
     * @param error Output parameter for conversion error message
     * @return true if conversion succeeded
     */
    static bool eventToJson(const audio_engine::EngineEvent& event, std::string& jsonString, std::string& error);

    /**
     * Read JSON from file or stdin.
     *
     * @param path File path, or "-" for stdin
     * @param jsonString Output parameter for JSON content
     * @param error Output parameter for read error message
     * @return true if reading succeeded
     */
    static bool readJsonFromFile(const std::string& path, std::string& jsonString, std::string& error);
};

} // namespace juceaudioservice