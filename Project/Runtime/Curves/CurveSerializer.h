#pragma once
#include "FloatCurve.h"
#include <filesystem>
#include <string>
#include <string_view>

namespace NexusEngine {
/** \brief .curve v1/v2を読み込む。用途やアタッチ先は保存しない。 */
Result<FloatCurve> LoadFloatCurve(const std::filesystem::path& file);
/** \brief 共通AtomicFileを用いて保存する。既存ファイルは成功時だけ置換する。 */
Result<void> SaveFloatCurve(const std::filesystem::path& file, const FloatCurve& curve);
/** \brief clipboard/IPCでもdiskと同じ検証・保存形式を使用する。 */
Result<FloatCurve> ParseFloatCurve(std::string_view text);
std::string SerializeFloatCurve(const FloatCurve& curve);
}
