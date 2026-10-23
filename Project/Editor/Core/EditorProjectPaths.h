#pragma once
#include <filesystem>
#include <optional>

namespace NexusEngine {
// Resolve source-tree launches from Visual Studio, the repository, or the output folder.
[[nodiscard]] inline std::optional<std::filesystem::path> FindEditorProjectRoot(std::filesystem::path directory) {
    std::error_code error;
    directory = std::filesystem::absolute(directory, error);
    if(error) return std::nullopt;
    directory = directory.lexically_normal();
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
