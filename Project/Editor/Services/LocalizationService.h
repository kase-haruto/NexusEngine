#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * LocalizationService
 * - UTF-8外部辞書とEnglish fallbackを管理する
 * - 戻り値は所有string。言語切替でImGuiが参照する文字列を無効化しない
 *---------------------------------------------------------------------------------------*/
class LocalizationService final {
public:
    /** \brief 起動時に英日辞書を読み込む */
    [[nodiscard]] bool Load(const std::filesystem::path& directory);
    /** \brief 辞書を再生成せず表示言語のみ切り替える */
    void SetLanguage(std::string_view language) { language_ = language == "ja" ? "ja" : "en"; }
    /** \brief 選択言語→英語→キーの順で解決する */
    [[nodiscard]] std::string Text(std::string_view key) const;
    /** \brief 表示文字列と言語非依存のImGui IDを結合する */
    [[nodiscard]] std::string Label(std::string_view key, std::string_view id) const;
private:
    std::map<std::string, std::string, std::less<>> english_, japanese_; //< UTF-8辞書
    std::string language_ = "en"; //< 選択言語
};
}
