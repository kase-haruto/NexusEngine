#pragma once
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace NexusEngine {
/** Project共有の起動設定。Editor個人設定には依存しない。 */
struct ProjectSettings {
    std::string name = "NexusEngine";
    std::string startupLevel = "Resources/Assets/Levels/DemoLevel.json";
    std::string assetPath = "Resources/Assets";
};

/** \brief UTF-8の共有設定を読む。不正な既知項目と読込失敗を報告する */
[[nodiscard]] inline Result<ProjectSettings> LoadProjectSettings(const std::filesystem::path& root) {
    std::ifstream input(root / "Config/Project.settings");
    if(!input) return std::unexpected(Error(ErrorCategory::FileSystem, 44, "Cannot read Config/Project.settings."));
    ProjectSettings settings;
    std::string line;
    while(std::getline(input, line)) {
        std::istringstream row(line);
        std::string key, value;
        if(!(row >> key) || key.starts_with('#')) continue;
        if(key != "name" && key != "startupLevel" && key != "assetPath") continue;
        if(!(row >> std::quoted(value)) || value.empty())
            return std::unexpected(Error(ErrorCategory::FileSystem, 45, "Invalid project setting: " + key));
        if(key == "name") settings.name = value;
        if(key == "startupLevel") settings.startupLevel = value;
        if(key == "assetPath") settings.assetPath = value;
    }
    if(input.bad()) return std::unexpected(Error(ErrorCategory::FileSystem, 44, "Failed reading Config/Project.settings."));
    return settings;
}

/** \brief ソースツリーと配布先の両方からProjectルートを探索する */
[[nodiscard]] inline std::optional<std::filesystem::path> FindProjectRoot(std::filesystem::path directory) {
    std::error_code error;
    directory = std::filesystem::absolute(directory, error).lexically_normal();
    if(error) return std::nullopt;
    while(!directory.empty()) {
        for(const auto& candidate : {directory, directory / "Project"}) {
            if(std::filesystem::is_regular_file(candidate / "Config/Project.settings", error) &&
               std::filesystem::is_directory(candidate / "Resources/Assets", error) &&
               std::filesystem::is_directory(candidate / "Shaders", error)) return candidate;
        }
        const auto parent = directory.parent_path();
        if(parent == directory) break;
        directory = parent;
    }
    return std::nullopt;
}
}
