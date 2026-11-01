#pragma once
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <optional>
#include <memory>

namespace NexusEngine {
class ProjectContext;
/*-----------------------------------------------------------------------------------------
 * ApplicationPaths
 * - ゲームとエンジンに属する解決済みのルートを保持する
 * - 設定読込、UI、サービスの所有は担当しない
 *---------------------------------------------------------------------------------------*/
struct ApplicationPaths {
    std::filesystem::path projectRoot; //< Game設定・Level内の互換参照の基準
    std::filesystem::path engineRoot; //< ShaderとEditor内蔵資源の基準
    std::shared_ptr<const ProjectContext> project; //< nxproject起動時だけ保持する不変snapshot
};

/**
 * \brief Root内の参照を正規化し、junction等による逸脱も拒否する
 * \param root 明示的に解決済みの絶対ルート
 * \param reference 絶対またはRoot相対の参照
 * \return 存在しない保存先も含めた解決結果
 */
[[nodiscard]] inline Result<std::filesystem::path> ResolveProjectPath(
    const std::filesystem::path& root, const std::filesystem::path& reference) {
    std::error_code error;
    if(root.empty() || !root.is_absolute() || reference.empty() ||
       (!reference.is_absolute() && reference.has_root_name()))
        return std::unexpected(Error(ErrorCategory::FileSystem, 150, "Invalid project root or path reference."));
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, error);
    if(error) return std::unexpected(Error(ErrorCategory::FileSystem, 150, "Cannot resolve project root: " + error.message()));
    const auto resolved = std::filesystem::weakly_canonical(reference.is_absolute() ? reference : canonicalRoot / reference, error);
    const auto relative = resolved.lexically_relative(canonicalRoot);
    if(error || relative.empty() || relative.is_absolute() || *relative.begin() == "..")
        return std::unexpected(Error(ErrorCategory::FileSystem, 151, "Path escapes the selected project root."));
    return resolved;
}

/** \brief 実行ファイル付近からEngineのデータRootを探索する。Game Rootとは独立して扱う */
[[nodiscard]] inline std::optional<std::filesystem::path> FindEngineRoot(std::filesystem::path start) {
    std::error_code error;
    start = std::filesystem::absolute(start, error);
    if(error) return {};
    while(!start.empty()) {
        for(const auto& candidate : {start, start / "Project", start / "Data"}) {
            if(std::filesystem::is_directory(candidate / "Shaders", error)) return candidate;
        }
        const auto parent = start.parent_path();
        if(parent == start) break;
        start = parent;
    }
    return {};
}
}
