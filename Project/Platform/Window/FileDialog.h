#pragma once
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <optional>
#include <Windows.h>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * FileDialog
 * - UIスレッドでWindowsのファイル・Directory選択を表示する
 * - COM Apartmentの開始と終了は呼び出し側が所有する
 *---------------------------------------------------------------------------------------*/
class FileDialog final {
public:
    /** \brief ProjectまたはEngine manifestを選ぶ。キャンセルは空の成功結果を返す */
    [[nodiscard]] static Result<std::optional<std::filesystem::path>> Open(HWND owner, bool engineManifest = false);
    /** \brief 既存Directoryを選ぶ。Projectの作成先はその下で別途検証する */
    [[nodiscard]] static Result<std::optional<std::filesystem::path>> Folder(HWND owner);
    /** \brief 明示的なProject復旧記録を選択する */
    [[nodiscard]] static Result<std::optional<std::filesystem::path>> Recovery(HWND owner);
};
}
