#pragma once
#include "AssetCatalog.h"

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * ContentBrowserActions
 * - UIにOS APIを露出せず、範囲検証したフォルダ作成・Explorer起動を提供する
 * - Asset移動はUUIDを維持する既存AssetCatalog::Renameへ委譲する
 *---------------------------------------------------------------------------------------*/
class ContentBrowserActions final {
  public:
    static Result<void> CreateFolder(AssetCatalog &catalog, const std::filesystem::path &parent, std::string_view name);
    static Result<std::vector<std::filesystem::path>> ChooseImportFiles();
    static Result<void> ShowInExplorer(const std::filesystem::path &file);
};
} // namespace NexusEngine
