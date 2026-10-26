#include "EditorChrome.h"
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
		section.draw  = [labelKey = std::move(labelKey), command = std::move(command)](const auto& text, const auto& commands) {
			ImGui::BeginDisabled(!commands.CanExecute(command));
			if(ImGui::Button(text.Label(labelKey, "button").c_str())) {
				commands.Execute(command);
			}
			ImGui::EndDisabled();

			// 無効な操作にも説明を出す。Shortcutはメニューと同じCommand登録から取得する。
			if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
				ImGui::SetTooltip("%s %s", text.Text(labelKey).c_str(), commands.Shortcut(command));
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
		ResetOffsets();
		height_ = 0;
	}
	bool EditorToolbarRegistry::SetOffset(const std::string& id, ToolbarOffset offset) {
		// UIからの編集と設定ファイルからの復元で、同じ入力制約を使用する。
		if(id.empty() || !IsValidToolbarOffset(offset)) {
			return false;
		}
		offset = ClampToolbarOffset(offset);
		if(GetOffset(id) == offset) {
			return true;
		}

		// 初期位置は保存せず、リセットした項目が設定ファイルへ残り続けないようにする。
		if(offset == ToolbarOffset{}) {
			offsets_.erase(id);
		} else {
			offsets_[id] = offset;
		}
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
		if(!offsets_.empty()) {
			offsets_.clear();
			++layoutRevision_;
		}
	}
	void EditorToolbarRegistry::Draw(const LocalizationService& text, const EditorCommandRegistry& commands) {
		drawing_				  = true;
		const ImVec2 contentStart = ImGui::GetCursorScreenPos();
		const float	 right		  = contentStart.x + ImGui::GetContentRegionAvail().x;
		const float	 scale		  = ImGui::GetFontSize() / 18.0f;
		bool		 first		  = true;
		for(auto& entry : entries_) {
			// フォントサイズや言語の変更を毎フレーム反映し、固定ピクセル幅には依存しない。
			const float width		  = entry.labelKey.empty() ? entry.section.minimumWidthEm * ImGui::GetFontSize() : ImGui::CalcTextSize(text.Text(entry.labelKey).c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
			const auto	offset		  = GetOffset(entry.section.id);
			const float requiredWidth = width + std::max(0.0f, offset[0] * scale);

			// 次の項目が収まる場合だけ同じ行に置く。収まらない場合は通常の次行位置を使う。
			if(!first && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + requiredWidth <= right) {
				ImGui::SameLine();
			}

			// 自動配置の基準位置を残し、その位置へユーザー指定のオフセットを加える。
			// 横方向は実寸、縦方向は画面の高さを考慮し、項目が操作不能な位置へ隠れないようにする。
			const ImVec2 base		  = ImGui::GetCursorScreenPos();
			const float	 visibleWidth = std::min(std::max(width, entry.previousWidth), right - contentStart.x);
			const float	 maximumX	  = std::max(contentStart.x, right - visibleWidth);
			const float	 maximumY	  = contentStart.y + ImGui::GetMainViewport()->Size.y * .4f;
			const ImVec2 position{
				std::clamp(base.x + offset[0] * scale, contentStart.x, maximumX),
				std::clamp(base.y + offset[1] * scale, contentStart.y, std::max(contentStart.y, maximumY))};
			ImGui::SetCursorScreenPos(position);

			// Groupの外接矩形を使い、Popup内部の最後のItemから次項目の配置計算を切り離す。
			// 機能ごとにIDを分けるため、別セクションに同名ボタンがあっても衝突しない。
			ImGui::PushID(entry.section.id.c_str());
			ImGui::BeginGroup();
			entry.section.draw(text, commands);
			ImGui::EndGroup();
			const ImVec2 sectionMinimum = ImGui::GetItemRectMin();
			const ImVec2 sectionMaximum = ImGui::GetItemRectMax();
			const ImVec2 sectionSize	= ImGui::GetItemRectSize();
			entry.previousWidth			= sectionSize.x;

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
			const ImVec2 occupied{
				std::max(sectionSize.x, position.x - base.x + sectionSize.x),
				std::max(sectionSize.y, position.y - base.y + sectionSize.y)};
			ImGui::SetCursorScreenPos(base);
			ImGui::Dummy(occupied);
			first = false;
		}
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
		const auto* viewport = ImGui::GetMainViewport();
		const auto	padding	 = ImGui::GetStyle().WindowPadding;
		ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x - padding.x,
								 viewport->WorkPos.y + viewport->WorkSize.y - padding.y},
								ImGuiCond_Always, {1, 1});
		ImGui::SetNextWindowBgAlpha(0.95f);
		if(ImGui::Begin("###Notifications", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing)) {
			for(const auto& item : items) {
				ImGui::TextUnformatted(item.error ? "!" : "+");
				ImGui::SameLine();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
				ImGui::TextUnformatted(item.text.c_str());
				ImGui::PopTextWrapPos();
			}
		}
		ImGui::End();
	}
} // namespace NexusEngine
