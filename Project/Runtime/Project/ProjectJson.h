#pragma once
// Project実装専用。公開APIにJSONライブラリの型を含めない。
#include "ThirdParty/nlohmann/json.hpp"
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <string_view>

namespace NexusEngine::ProjectJson {
using Json = nlohmann::json;
[[nodiscard]] Result<Json> Read(const std::filesystem::path& file);
/** \brief DLLからコピーしたJSONにもファイル読込と同じ上限・深さ・重複キー検証を適用する */
[[nodiscard]] Result<Json> Parse(std::string_view text);
[[nodiscard]] Result<void> Write(const std::filesystem::path& file, const Json& json, bool replace = true);
void Keys(const Json& object, std::initializer_list<const char*> allowed);
std::string Text(const Json& object, const char* key, bool allowEmpty = false);
std::string Utf8(const std::filesystem::path& path);
std::filesystem::path Path(const std::string& value);
}
