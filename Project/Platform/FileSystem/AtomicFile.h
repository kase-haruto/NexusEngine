#pragma once
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <functional>
#include <string_view>

namespace NexusEngine {
/** \brief UUIDを発行する。失敗時はOSエラーを返す */
[[nodiscard]] Result<std::string> CreateUuid();
/** \brief 同一Directoryの一時ファイルをflushしてから置換する。失敗時は旧ファイルを保持する */
[[nodiscard]] Result<void> WriteFileAtomically(const std::filesystem::path& file, std::string_view bytes, bool replace = true);
/** \brief 同一volume内の新規Directoryを公開する。既存の保存先は置換しない */
[[nodiscard]] Result<void> PublishDirectory(const std::filesystem::path& staging, const std::filesystem::path& destination);
/** \brief 同一パスへの複数プロセスからの更新を直列化する。待機上限は5秒 */
[[nodiscard]] Result<void> WithFileLock(const std::filesystem::path& file, const std::function<Result<void>()>& update);
/** \brief 区切りと大小文字差を吸収して比較する。symlinkの同一性は呼び出し側で解決する */
[[nodiscard]] bool SameFilePath(const std::filesystem::path& first, const std::filesystem::path& second);
/** \brief 配布済みEngineやビルド成果物の内容をSHA-256で識別する */
[[nodiscard]] Result<std::string> Sha256File(const std::filesystem::path& file);
}
