#pragma once
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <string>

namespace NexusEngine {
bool IsAssetIdentity(const std::string& reference);
/** \brief Editorの保存/選択でsidecarとroot内registryを発行する。Runtimeの読込では書き込まない。 */
Result<std::string> EnsureAssetIdentity(const std::filesystem::path& root, const std::filesystem::path& file);
Result<std::filesystem::path> ResolveAssetIdentity(const std::filesystem::path& root, const std::string& reference);
/** \brief 読込用の参照解決。旧pathの改名履歴も読み、保存先の解決には流用しない。 */
Result<std::filesystem::path> ResolveAssetReference(const std::filesystem::path& root, const std::string& reference);
/** \brief fileとsidecarを一緒に改名しregistryを更新する。既存の宛先は置換しない。 */
Result<void> RenameAssetWithIdentity(const std::filesystem::path& root, const std::filesystem::path& source,
    const std::filesystem::path& destination);
bool SameAssetReference(const std::filesystem::path& root, const std::string& first, const std::string& second);
}
