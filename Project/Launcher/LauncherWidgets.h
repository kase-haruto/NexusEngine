#pragma once
#include "imgui.h"
#include <algorithm>

namespace NexusEngine::LauncherUI {
/** \brief Editorの配色を継承し、Launcherのレイアウト寸法だけをDPIへ合わせる */
inline float Scale() { return ImGui::GetStyle().FontScaleDpi; }
inline void Gap(float height = 12) { ImGui::Dummy({0,height * Scale()}); }
inline void Title(const char* title, const char* description) {
    // FontScaleDpiはImGuiが適用するため、論理サイズへDPIを二重乗算しない。
    ImGui::PushFont(nullptr,28); ImGui::TextUnformatted(title); ImGui::PopFont();
    ImGui::TextDisabled("%s",description); Gap(20);
}
/** \brief 主操作にだけ既存テーマのアクセントを使い、文字のコントラストを確保する */
inline bool Primary(const char* label, ImVec2 size = {}) {
    ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImGui::GetStyleColorVec4(ImGuiCol_SeparatorHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImGui::GetStyleColorVec4(ImGuiCol_SeparatorActive));
    ImGui::PushStyleColor(ImGuiCol_Text,{0.08f,0.08f,0.08f,1});
    const bool pressed = ImGui::Button(label,size); ImGui::PopStyleColor(4); return pressed;
}
/** \brief 実際のLevel画像と誤認しない、共通のワイヤーフレーム表紙を描く */
inline void Cover(float height, bool cube = true) {
    const auto start = ImGui::GetCursorScreenPos();
    const ImVec2 size{ImGui::GetContentRegionAvail().x,height * Scale()};
    auto* draw = ImGui::GetWindowDrawList();
    const auto end = ImVec2{start.x + size.x,start.y + size.y};
    const auto line = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
    const auto accent = ImGui::GetColorU32(ImGuiCol_CheckMark);
    draw->AddRectFilled(start,end,ImGui::GetColorU32(ImGuiCol_TitleBg));
    draw->PushClipRect(start,end,true);
    for(int index = 0; index < 9; ++index) {
        const float x = start.x + size.x * static_cast<float>(index) / 8;
        draw->AddLine({start.x + size.x * 0.5f,start.y + size.y * 0.4f},{x,end.y},line);
    }
    for(int index = 0; index < 5; ++index) {
        const float y = start.y + size.y * (0.55f + static_cast<float>(index) * 0.11f);
        draw->AddLine({start.x,y},{end.x,y},line);
    }
    if(cube) {
        const float x = start.x + size.x * 0.5f, y = start.y + size.y * 0.45f;
        const float r = std::min(size.x * 0.17f,size.y * 0.29f);
        const ImVec2 a{x,y-r}, b{x+r,y-r*0.45f}, c{x,y}, d{x-r,y-r*0.45f};
        draw->AddQuad(a,b,c,d,accent,1.5f * Scale());
        draw->AddLine(b,{b.x,b.y+r},accent,1.5f * Scale());
        draw->AddLine(c,{c.x,c.y+r},accent,1.5f * Scale());
        draw->AddLine(d,{d.x,d.y+r},accent,1.5f * Scale());
        draw->AddLine({b.x,b.y+r},{c.x,c.y+r},accent,1.5f * Scale());
        draw->AddLine({d.x,d.y+r},{c.x,c.y+r},accent,1.5f * Scale());
    }
    draw->PopClipRect(); ImGui::Dummy(size);
}
inline void Label(const char* text) { Gap(10); ImGui::TextDisabled("%s",text); }
/** \brief 長いパスでカードの操作を押し流さず、全文はhoverで確認できる */
inline void PathLine(const char* text) {
    const auto start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x, height = ImGui::GetTextLineHeight();
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(start,{start.x + width,start.y + height},true);
    draw->AddText(start,ImGui::GetColorU32(ImGuiCol_TextDisabled),text);
    draw->PopClipRect(); ImGui::Dummy({width,height});
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",text);
}
}
