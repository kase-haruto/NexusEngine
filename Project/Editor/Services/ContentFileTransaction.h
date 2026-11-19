#pragma once
#include "AssetCatalog.h"
#include "EditorHistory.h"
#include <span>

namespace NexusEngine {
enum class ContentFileOperation { Duplicate, Move, Archive };
/*-----------------------------------------------------------------------------------------
 * ContentFileTransaction
 * - Assets内の複数fileを事前検証し、UUIDを保つ移動と取り消しを共通履歴へ登録する
 * - 削除は非表示archiveへの退避。物理削除・Source moduleの移行・外部folder操作はしない
 *---------------------------------------------------------------------------------------*/
class ContentFileTransaction final {
  public:
    static Result<void> Rename(AssetCatalog &catalog, const std::filesystem::path &source, std::string_view relative,
                               EditorHistory &history, std::shared_ptr<size_t> revision);
    static Result<void> Execute(AssetCatalog &catalog, std::span<const std::filesystem::path> files,
                                ContentFileOperation operation, const std::filesystem::path &destination,
                                EditorHistory &history, std::shared_ptr<size_t> revision);
};
} // namespace NexusEngine
