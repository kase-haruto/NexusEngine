#pragma once
#include "EditorCommandRegistry.h"
#include <algorithm>
#include <vector>

namespace NexusEngine {
enum class ViewportToolGlyph { Select, Translate, Rotate, Scale, Coordinate, Snap, View, Display, Other };
struct ViewportToolDescriptor {
    std::string id, labelKey, iconKey;
    std::string group; //< 同じGroupに複数Toolを登録できる。Group名は拡張側でも定義できる。
    ViewportToolGlyph glyph = ViewportToolGlyph::Other; //< PNG未配置時の図形アイコン
    int shortcutKey = 0;
    std::string shortcut;
    std::string variantOf; //< 将来の派生Tool用。空なら独立したTool。
};
/*-----------------------------------------------------------------------------------------
 * ViewportToolRegistry
 * - Viewport専用のTool記述と既存Commandの接続を保持する
 * - TransformやSceneの編集、ImGui描画は担当しない
 *---------------------------------------------------------------------------------------*/
class ViewportToolRegistry final {
public:
    /** \brief 安定ID順の登録を保持し、重複ID・Shortcutを拒否する */
    bool Register(ViewportToolDescriptor tool, EditorCommandRegistry::Command command) {
        if(tool.id.empty() || tool.labelKey.empty() || !command.execute) return false;
        for(const auto& other : tools_)
            if(other.id == tool.id || (tool.shortcutKey && other.shortcutKey == tool.shortcutKey)) return false;
        const auto id = tool.id;
        if(!commands_.Register(id, std::move(command))) return false;
        if(tool.shortcutKey) commands_.Bind(id, tool.shortcutKey, false, false, false, tool.shortcut);
        tools_.push_back(std::move(tool));
        return true;
    }
    const std::vector<ViewportToolDescriptor>& Tools() const noexcept { return tools_; }
    const EditorCommandRegistry& Commands() const noexcept { return commands_; }
private:
    std::vector<ViewportToolDescriptor> tools_;
    EditorCommandRegistry commands_; //< Global Shortcutと競合しないViewportフォーカス範囲でDispatchする
};
}
