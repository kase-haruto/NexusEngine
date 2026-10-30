#pragma once
#include <filesystem>
#include <optional>
#include "Runtime/Application/ProjectSettings.h"

namespace NexusEngine {
// Resolve source-tree launches from Visual Studio, the repository, or the output folder.
[[nodiscard]] inline std::optional<std::filesystem::path> FindEditorProjectRoot(std::filesystem::path directory) {
    return FindProjectRoot(std::move(directory));
}
}
