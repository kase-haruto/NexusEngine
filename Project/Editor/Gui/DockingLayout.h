#pragma once
#include <filesystem>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * DockingLayout
 * - ImGui配置の初期構築と個人ディレクトリへの保存・復元を担当する
 * - 翻訳済み表示名でなく安定Window IDで配置する
 *---------------------------------------------------------------------------------------*/
class DockingLayout final {
public:
    /** \brief ImGui Context生成後、最初のNewFrameより前に復元する */
    void Initialize(const std::filesystem::path& userDirectory, std::string_view profile = "General");
    /** \brief 現在配置を保存し、名前付き配置へ切り替える */
    void SwitchProfile(std::string_view profile, bool saveCurrent);
    /** \brief DockSpaceを描き、必要なら初期配置を構築する */
    void Draw();
    void Reset() { reset_ = true; }
    /** \brief 終了時に呼ぶ。ImGui Context破棄前であること */
    void Save(bool enabled);
private:
    std::filesystem::path path_; //< OSネイティブ形式の保存先。UTF-8への再変換を避ける
    std::filesystem::path directory_; //< Project別の配置保存先
    bool reset_ = false; //< 次frameのlayout再構築要求
};
}
