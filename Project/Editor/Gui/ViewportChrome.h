#pragma once
#include "Editor/Services/ViewportToolRegistry.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/ImGui/ImGuiStyle.h"
#include "Editor/Gui/Widgets/EditorIconButton.h"
#include "ThirdParty/imgui/imgui_internal.h"
#include <algorithm>
#include <cmath>

namespace NexusEngine {
struct ViewportUiRect {
    ImVec2 position{}, size{};
    bool Contains(ImVec2 point) const {
        return point.x >= position.x && point.y >= position.y && point.x < position.x + size.x && point.y < position.y + size.y;
    }
};
/** \brief PNG未配置時も文字幅でShelfを広げず、Themeの文字色で図形を描く */
inline void DrawViewportGlyph(ImDrawList& draw, ImVec2 p, float side, ViewportToolGlyph glyph) {
    const auto color = ImGui::GetColorU32(ImGuiCol_Text);
    const ImVec2 c{p.x + side * .5f, p.y + side * .5f};
    const float r = side * .27f;
    if(glyph == ViewportToolGlyph::Select) {
        draw.AddTriangle({c.x-r,c.y-r}, {c.x+r,c.y}, {c.x,c.y+r}, color, 1.5f);
        draw.AddLine(c, {c.x+r,c.y+r}, color, 1.5f);
    } else if(glyph == ViewportToolGlyph::Translate) {
        draw.AddLine({c.x-r,c.y}, {c.x+r,c.y}, color, 1.5f);
        draw.AddLine({c.x,c.y-r}, {c.x,c.y+r}, color, 1.5f);
        for(float sign : {-1.f, 1.f}) {
            draw.AddTriangleFilled({c.x+sign*r,c.y}, {c.x+sign*r*.5f,c.y-r*.3f}, {c.x+sign*r*.5f,c.y+r*.3f}, color);
            draw.AddTriangleFilled({c.x,c.y+sign*r}, {c.x-r*.3f,c.y+sign*r*.5f}, {c.x+r*.3f,c.y+sign*r*.5f}, color);
        }
    } else if(glyph == ViewportToolGlyph::Rotate) {
        draw.PathArcTo(c, r, .4f, 5.6f, 24); draw.PathStroke(color, 0, 1.5f);
        draw.AddTriangleFilled({c.x+r,c.y-r*.8f}, {c.x+r,c.y+r*.05f}, {c.x+r*.2f,c.y-r*.5f}, color);
    } else if(glyph == ViewportToolGlyph::Scale) {
        draw.AddRect({c.x-r,c.y+r*.1f}, {c.x-r*.1f,c.y+r}, color);
        draw.AddLine({c.x-r*.2f,c.y+r*.2f}, {c.x+r,c.y-r}, color, 1.5f);
        draw.AddRect({c.x+r*.4f,c.y-r}, {c.x+r,c.y-r*.4f}, color);
    } else if(glyph == ViewportToolGlyph::Coordinate) {
        draw.AddLine({c.x-r,c.y+r},{c.x+r,c.y+r},color,1.5f);
        draw.AddLine({c.x-r,c.y+r},{c.x-r,c.y-r},color,1.5f);
        draw.AddLine({c.x-r,c.y+r},{c.x+r*.5f,c.y-r*.5f},color,1.5f);
        draw.AddCircleFilled({c.x+r,c.y+r},2,color);
    } else if(glyph == ViewportToolGlyph::Snap) {
        draw.PathArcTo(c,r,0,3.14159f,16); draw.PathStroke(color,ImDrawFlags_None,2.0f);
        draw.AddLine({c.x-r,c.y},{c.x-r,c.y-r},color,2);
        draw.AddLine({c.x+r,c.y},{c.x+r,c.y-r},color,2);
        draw.AddLine({c.x-r-r*.2f,c.y-r*.5f},{c.x-r+r*.2f,c.y-r*.5f},color,1);
        draw.AddLine({c.x+r-r*.2f,c.y-r*.5f},{c.x+r+r*.2f,c.y-r*.5f},color,1);
    } else if(glyph == ViewportToolGlyph::View) {
        draw.AddRect({c.x-r,c.y-r*.6f},{c.x+r*.5f,c.y+r*.6f},color,2,0,1.5f);
        draw.AddTriangle({c.x+r*.5f,c.y},{c.x+r*1.2f,c.y-r*.7f},{c.x+r*1.2f,c.y+r*.7f},color,1.5f);
    } else if(glyph == ViewportToolGlyph::Display) {
        draw.AddEllipse(c,{r,r*.65f},color,0,24,1.5f); draw.AddCircleFilled(c,r*.25f,color);
    } else draw.AddCircle(c, r*.7f, color, 16, 1.5f);
}
/** \brief 同じItem IDと固定サイズのまま、PNGまたは図形アイコンを描画する */
inline bool ViewportGlyphButton(const std::string& icon, const char* id, ImVec2 size, ViewportToolGlyph glyph,
                                const char* expandedLabel = nullptr, bool active = false) {
    const auto p = ImGui::GetCursorScreenPos();
    // 状態色はエンジンのAccentへ追従し、Hover/押下中も選択状態が分かるようにする。
    if(active) {
        const auto accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
        const auto base = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        const auto blend = [&](float strength) { return ImVec4(base.x+(accent.x-base.x)*strength,
            base.y+(accent.y-base.y)*strength,base.z+(accent.z-base.z)*strength,1); };
        ImGui::PushStyleColor(ImGuiCol_Button,blend(.65f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,blend(.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,blend(.95f));
    }
    const bool clicked = ImGui::Button(id, size);
    if(active) {
        ImGui::PopStyleColor(3);
        ImGui::GetWindowDrawList()->AddRect(p,{p.x+size.x,p.y+size.y},ImGui::GetColorU32(ImGuiCol_CheckMark),
            ImGui::GetStyle().FrameRounding,0,std::max(1.f,ImGui::GetFontSize()/18*1.5f));
    }
    const float side = size.y;
    const auto* texture = UI::FindEditorIcon(icon + (active ? ".active" : ".inactive"));
    if(!texture) texture = UI::FindEditorIcon(icon);
    if(texture) {
        const float inset = side * .5f * (1-.6f*UI::CurrentEditorTheme().viewportTools.iconScale);
        ImGui::GetWindowDrawList()->AddImage(*texture, {p.x+inset,p.y+inset}, {p.x+side-inset,p.y+side-inset},
            {0,0}, {1,1}, ImGui::GetColorU32(ImVec4{1,1,1,1}));
    } else {
        const float iconSide = side*UI::CurrentEditorTheme().viewportTools.iconScale;
        const float inset = (side-iconSide)*.5f;
        DrawViewportGlyph(*ImGui::GetWindowDrawList(),{p.x+inset,p.y+inset},iconSide,glyph);
    }
    if(expandedLabel) ImGui::GetWindowDrawList()->AddText({p.x+side+3,p.y+(side-ImGui::GetFontSize())*.5f},
        ImGui::GetColorU32(ImGuiCol_Text), expandedLabel);
    return clicked;
}
/** \brief アイコンから分離した矢印で詳細Popupを開閉する。Popup表示中の同じ矢印も閉じ操作として受け取る */
inline void ViewportDetailArrow(const char* id,const char* popup,float height) {
    const bool open = ImGui::IsPopupOpen(popup);
    const float width = UI::CurrentEditorTheme().viewportTools.detailArrowWidth*ImGui::GetFontSize()/18;
    const auto p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Button(id,{width,height});
    const ImVec2 c{p.x+width*.5f,p.y+height*.5f};
    const float r = std::min(width*.28f,ImGui::GetFontSize()*.18f), sign = open ? -1.f : 1.f;
    ImGui::GetWindowDrawList()->AddTriangleFilled({c.x-r,c.y-sign*r*.5f},{c.x+r,c.y-sign*r*.5f},{c.x,c.y+sign*r*.6f},ImGui::GetColorU32(ImGuiCol_Text));
    if(clicked || (open && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))) {
        if(open) ImGui::ClosePopupToLevel(ImGui::GetCurrentContext()->BeginPopupStack.Size,true);
        else ImGui::OpenPopup(popup);
    }
    if(ImGui::IsPopupOpen(popup)) {
        // 自動Popup配置は開いた位置や画面端でアイコン側へ戻るため、Header全体の下へ明示的に固定する。
        const float gap = UI::CurrentEditorTheme().viewportTools.detailPopupGap*ImGui::GetFontSize()/18;
        const auto* viewport = ImGui::GetWindowViewport();
        const float minimumRight = viewport->WorkPos.x+gap+1;
        const float right = std::clamp(p.x+width,minimumRight,std::max(minimumRight,viewport->WorkPos.x+viewport->WorkSize.x-gap));
        const float top = std::max(p.y+height,ImGui::GetWindowPos().y+ImGui::GetWindowSize().y)+gap;
        ImGui::SetNextWindowSizeConstraints({0,0},{std::max(1.f,right-viewport->WorkPos.x-gap),
            std::max(1.f,viewport->WorkPos.y+viewport->WorkSize.y-top-gap)});
        ImGui::SetNextWindowPos({right,top},ImGuiCond_Always,{1,0});
    }
}
/*-----------------------------------------------------------------------------------------
 * ViewportToolShelf
 * - Viewport上の小さな子Window内にToolの一覧と選択状態を描画する
 * - 選択通知は既存Commandへ渡し、Transform処理やScene編集は持たない
 *---------------------------------------------------------------------------------------*/
class ViewportToolShelf final {
public:
    /** \brief RegistryのToolを描画する。戻り値は入力を遮断する実際の小領域 */
    ViewportUiRect Draw(ImVec2 origin, ImVec2 available, const LocalizationService& text, const ViewportToolRegistry& registry) {
        const auto style = UI::CurrentEditorTheme().viewportTools;
        // Command内で別Toolが登録されても、描画中のDescriptor参照を無効にしない。
        const auto tools = registry.Tools();
        const float scale = ImGui::GetFontSize()/18;
        const float button = style.button*scale, padding = style.padding*scale, spacing = style.spacing*scale;
        float height = style.toggleHeight*scale + spacing + padding*2;
        std::string group;
        for(const auto& tool : tools) {
            if(!group.empty() && group != tool.group) height += style.groupSpacing*scale;
            height += button+spacing; group = tool.group;
        }
        const float width = (expanded_ ? style.expandedWidth : style.button+style.padding*2)*scale;
        const ImVec2 size{std::min(width, available.x), std::min(height, available.y)};
        if(size.x <= 0 || size.y <= 0) return {};
        ImGui::SetCursorScreenPos(origin);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {padding,padding});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {spacing,spacing});
        auto background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg); background.w = .9f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
        if(ImGui::BeginChild("ViewportToolShelf", size, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoSavedSettings)) {
            if(ImGui::Button(expanded_ ? "<###ExpandShelf" : ">###ExpandShelf", {ImGui::GetContentRegionAvail().x,style.toggleHeight*scale})) expanded_ = !expanded_;
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",text.Text(expanded_ ? "viewport.collapseTools" : "viewport.expandTools").c_str());
            group.clear();
            for(const auto& tool : tools) {
                if(!group.empty() && group != tool.group) {
                    ImGui::Separator(); ImGui::Dummy({0,std::max(0.f,style.groupSpacing*scale-spacing*2-1)});
                }
                group = tool.group;
                ImGui::PushID(tool.id.c_str());
                const auto& commands = registry.Commands();
                ImGui::BeginDisabled(!commands.CanExecute(tool.id));
                const bool selected = commands.IsChecked(tool.id);
                if(ViewportGlyphButton(tool.iconKey,"##Tool",{ImGui::GetContentRegionAvail().x,button},tool.glyph,
                    expanded_ ? text.Text(tool.labelKey).c_str() : nullptr,selected)) commands.Execute(tool.id);
                ImGui::EndDisabled();
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::BeginTooltip(); ImGui::TextUnformatted(text.Text(tool.labelKey).c_str());
                    if(*commands.Shortcut(tool.id)) ImGui::TextDisabled("%s",commands.Shortcut(tool.id));
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::PopStyleVar(2);
        return {origin,size};
    }
private:
    bool expanded_ = false; //< 既定はアイコンのみ。Toolの状態・編集処理は所有しない
};
/*-----------------------------------------------------------------------------------------
 * ViewportHeader
 * - 座標系・Snap・視点・表示設定だけを小幅のOverlayとして構成する
 * - 実際の設定値は既存ViewportToolsが管理する
 *---------------------------------------------------------------------------------------*/
class ViewportHeader final {
public:
    static float Width() {
        const auto style = UI::CurrentEditorTheme().viewportTools;
        const float scale = ImGui::GetFontSize()/18;
        const bool collapsed = ImGui::GetStateStorage()->GetBool(ImGui::GetID("ViewportHeaderCollapsed"));
        // 子Windowの境界は整数pixelへ丸められる。DPI倍率が端数でも最後の組を折り返さない余白を確保する。
        return std::ceil((style.padding*2+style.detailArrowWidth+(collapsed ? 0 : (style.button+style.detailArrowWidth)*4+style.spacing*4))*scale) + 2;
    }
    /** \brief 設定UIを小幅の子Windowへ構成し、その入力領域を返す */
    template<class DrawSettings>
    static ViewportUiRect Draw(ImVec2 origin, ImVec2 available, const LocalizationService& text, DrawSettings draw) {
        const auto style = UI::CurrentEditorTheme().viewportTools;
        const float scale = ImGui::GetFontSize()/18, padding = style.padding*scale;
        auto* storage = ImGui::GetStateStorage();
        const auto collapseId = ImGui::GetID("ViewportHeaderCollapsed");
        const bool collapsed = storage->GetBool(collapseId);
        const float width = std::min(available.x,Width());
        if(width <= 0 || available.y <= 0) return {};
        ImGui::SetCursorScreenPos(origin);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{padding,padding});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{style.spacing*scale,style.spacing*scale});
        auto background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg); background.w = .9f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg,background);
        ImGui::SetNextWindowSizeConstraints({0,0},{width,std::max(1.f,available.y)});
        // 高さの自動計測中に縦Scrollbarが出ると、有効幅が縮んで折返しが固定される。
        // Headerは行数に合わせて伸長させ、スクロールによる幅の変動を避ける。
        if(ImGui::BeginChild("ViewportHeader",{width,0},ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse)) {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{0,0});
            const bool collapseClicked = ImGui::Button(collapsed ? "<###CollapseOverlay" : ">###CollapseOverlay",{style.detailArrowWidth*scale,style.button*scale});
            ImGui::PopStyleVar();
            if(collapseClicked) storage->SetBool(collapseId,!collapsed);
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",text.Text(collapsed ? "viewport.expandOverlay" : "viewport.collapseOverlay").c_str());
            if(!collapsed) { ImGui::SameLine(); draw(); }
        }
        ImGui::EndChild();
        const auto size = ImGui::GetItemRectSize();
        ImGui::PopStyleColor(); ImGui::PopStyleVar(2);
        return {origin,size};
    }
};
}
