#pragma once
#include "Runtime/Reflection/TypeRegistry.h"
#include <filesystem>
#include <string>
#include <vector>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * SourceFileActions
 * - Content Browserのソースと共通Reflectionの明示的な宣言位置を照合する
 * - Componentの所有・生成は既存SceneEditServiceへ委譲し、ファイル名から実行型を推測しない
 *---------------------------------------------------------------------------------------*/
class SourceFileActions final {
public:
    /** \brief Browserがソースとして扱う拡張子か調べる */
    [[nodiscard]] static bool IsSource(const std::filesystem::path& file);
    /** \brief Root内の実在するソースか検査する。外部symlinkや古いProjectのパスを拒否する */
    [[nodiscard]] static bool Contains(const std::filesystem::path& root, const std::filesystem::path& file);
    /** \brief 現在のRegistryからアタッチ可能な型IDを値で返す。Reloadを跨ぐDescriptor参照を保存しない */
    [[nodiscard]] static std::vector<std::string> TypesFor(const std::filesystem::path& root,
        const std::filesystem::path& file, const TypeRegistry& registry);
};
}
