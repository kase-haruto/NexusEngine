#pragma once
#include "ThirdParty/imgui/imgui.h"
#include <span>
#include <string_view>

namespace NexusEngine::UI {
/*-----------------------------------------------------------------------------------------
 * EditorIconDefinition
 * - 論理IDと起動時に登録される画像名の対応。GPU資源はImGuiRendererが所有する
 * - 暫定画像の参照は共有し、Panelごとのdecode / uploadを行わない
 *---------------------------------------------------------------------------------------*/
struct EditorIconDefinition {
    std::string_view id, file, registered, substitute;
};
void SetEditorIconMonochrome(std::string_view key, bool enabled);
void ClearEditorIconTheme();
std::span<const EditorIconDefinition> EditorIconDefinitions();
const ImTextureRef *ResolveEditorIcon(std::string_view id);
void DrawEditorIcon(std::string_view id, ImVec2 position, float side, bool selected = false);
void EditorIcon(std::string_view id, float side = 0);
bool IconButton(std::string_view id, const char *label, bool enabled = true, bool selected = false);
bool IconMenuItem(std::string_view id, const char *label, bool selected = false, bool enabled = true);
} // namespace NexusEngine::UI
