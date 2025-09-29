#include "EdlJson.h"
#include <google/protobuf/util/json_util.h>
#include <iostream>
#include <fstream>
#include <sstream>

namespace juceaudioservice {

bool EdlJson::parseFromJson(const std::string& jsonString, audio_engine::Edl& edl, std::string& error) {
    google::protobuf::util::JsonParseOptions options;
    options.ignore_unknown_fields = false;
    options.case_insensitive_enum_parsing = true;

    auto status = google::protobuf::util::JsonStringToMessage(jsonString, &edl, options);
    if (!status.ok()) {
        error = "JSON parse error: " + std::string(status.message());
        return false;
    }

    return true;
}

bool EdlJson::toJson(const audio_engine::Edl& edl, std::string& jsonString, std::string& error) {
    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = true;
    options.preserve_proto_field_names = true;
    // Note: always_print_primitive_fields removed in newer protobuf versions

    auto status = google::protobuf::util::MessageToJsonString(edl, &jsonString, options);
    if (!status.ok()) {
        error = "JSON conversion error: " + std::string(status.message());
        return false;
    }

    return true;
}

bool EdlJson::eventToJson(const audio_engine::EngineEvent& event, std::string& jsonString, std::string& error) {
    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = false; // Compact format for streaming
    options.preserve_proto_field_names = true;
    // Note: always_print_primitive_fields removed in newer protobuf versions

    auto status = google::protobuf::util::MessageToJsonString(event, &jsonString, options);
    if (!status.ok()) {
        error = "Event JSON conversion error: " + std::string(status.message());
        return false;
    }

    return true;
}

bool EdlJson::readJsonFromFile(const std::string& path, std::string& jsonString, std::string& error) {
    if (path == "-") {
        // Read from stdin
        std::ostringstream ss;
        std::string line;
        while (std::getline(std::cin, line)) {
            ss << line << "\n";
        }
        jsonString = ss.str();
        return true;
    }

    // Read from file
    std::ifstream file(path);
    if (!file.is_open()) {
        error = "Cannot open file: " + path;
        return false;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    jsonString = ss.str();

    if (file.bad()) {
        error = "Error reading file: " + path;
        return false;
    }

    return true;
}

} // namespace juceaudioservice