#include "ContentDragPayload.h"
#include "AssetCatalog.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "ThirdParty/nlohmann/json.hpp"
#include <cstring>
#include <set>

namespace NexusEngine {
namespace {
Error Invalid() {
    return Error(ErrorCategory::Resource, 301, "Invalid Content drag payload (maximum 256 paths / 1 MiB). ");
}
} // namespace
Result<std::string> ContentDragPayload::Encode(std::span<const std::filesystem::path> files) {
    if (files.empty() || files.size() > 256) {
        return std::unexpected(Invalid());
    }
    auto paths = nlohmann::json::array();
    for (const auto &file : files) {
        const auto path = AssetCatalog::Utf8(file);
        if (!file.is_absolute() || path.size() > 32767 || path.find('\0') != std::string::npos) {
            return std::unexpected(Invalid());
        }
        paths.push_back(path);
    }
    auto bytes = nlohmann::json{{"version", 1}, {"paths", std::move(paths)}}.dump();
    if (bytes.size() > 1024 * 1024) {
        return std::unexpected(Invalid());
    }
    return bytes;
}
Result<std::vector<std::filesystem::path>> ContentDragPayload::Decode(const void *data, size_t bytes) {
    if (!data || !bytes || bytes > 1024 * 1024) {
        return std::unexpected(Invalid());
    }
    try {
        const auto json =
            nlohmann::json::parse(static_cast<const char *>(data), static_cast<const char *>(data) + bytes);
        const auto &paths = json.at("paths");
        if (json.at("version") != 1 || !paths.is_array() || paths.empty() || paths.size() > 256) {
            return std::unexpected(Invalid());
        }
        std::vector<std::filesystem::path> result;
        std::set<std::filesystem::path> unique;
        for (const auto &value : paths) {
            const auto path = value.get<std::string>();
            if (path.empty() || path.size() > 32767 || path.find('\0') != std::string::npos) {
                return std::unexpected(Invalid());
            }
            std::filesystem::path file(ConvertString(path));
            if (!file.is_absolute() || !unique.insert(file.lexically_normal()).second) {
                return std::unexpected(Invalid());
            }
            result.push_back(std::move(file));
        }
        return result;
    } catch (...) {
        return std::unexpected(Invalid());
    }
}
Result<std::vector<std::filesystem::path>> ContentDragPayload::Legacy(const void *data, size_t bytes) {
    if (!data || bytes < 2 || bytes > 32768 || static_cast<const char *>(data)[bytes - 1] != '\0' ||
        std::memchr(data, '\0', bytes - 1)) {
        return std::unexpected(Invalid());
    }
    std::filesystem::path file(ConvertString(std::string(static_cast<const char *>(data), bytes - 1)));
    if (!file.is_absolute()) {
        return std::unexpected(Invalid());
    }
    return std::vector<std::filesystem::path>{std::move(file)};
}
} // namespace NexusEngine
