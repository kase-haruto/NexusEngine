#include "SearchBar.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <vector>

#include "Editor/Gui/Core/GuiContext.h"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_internal.h"

namespace NexusEngine::UI {
    namespace {
        constexpr float kHeight = 16.0f;
        constexpr float kLeftPadding = 10.0f;
        constexpr float kIconSize = 14.0f;
        constexpr float kIconGap = 8.0f;
        constexpr float kStroke = 1.5f;
        constexpr float kRoundingRatio = 0.5f;
        constexpr float kCircleRadiusRatio = 0.32f;
        constexpr float kCircleCenterRatio = 0.36f;
        constexpr float kDiagonalRatio = 0.70710678f;
        constexpr float kHandleEndRatio = 0.92f;
        constexpr std::size_t kInitialBufferSize = 64;

        /** @brief Engineの色をImGuiへ変換する。公開ヘッダーにはImGui型を出さない。 */
        ImVec4 ToImGui(const Color& color) {
            return {color.red, color.green, color.blue, color.alpha};
        }

        /** @brief 一時的なスタイルをスコープ終了時に必ず復元する。 */
        struct StyleScope {
            StyleScope(float paddingX, float paddingY, float height, const Theme* theme) {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(paddingX, paddingY));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, height * kRoundingRatio);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                const auto& style = ImGui::GetStyle();
                ImGui::PushStyleColor(ImGuiCol_FrameBg, theme ? ToImGui(theme->surface) : style.Colors[ImGuiCol_FrameBg]);
                ImGui::PushStyleColor(ImGuiCol_Text, theme ? ToImGui(theme->text) : style.Colors[ImGuiCol_Text]);
                ImGui::PushStyleColor(ImGuiCol_TextDisabled, theme ? ToImGui(theme->textDisabled) : style.Colors[ImGuiCol_TextDisabled]);
            }
            ~StyleScope() { ImGui::PopStyleColor(3); ImGui::PopStyleVar(3); }
            StyleScope(const StyleScope&) = delete;
            StyleScope& operator=(const StyleScope&) = delete;
        };

        /** @brief 実在するvector要素の領域を拡張し、再確保後のポインタをImGuiに返す。 */
        int ResizeBuffer(ImGuiInputTextCallbackData* data) {
            if(data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                auto& buffer = *static_cast<std::vector<char>*>(data->UserData);
                buffer.resize(static_cast<std::size_t>(data->BufSize));
                data->Buf = buffer.data();
            }
            return 0;
        }

        /** @brief 円と斜め線を、左中央の正方形領域内で組み合わせる。 */
        void DrawMagnifier(ImDrawList& drawList, ImVec2 origin, float size, ImU32 color, float thickness) {
            const ImVec2 center(origin.x + size * kCircleCenterRatio, origin.y + size * kCircleCenterRatio);
            const float radius = size * kCircleRadiusRatio;
            const float diagonal = radius * kDiagonalRatio;
            drawList.AddCircle(center, radius, color, 0, thickness);
            drawList.AddLine(ImVec2(center.x + diagonal, center.y + diagonal),
                             ImVec2(origin.x + size * kHandleEndRatio, origin.y + size * kHandleEndRatio),
                             color, thickness);
        }

        /** @brief 入力登録はInputTextExに一任し、装飾だけを後から追加する。 */
        bool DrawSearchBar(const Theme* theme, const char* label, std::string& text, const char* hint, float width) {
            IM_ASSERT(label && label[0]);
            if(!label || !label[0]) return false;
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            if(window->SkipItems) return false;
            // InputTextExの長さはint。表現できない入力を切り詰めず、描画を拒否する。
            if(text.size() >= static_cast<std::size_t>(INT_MAX - 1)) return false;

            const float scale = std::max(ImGui::GetWindowDpiScale() * ImGui::GetStyle().FontScaleMain, 0.01f);
            const float height = std::max(kHeight * scale, ImGui::GetFontSize() + 2.0f * scale);
            const float iconSize = kIconSize * scale;
            const float inputPadding = (kLeftPadding + kIconSize + kIconGap) * scale;
            const float actualWidth = std::max(width > 0.0f ? width : ImGui::GetContentRegionAvail().x, 1.0f);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 end(origin.x + actualWidth, origin.y + height);

            // labelは外側のIDスコープとして使い、内部ラベルは非表示に固定する。
            // Hintと表示名の変化は入力IDに影響しない。通常のPushIDにも対応する。
            ImGui::PushID(label);
            const ImGuiID id = window->GetID("##SearchInput");
            if(auto* state = ImGui::GetInputTextState(id); state && GImGui->ActiveId == id) {
                if(text != state->GetText()) state->ReloadUserBufAndKeepSelection();
            }

            // stdlib版は同梱されているが、string::capacity領域への書き込みを避けるため
            // 一時vectorを使用する。検索文字列の永続状態は保持しない。
            std::vector<char> buffer(std::max(text.size() + 1, kInitialBufferSize), '\0');
            std::memcpy(buffer.data(), text.data(), text.size());
            StyleScope style(inputPadding, (height - ImGui::GetFontSize()) * 0.5f, height, theme);
            // FramePaddingは左右対称。右側にも同じ余白を確保することで、標準の
            // テキスト・選択・カーソルのクリップ処理をそのまま使用できる。
            ImGui::InputTextEx("##SearchInput", hint ? hint : "", buffer.data(),
                               static_cast<int>(buffer.size()), ImVec2(actualWidth, height),
                               ImGuiInputTextFlags_CallbackResize, ResizeBuffer, &buffer);
            const bool active = ImGui::IsItemActive();
            const bool changed = text != buffer.data();
            if(changed) text.assign(buffer.data());

            if(ImGui::IsItemVisible()) {
                auto& drawList = *window->DrawList;
                // Window/TableのClipRectと交差させ、細いPanelでも隣のセルを覆わない。
                drawList.PushClipRect(origin, end, true);
                const ImU32 border = active
                    ? (theme ? ImGui::GetColorU32(ToImGui(theme->accent)) : ImGui::GetColorU32(ImGuiCol_CheckMark))
                    : ImGui::GetColorU32(ImGuiCol_Border);
                drawList.AddRect(origin, end, border, height * kRoundingRatio, 0, scale);
                const ImU32 iconColor = theme ? ImGui::GetColorU32(ToImGui(theme->textDisabled))
                                             : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                DrawMagnifier(drawList, ImVec2(origin.x + kLeftPadding * scale,
                                              origin.y + (height - iconSize) * 0.5f),
                              iconSize, iconColor, kStroke * scale);
                drawList.PopClipRect();
            }
            ImGui::PopID();
            return changed;
        }
    }

    bool SearchBar(const char* label, std::string& text, const char* hint, float width) {
        return DrawSearchBar(nullptr, label, text, hint, width);
    }

    bool SearchBar(Context& context, const char* label, std::string& text, const char* hint, float width) {
        return DrawSearchBar(&context.GetTheme(), label, text, hint, width);
    }
}
