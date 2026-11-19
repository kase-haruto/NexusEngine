#include "EditorIcons.h"
#include "Editor/Gui/Widgets/EditorIconButton.h"
#include <algorithm>
#include <array>
#include <string>
#include <map>

namespace NexusEngine::UI {
namespace {
std::map<ImGuiContext *, std::map<std::string, bool>> monochrome;
const EditorIconDefinition definitions[] = {{"back", "back.png", "", "edit.undo"},
                                            {"forward", "forward.png", "", "edit.redo"},
                                            {"folder_up", "folder_up.png", "", "asset.folderOpen"},
                                            {"add", "add.png", "", ""},
                                            {"import", "import.png", "", ""},
                                            {"save", "save.png", "file.save", ""},
                                            {"save_all", "save_all.png", "", "file.save"},
                                            {"refresh", "refresh.png", "", "project.reloadCpp"},
                                            {"filter", "filter.png", "", ""},
                                            {"search", "search.png", "", ""},
                                            {"settings", "settings.png", "", "viewport.show"},
                                            {"view_grid", "view_grid.png", "", ""},
                                            {"view_list", "view_list.png", "", ""},
                                            {"view_details", "view_details.png", "", ""},
                                            {"sort_ascending", "sort_ascending.png", "", ""},
                                            {"sort_descending", "sort_descending.png", "", ""},
                                            {"folder", "folder.png", "asset.folder", ""},
                                            {"folder_open", "folder_open.png", "asset.folderOpen", ""},
                                            {"favorite", "favorite.png", "", ""},
                                            {"collection", "collection.png", "", ""},
                                            {"project", "project.png", "", ""},
                                            {"content", "content.png", "", "asset.folder"},
                                            {"source", "source.png", "", "component.Script"},
                                            {"plugin", "plugin.png", "", ""},
                                            {"file", "file.png", "", ""},
                                            {"asset_model", "asset_model.png", "asset.model", ""},
                                            {"asset_texture", "asset_texture.png", "asset.texture", ""},
                                            {"asset_material", "asset_material.png", "", "details.Material"},
                                            {"asset_level", "asset_level.png", "", ""},
                                            {"asset_prefab", "asset_prefab.png", "", ""},
                                            {"asset_animation", "asset_animation.png", "", ""},
                                            {"asset_curve", "asset_curve.png", "", ""},
                                            {"asset_audio", "asset_audio.png", "", ""},
                                            {"asset_shader", "asset_shader.png", "", ""},
                                            {"source_cpp", "source_cpp.png", "", "component.Script"},
                                            {"source_header", "source_header.png", "", "component.Script"},
                                            {"source_csharp", "source_csharp.png", "", "component.Script"},
                                            {"open", "open.png", "", ""},
                                            {"rename", "rename.png", "", ""},
                                            {"duplicate", "duplicate.png", "", "copy"},
                                            {"delete", "delete.png", "", ""},
                                            {"copy", "copy.png", "", ""},
                                            {"move", "move.png", "", ""},
                                            {"copy_path", "copy_path.png", "", ""},
                                            {"explorer", "explorer.png", "", "asset.folderOpen"},
                                            {"code_editor", "code_editor.png", "", "component.Script"},
                                            {"references", "references.png", "", ""},
                                            {"dependencies", "dependencies.png", "", ""},
                                            {"reimport", "reimport.png", "", "project.reloadCpp"},
                                            {"dirty", "dirty.png", "", ""},
                                            {"warning", "warning.png", "", ""},
                                            {"error", "error.png", "", ""},
                                            {"loading", "loading.png", "", ""},
                                            {"missing", "missing.png", "", ""},
                                            {"lock", "lock.png", "", ""},
                                            {"info", "info.png", "", ""}};
const char *Symbol(std::string_view id) {
    if (id == "back") {
        return "<";
    }
    if (id == "forward") {
        return ">";
    }
    if (id == "folder_up") {
        return "^";
    }
    if (id == "add") {
        return "+";
    }
    if (id == "save" || id == "save_all") {
        return "S";
    }
    if (id == "refresh" || id == "reimport") {
        return "R";
    }
    if (id == "filter") {
        return "F";
    }
    if (id == "search") {
        return "?";
    }
    if (id == "settings") {
        return "...";
    }
    if (id == "view_grid") {
        return "#";
    }
    if (id == "view_list" || id == "view_details") {
        return "=";
    }
    if (id == "sort_ascending") {
        return "A^";
    }
    if (id == "sort_descending") {
        return "Av";
    }
    if (id == "favorite") {
        return "*";
    }
    if (id == "delete" || id == "error" || id == "missing") {
        return "X";
    }
    if (id == "warning" || id == "dirty") {
        return "!";
    }
    if (id == "source_cpp") {
        return "C+";
    }
    if (id == "source_header") {
        return "H";
    }
    if (id == "source_csharp") {
        return "C#";
    }
    if (id == "asset_animation") {
        return ">";
    }
    if (id == "asset_audio") {
        return "~";
    }
    if (id == "asset_curve") {
        return "/";
    }
    if (id == "collection") {
        return "{}";
    }
    return "[]";
}
} // namespace
void SetEditorIconMonochrome(std::string_view key, bool enabled) {
    monochrome[ImGui::GetCurrentContext()][std::string(key)] = enabled;
}
void ClearEditorIconTheme() {
    monochrome.erase(ImGui::GetCurrentContext());
}
std::span<const EditorIconDefinition> EditorIconDefinitions() {
    return definitions;
}
const ImTextureRef *ResolveEditorIcon(std::string_view id) {
    const auto found = std::ranges::find(definitions, id, &EditorIconDefinition::id);
    if (found != std::end(definitions) && !found->registered.empty()) {
        if (const auto *texture = FindEditorIcon(std::string(found->registered))) {
            return texture;
        }
    }
    const auto imageKey = found == std::end(definitions) ? id : found->file.substr(0, found->file.size() - 4);
    if (const auto *texture = FindEditorIcon(std::string(imageKey))) {
        return texture;
    }
    if (found != std::end(definitions) && !found->substitute.empty()) {
        return FindEditorIcon(std::string(found->substitute));
    }
    return FindEditorIcon("file");
}
void DrawEditorIcon(std::string_view id, ImVec2 position, float side, bool selected) {
    const auto color = ImGui::GetColorU32(selected ? ImGuiCol_CheckMark : ImGuiCol_Text);
    auto *draw = ImGui::GetWindowDrawList();
    if (const auto *image = ResolveEditorIcon(id)) {
        draw->AddImage(*image, position, {position.x + side, position.y + side}, {0, 0}, {1, 1}, [&] {
            const auto theme = monochrome.find(ImGui::GetCurrentContext());
            if (theme != monochrome.end()) {
                for (const auto &[key, tinted] : theme->second) {
                    if (tinted && FindEditorIcon(key) == image) {
                        return color;
                    }
                }
            }
            return ImGui::GetColorU32(ImVec4{1, 1, 1, 1});
        }());
        return;
    }
    // 画像未配置でもサイズと操作領域は変えず、文字記号をテーマ色で描く。
    // GPU資源を増やさず、画像の追加は次回起動時の既存loaderだけで反映する。
    draw->AddRect(position, {position.x + side, position.y + side}, color, side * .15f);
    const auto *symbol = Symbol(id);
    const auto size = ImGui::CalcTextSize(symbol);
    const float scale = std::min(1.f, side * .85f / std::max(1.f, size.x));
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale,
                  {position.x + (side - size.x * scale) * .5f, position.y + (side - size.y * scale) * .5f}, color,
                  symbol);
}
void EditorIcon(std::string_view id, float side) {
    if (side <= 0) {
        side = ImGui::GetFontSize();
    }
    const auto position = ImGui::GetCursorScreenPos();
    ImGui::Dummy({side, side});
    DrawEditorIcon(id, position, side);
}
bool IconButton(std::string_view id, const char *label, bool enabled, bool selected) {
    ImGui::BeginDisabled(!enabled);
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
    }
    const float side = ImGui::GetFontSize();
    const auto start = ImGui::GetCursorScreenPos();
    const auto padding = ImGui::GetStyle().FramePadding;
    // IDを画像名や翻訳から独立させ、画像の有無でクリック・focusを変えない。
    const bool pressed =
        ImGui::Button(("##" + std::string(label)).c_str(), {side + padding.x * 2, side + padding.y * 2});
    DrawEditorIcon(id, {start.x + padding.x, start.y + padding.y}, side, selected);
    if (selected) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        const auto title = std::string_view(label).substr(0, std::string_view(label).find("##"));
        ImGui::SetTooltip("%.*s", static_cast<int>(title.size()), title.data());
    }
    ImGui::EndDisabled();
    return pressed;
}
bool IconMenuItem(std::string_view id, const char *label, bool selected, bool enabled) {
    const auto start = ImGui::GetCursorScreenPos();
    const float side = ImGui::GetFontSize();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + side + ImGui::GetStyle().ItemInnerSpacing.x);
    const bool pressed = ImGui::MenuItem(label, nullptr, selected, enabled);
    ImGui::BeginDisabled(!enabled);
    DrawEditorIcon(id, {start.x, ImGui::GetItemRectMin().y + (ImGui::GetItemRectSize().y - side) * .5f}, side,
                   selected);
    ImGui::EndDisabled();
    return pressed;
}
} // namespace NexusEngine::UI
