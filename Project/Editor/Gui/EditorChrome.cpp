#include "EditorChrome.h"
#include "Editor/Gui/Widgets/EditorIconButton.h"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_internal.h"
#include <algorithm>
#include <cmath>

namespace NexusEngine {
	bool EditorToolbarRegistry::RegisterSection(Section section) {
		// 描画中に配列が変わると、現在参照しているセクションが無効になるため登録を拒否する。
		if(drawing_ || section.id.empty() || !section.draw) {
			return false;
		}

		// 最低幅はレイアウト計算に使う。負数や非数を受け入れない。
		if(!std::isfinite(section.minimumWidthEm) || section.minimumWidthEm < 0) {
			return false;
		}

		// 安定したIDを一意にし、異なる機能のボタンやPopupの状態が衝突しないようにする。
		for(const auto& entry : entries_) {
			if(entry.section.id == section.id) {
				return false;
			}
		}

		entries_.push_back({std::move(section), {}});

		// 同じ優先順位の項目は登録順を維持し、外部機能が追加された場合も配置を予測しやすくする。
		std::stable_sort(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) {
			return a.section.order < b.section.order;
		});
		return true;
	}
	bool EditorToolbarRegistry::RegisterButton(std::string id, std::string labelKey, std::string command, int order) {
		if(labelKey.empty() || command.empty()) {
			return false;
		}

		// 表示名は翻訳用のキー、操作対象はCommandのIDとしてそれぞれ保持する。
		// Commandがまだ登録されていない場合も、描画時の実行可否判定によって無効なボタンになる。
		const auto sectionId = id;
		const auto label	 = labelKey;
		Section	   section;
		section.id	  = std::move(id);
		section.order = order;
		section.draw  = [icon = sectionId, labelKey = std::move(labelKey), command = std::move(command)](const auto& text, const auto& commands) {
			ImGui::BeginDisabled(!commands.CanExecute(command));
			if(UI::EditorIconButton(icon, text.Label(labelKey, "button").c_str())) {
				commands.Execute(command);
			}
			ImGui::EndDisabled();

			// 無効な操作にも説明を出す。Shortcutはメニューと同じCommand登録から取得する。
			if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
				const auto key = labelKey + ".hint";
				const auto hint = text.Text(key);
				ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
				ImGui::TextUnformatted((hint == key ? text.Text(labelKey) : hint).c_str());
				if(*commands.Shortcut(command)) ImGui::TextDisabled("%s",commands.Shortcut(command));
				ImGui::PopTextWrapPos(); ImGui::EndTooltip();
			}
		};

		if(!RegisterSection(std::move(section))) {
			return false;
		}

		// ボタンは翻訳後の実寸で配置するため、最低幅の代わりに表示名のキーを記録する。
		for(auto& entry : entries_) {
			if(entry.section.id == sectionId) {
				entry.labelKey = label;
				break;
			}
		}
		return true;
	}
	bool EditorToolbarRegistry::Unregister(std::string_view id) {
		// 自分自身を描画コールバック内で破棄する操作は許可しない。
		if(drawing_) {
			return false;
		}
		return std::erase_if(entries_, [&](const auto& entry) {
				   return entry.section.id == id;
			   }) != 0;
	}
	void EditorToolbarRegistry::Clear() {
		if(drawing_) {
			return;
		}

		// Workspaceの終了・再生成時に、機能側のオブジェクトを借用するコールバックも解除する。
		entries_.clear();
		draggingSection_.clear();
		ResetOffsets();
		height_ = 0;
	}
	bool EditorToolbarRegistry::SetOffset(const std::string& id, ToolbarOffset offset) {
		// UIからの編集と設定ファイルからの復元で、同じ入力制約を使用する。
		if(id.empty() || !IsValidToolbarOffset(offset)) {
			return false;
		}
		offset = ClampToolbarOffset(offset);
		if(GetOffset(id) == offset && !placements_.contains(id)) {
			return true;
		}

		// 初期位置は保存せず、リセットした項目が設定ファイルへ残り続けないようにする。
		if(offset == ToolbarOffset{}) {
			offsets_.erase(id);
		} else {
			offsets_[id] = offset;
		}
		placements_.erase(id);
		++layoutRevision_;
		return true;
	}
	ToolbarOffset EditorToolbarRegistry::GetOffset(const std::string& id) const {
		const auto found = offsets_.find(id);
		if(found == offsets_.end()) {
			return {};
		}
		return found->second;
	}
	void EditorToolbarRegistry::SetOffsets(const ToolbarOffsets& offsets) {
		// 不正な設定値を除き、未登録セクションの設定は将来の再登録に備えて残す。
		ToolbarOffsets normalized;
		for(const auto& [id, offset] : offsets) {
			if(id.empty() || !IsValidToolbarOffset(offset)) {
				continue;
			}
			const auto value = ClampToolbarOffset(offset);
			if(value != ToolbarOffset{}) {
				normalized[id] = value;
			}
		}
		if(offsets_ != normalized) {
			offsets_ = std::move(normalized);
			++layoutRevision_;
		}
	}
	void EditorToolbarRegistry::ResetOffsets() {
		if(!offsets_.empty() || !placements_.empty()) {
			offsets_.clear();
			placements_.clear();
			++layoutRevision_;
		}
	}
    void EditorToolbarRegistry::SetPlacements(const ToolbarPlacements& placements) {
        ToolbarPlacements normalized;
        for(const auto& [id,value] : placements) if(!id.empty() && IsValidToolbarPlacement(value)) normalized[id] = value;
        if(placements_ != normalized) { placements_ = std::move(normalized); ++layoutRevision_; }
    }
	void EditorToolbarRegistry::Draw(const LocalizationService& text, const EditorCommandRegistry& commands) {
		drawing_				  = true;
        if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)) draggingSection_.clear();
		const ImVec2 contentStart = ImGui::GetCursorScreenPos();
		const float	 right		  = contentStart.x + ImGui::GetContentRegionAvail().x;
		const float	 scale		  = ImGui::GetFontSize() / 18.0f;
		bool		 first		  = true;
        float drawnBottom = contentStart.y;
		for(auto& entry : entries_) {
			// フォントサイズや言語の変更を毎フレーム反映し、固定ピクセル幅には依存しない。
			const float width		  = entry.labelKey.empty() ? entry.section.minimumWidthEm * ImGui::GetFontSize() :
                (UI::FindEditorIcon(entry.section.id) ? ImGui::GetFontSize() : ImGui::CalcTextSize(text.Text(entry.labelKey).c_str()).x) + ImGui::GetStyle().FramePadding.x * 2;
			const auto	offset		  = GetOffset(entry.section.id);
			const float requiredWidth = std::max(width, entry.previousWidth);

			// 次の項目が収まる場合だけ同じ行に置く。収まらない場合は通常の次行位置を使う。
			if(!first && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + requiredWidth <= right) {
				ImGui::SameLine();
			}

			// 自動配置の基準位置を残し、その位置へユーザー指定のオフセットを加える。
			// 横方向は実寸、縦方向は画面の高さを考慮し、項目が操作不能な位置へ隠れないようにする。
			const ImVec2 base		  = ImGui::GetCursorScreenPos();
			const float	 visibleWidth = std::min(entry.previousWidth > 0 ? entry.previousWidth : width, std::max(0.f,right - contentStart.x));
			const float	 maximumX	  = std::max(contentStart.x, right - visibleWidth);
			const float	 maximumY	  = contentStart.y + ImGui::GetMainViewport()->Size.y * .4f;
			ImVec2 position{
				std::clamp(base.x + offset[0] * scale, contentStart.x, maximumX),
				std::clamp(base.y + offset[1] * scale, contentStart.y, std::max(contentStart.y, maximumY))};
            if(const auto found = placements_.find(entry.section.id); found != placements_.end()) {
                const auto& placement = found->second;
                position.x = std::clamp(placement[2] == 1 ? right-visibleWidth-placement[0]*scale : contentStart.x+placement[0]*scale,contentStart.x,maximumX);
                position.y = std::clamp(contentStart.y+placement[1]*scale,contentStart.y,std::max(contentStart.y,maximumY));
                // 自動配置の行が変わっても、現在の画面に対するオフセットへ再計算する。
                offsets_[entry.section.id] = {(position.x-base.x)/scale,(position.y-base.y)/scale};
            }
			ImGui::SetCursorScreenPos(position);

			// Groupの外接矩形を使い、Popup内部の最後のItemから次項目の配置計算を切り離す。
			// 機能ごとにIDを分けるため、別セクションに同名ボタンがあっても衝突しない。
			ImGui::PushID(entry.section.id.c_str());
			ImGui::BeginGroup();
            ImGui::BeginDisabled(ImGui::GetIO().KeyAlt || !draggingSection_.empty());
			entry.section.draw(text, commands);
            ImGui::EndDisabled();
			ImGui::EndGroup();
			const ImVec2 sectionMinimum = ImGui::GetItemRectMin();
			const ImVec2 sectionMaximum = ImGui::GetItemRectMax();
            drawnBottom = std::max(drawnBottom,sectionMaximum.y+ImGui::GetStyle().ItemSpacing.y);
			const ImVec2 sectionSize	= ImGui::GetItemRectSize();
			entry.previousWidth			= sectionSize.x;
            if(!placements_.contains(entry.section.id) && offset != ToolbarOffset{}) {
                const bool fromRight = position.x+sectionSize.x*.5f > (contentStart.x+right)*.5f;
                placements_[entry.section.id] = {std::max(0.f,(fromRight ? right-position.x-sectionSize.x : position.x-contentStart.x)/scale),
                    std::max(0.f,(position.y-contentStart.y)/scale),fromRight ? 1.f : 0.f};
                ++layoutRevision_;
            }
            if(draggingSection_.empty() && ImGui::GetIO().KeyAlt &&
               ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_NoNavOverride) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                draggingSection_ = entry.section.id;
            if(draggingSection_ == entry.section.id && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                auto moved = GetOffset(entry.section.id);
                moved[0] += ImGui::GetIO().MouseDelta.x / scale;
                moved[1] += ImGui::GetIO().MouseDelta.y / scale;
                SetOffset(entry.section.id, moved);
            }

			// 無効化されたボタンにも右クリックを許可し、配置変更と機能の実行可否を分離する。
			// 既存のPopupが開いている場合は、背後のセクションで配置メニューを開かない。
			if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
				ImGui::OpenPopup("SectionLayout");
			}
			if(ImGui::BeginPopup("SectionLayout")) {
				ImGui::SeparatorText(text.Text("toolbar.position").c_str());
				ImGui::TextWrapped("%s", text.Text("toolbar.offsetHelp").c_str());
				auto edited = GetOffset(entry.section.id);

				// ドラッグと直接数値入力の両方で調整し、変更は次のフレームからUIへ反映する。
				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
				const bool changedX = ImGui::DragFloat(text.Text("toolbar.offsetX").c_str(), &edited[0], .5f,
													   -kToolbarOffsetLimitX, kToolbarOffsetLimitX, "%.1f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
				const bool changedY = ImGui::DragFloat(text.Text("toolbar.offsetY").c_str(), &edited[1], .5f,
													   -kToolbarOffsetLimitY, kToolbarOffsetLimitY, "%.1f", ImGuiSliderFlags_AlwaysClamp);
				if(changedX || changedY) {
					SetOffset(entry.section.id, edited);
				}

				if(ImGui::Button(text.Label("toolbar.resetSection", "ResetSection").c_str())) {
					SetOffset(entry.section.id, {});
				}
				if(ImGui::Button(text.Label("toolbar.resetAll", "ResetAll").c_str())) {
					ResetOffsets();
				}
				ImGui::EndPopup();
			}

			// 内部IDを画面へ出す代わりに、調整しているセクションを枠で示す。
			if(ImGui::IsPopupOpen("SectionLayout")) {
				ImGui::GetWindowDrawList()->AddRect(sectionMinimum, sectionMaximum,
													ImGui::GetColorU32(ImGuiCol_HeaderActive), ImGui::GetStyle().FrameRounding, 0, 1.5f);
			}
			ImGui::PopID();

			// 移動先も含む占有領域を通常のItemとして予約し、後続の項目とDockSpaceへ高さを伝える。
			// 明示的にDummyを置くことで、Cursor移動だけで親Windowの境界を変える処理も避ける。
			const ImVec2 occupied{sectionSize.x, sectionSize.y};
			ImGui::SetCursorScreenPos(base);
			ImGui::Dummy(occupied);
			first = false;
		}
        // 手動位置は後続項目の自動折り返しから切り離し、縦方向の占有だけをSidebarへ伝える。
        const auto end = ImGui::GetCursorScreenPos();
        ImGui::Dummy({0,std::max(0.f,drawnBottom-end.y)});
		drawing_ = false;
	}
	// ステータス表示を省略してUIを調整する場合も、関数の呼び出し契約は維持する。
	// 表示専用の引数には未使用を許可し、コメントアウト時のC4100がC2220になることを防ぐ。
	// 他のコンパイル警告は従来どおり検出し、プロジェクト全体の警告設定は変更しない。
	void DrawEditorToolbar(const LocalizationService& text, const EditorCommandRegistry& commands,
		[[maybe_unused]] std::string_view document,
		[[maybe_unused]] bool dirty,
		[[maybe_unused]] bool playing,
		[[maybe_unused]] bool paused,
		EditorToolbarRegistry& toolbar) {
		const auto	flags		  = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
		const float minimumHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y * 2;
		if(ImGui::BeginViewportSideBar("###EditorToolbar", ImGui::GetMainViewport(), ImGuiDir_Up,
									   std::max(minimumHeight, toolbar.Height()), flags)) {
			toolbar.Draw(text, commands);
			

			// 折り返しで増えた高さを、次フレームのDockSpace用作業領域から除外する。
			toolbar.SetHeight(std::max(minimumHeight, ImGui::GetCursorScreenPos().y - ImGui::GetWindowPos().y + ImGui::GetStyle().WindowPadding.y));
		}
		ImGui::End();
	}
	void DrawEditorNotifications(NotificationService& notifications) {
		const auto& items = notifications.Items();
		if(items.empty()) return;
		auto* viewport = ImGui::GetMainViewport();
		const float em = ImGui::GetFontSize();
		const float margin = em;
		const float width = std::min(em * 24, viewport->WorkSize.x - margin * 2);
		if(width < em * 6 || viewport->WorkSize.y < em * 6) return;
		const float textWidth = width - em * 4;
		float bottom = viewport->WorkPos.y + viewport->WorkSize.y - margin;
		auto* draw = ImGui::GetForegroundDrawList(viewport);
		const auto now = std::chrono::steady_clock::now();
		draw->PushClipRect(viewport->WorkPos, {viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y});
		// 新しい通知を右下に置き、各カードを独立してフェードする。入力は背後のUIへ渡す。
		for(auto it = items.rbegin(); it != items.rend(); ++it) {
			const float remaining = std::chrono::duration<float>(it->expires - now).count();
			const float age = 6.0f - remaining;
			const float opacity = std::clamp(std::min(age / .18f, remaining / .4f), 0.0f, 1.0f);
			const auto color = [opacity](int r, int g, int b, int alpha = 255) {
				return IM_COL32(r, g, b, static_cast<int>(static_cast<float>(alpha) * opacity));
			};
			const float textHeight = ImGui::CalcTextSize(it->text.c_str(), nullptr, false, textWidth).y;
			const float height = std::min(std::max(em * 3.4f, textHeight + em * 1.5f), viewport->WorkSize.y * .21f);
			const float left = viewport->WorkPos.x + viewport->WorkSize.x - margin - width + (1 - opacity) * em;
			const ImVec2 min{left, bottom - height}, max{left + width, bottom};
			draw->AddRectFilled({min.x + 2, min.y + 3}, {max.x + 2, max.y + 3}, color(0, 0, 0, 75), 3);
			draw->AddRectFilled(min, max, color(43, 43, 43, 250), 3);
			draw->AddRect(min, max, color(85, 85, 85), 3);
			const ImU32 accent = it->error ? color(235, 83, 77) : color(111, 190, 106);
			const ImVec2 center{min.x + em * 1.25f, min.y + height * .5f};
			draw->AddCircle(center, em * .55f, accent, 24, 1.5f);
			if(it->error) {
				draw->AddLine({center.x - em * .2f, center.y - em * .2f}, {center.x + em * .2f, center.y + em * .2f}, accent, 2);
				draw->AddLine({center.x + em * .2f, center.y - em * .2f}, {center.x - em * .2f, center.y + em * .2f}, accent, 2);
			} else {
				draw->AddLine({center.x - em * .3f, center.y}, {center.x - em * .06f, center.y + em * .22f}, accent, 2);
				draw->AddLine({center.x - em * .06f, center.y + em * .22f}, {center.x + em * .3f, center.y - em * .23f}, accent, 2);
			}
			draw->PushClipRect({min.x + em * 2.5f, min.y + em * .6f}, {max.x - em, max.y - em * .6f}, true);
			draw->AddText(nullptr, em, {min.x + em * 2.5f, min.y + std::max(em * .75f, (height - textHeight) * .5f)}, color(235, 235, 235), it->text.c_str(), nullptr, textWidth);
			draw->PopClipRect();
			bottom = min.y - em * .5f;
		}
		draw->PopClipRect();
	}
} // namespace NexusEngine
