#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include "ToolbarLayout.h"
#include "Runtime/Application/ProjectSettings.h"

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * EditorSettingsStore
 * - User / Editor / Project設定の保存先と永続化を管理する
 * - UI、Runtime World、ImGuiの状態は所有しない
 *---------------------------------------------------------------------------------------*/
class EditorSettingsStore final {
public:
    struct User {
        std::string language = "en"; //< 翻訳言語
        std::string theme = "Dark"; //< UI配色
        float fontSize = 18; //< 論理フォントサイズ
        float uiScale = 1; //< UI倍率
        bool autoSaveLayout = true; //< 終了時に配置を保存する
        std::string workspace = "General"; //< 個人の名前付き配置
        std::map<std::string, bool> panels; //< 安定Panel IDごとの表示状態
        std::vector<std::string> recentLevels; //< 個人の最近使ったLevel、最大10件
        // セクションの安定IDをキーとして、個人用のツールバー配置を保存する。
        ToolbarOffsets toolbarOffsets;
    } user;
    struct Editor {
        std::string giMode = "Off"; //< Off / DDGI / RTGI / Baked
        bool grid = true; //< Grid表示
        bool gizmo = true; //< トランスフォームGizmo表示
        bool viewport = true; //< Viewport初期表示
    } editor;
    using Project = ProjectSettings;
    Project project;
    /** \brief Project→Editor→Userの順で既定値へ上書きする */
    void Load(const std::filesystem::path& projectRoot);
    /** \brief 個人設定のみ保存する。失敗時は旧ファイルを保持する */
    [[nodiscard]] bool SaveUser() const;
    /** \brief プロジェクト共有設定を明示的に保存する */
    [[nodiscard]] bool SaveProject() const;
    /** \brief エディタ動作設定を明示的に保存する */
    [[nodiscard]] bool SaveEditor() const;
    [[nodiscard]] const std::filesystem::path& UserDirectory() const { return userDirectory_; }
private:
    std::filesystem::path userDirectory_; //< LOCALAPPDATA/NexusEngine/Editor
    std::filesystem::path projectDirectory_; //< ProjectRoot/Config
};
}
