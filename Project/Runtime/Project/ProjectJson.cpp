#include "ProjectJson.h"
#include "Platform/FileSystem/AtomicFile.h"
#include <fstream>
#include <set>

namespace NexusEngine::ProjectJson {
std::string Utf8(const std::filesystem::path& path) {
    auto value = path.generic_u8string(); return {reinterpret_cast<const char*>(value.data()), value.size()};
}
std::filesystem::path Path(const std::string& value) { return std::filesystem::path(reinterpret_cast<const char8_t*>(value.c_str())); }

////////////////////////////
// 外部JSONのサイズ・深さ・重複キーを検証する
////////////////////////////
Result<Json> Read(const std::filesystem::path& file) {
    try {
        std::ifstream input(file, std::ios::binary | std::ios::ate);
        if(!input) throw std::runtime_error("Cannot read file.");
        const auto size = input.tellg();
        if(size < 0 || size > 1024 * 1024) throw std::runtime_error("JSON file exceeds 1 MiB.");
        std::string text(static_cast<size_t>(size), '\0');
        input.seekg(0); input.read(text.data(), static_cast<std::streamsize>(text.size()));
        if(!input) throw std::runtime_error("Cannot read complete JSON file.");
        std::vector<std::set<std::string>> keys;
        const auto validate = [&](int depth, Json::parse_event_t event, Json& value) {
            if(depth > 32) throw std::runtime_error("JSON nesting exceeds 32 levels.");
            if(event == Json::parse_event_t::object_start) keys.emplace_back();
            if(event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second) throw std::runtime_error("Duplicate JSON key.");
            if(event == Json::parse_event_t::object_end) keys.pop_back();
            return true;
        };
        return Json::parse(text, validate);
    } catch(const std::exception& exception) {
        return std::unexpected(Error(ErrorCategory::FileSystem, 160, Utf8(file) + ": " + exception.what()));
    }
}

Result<void> Write(const std::filesystem::path& file, const Json& json, bool replace) {
    try {
        const auto text = json.dump(2) + "\n";
        if(text.size() > 1024 * 1024) throw std::runtime_error("JSON output exceeds 1 MiB.");
        return WriteFileAtomically(file, text, replace);
    }
    catch(const std::exception& exception) { return std::unexpected(Error(ErrorCategory::FileSystem, 160, Utf8(file) + ": " + exception.what())); }
}
void Keys(const Json& object, std::initializer_list<const char*> allowed) {
    if(!object.is_object()) throw std::runtime_error("Expected JSON object.");
    for(const auto& [key, value] : object.items()) {
        static_cast<void>(value);
        bool found = false;
        for(const char* candidate : allowed) found |= key == candidate;
        if(!found) throw std::runtime_error("Unsupported field: " + key);
    }
}
std::string Text(const Json& object, const char* key, bool allowEmpty) {
    auto value = object.at(key).get<std::string>();
    if((!allowEmpty && value.empty()) || value.size() > 4096 || value.find('\0') != value.npos || value.find('\n') != value.npos || value.find('\r') != value.npos)
        throw std::runtime_error(std::string("Invalid string: ") + key);
    return value;
}
}
