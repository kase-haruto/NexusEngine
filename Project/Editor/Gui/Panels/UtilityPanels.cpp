#include "UtilityPanels.h"
#include "Editor/Gui/Widgets/AssetPropertyWidget.h"
#include "Editor/Gui/Widgets/SearchBar.h"
#include "Editor/Services/EditorCommandRegistry.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Services/SelectionService.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_internal.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace NexusEngine {
	namespace {
		std::string LogLine(const OwnedLogRecord& record, const LocalizationService& text) {
			auto	time = std::chrono::system_clock::to_time_t(record.timestamp);
			std::tm local{};
			localtime_s(&local, &time);
			static constexpr const char* keys[] = {"log.trace", "log.debug", "log.info", "log.warning", "log.error", "log.critical"};
			auto						 level	= static_cast<size_t>(record.level);
			std::ostringstream			 out;
			out << std::put_time(&local, "%H:%M:%S") << " [" << text.Text(keys[level < 6 ? level : 5]) << "] ["
				<< record.category << "] " << record.message << " (" << record.source << ')';
			return out.str();
		}
	} // namespace
	void OutputLogPanel::Draw(const EditorContext& context) {
		if(ImGui::Button(context.text.Label("clear", "clear").c_str())) buffer_->Clear();
		ImGui::SameLine();
		const bool copy = ImGui::Button(context.text.Label("copy", "copy").c_str());
		ImGui::SameLine();
		ImGui::Checkbox(context.text.Label("log.autoscroll", "auto").c_str(), &autoScroll_);
		UI::SearchBar("OutputLogSearch", search_, context.text.Text("search").c_str());
		static constexpr const char* keys[] = {"log.trace", "log.debug", "log.info", "log.warning", "log.error", "log.critical"};
		for(size_t i = 0; i < levels_.size(); ++i) {
			if(i) ImGui::SameLine();
			ImGui::Checkbox(context.text.Label(keys[i], keys[i]).c_str(), &levels_[i]);
		}
		// mutex下でコピーしたsnapshotだけを描く。UI処理中はログthreadを妨げない。
		auto		snapshot  = buffer_->SnapshotIfChanged(logRevision_);
		std::string filterKey = search_ + '\n' + std::string(context.text.Language());
		for(bool enabled : levels_) filterKey += enabled ? '1' : '0';
		if(snapshot || filterKey != filterKey_) {
			if(snapshot) records_ = std::move(*snapshot);
			filterKey_ = std::move(filterKey);
			lines_.clear();
			for(const auto& record : records_) {
				const auto level = static_cast<size_t>(record.level);
				if(level >= levels_.size() || !levels_[level]) continue;
				auto line = LogLine(record, context.text);
				// 1ログを1clip行にする。元messageの改行は行ごとに保持する。
				size_t start = 0;
				do {
					auto end  = line.find('\n', start);
					auto part = line.substr(start, end == line.npos ? line.npos : end - start);
					if(search_.empty() || part.find(search_) != part.npos) lines_.push_back(std::move(part));
					if(end == line.npos) break;
					start = end + 1;
				} while(start <= line.size());
			}
		}
		std::string clipboard;
		ImGui::BeginChild("records", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
		bool			 atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1;
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(lines_.size()));
		while(clipper.Step())
			for(int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) ImGui::TextUnformatted(lines_[row].c_str());
		if(autoScroll_ && atBottom) ImGui::SetScrollHereY(1);
		ImGui::EndChild();
		if(copy) {
			for(const auto& line : lines_) clipboard += line + '\n';
			ImGui::SetClipboardText(clipboard.c_str());
		}
	}
	void TerminalPanel::UpdateSuggestions(std::string_view input) {
		if(input != suggestionPrefix_) {
			suggestionPrefix_	= input;
			suggestionIndex_	= 0;
			scrollToSuggestion_ = true;
		}
		suggestions_ = input.starts_with('/') && input.find_first_of(" \t\r\n") == input.npos
						   ? console_.Complete(input)
						   : std::vector<std::string>{};
	}
	void TerminalPanel::Draw(const EditorContext& context) {
		ImGui::BeginChild("results", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
		for(const auto& line : output_) ImGui::TextUnformatted(line.c_str());
		if(scrollToBottom_) {
			ImGui::SetScrollHereY(1);
			scrollToBottom_ = false;
		}
		ImGui::EndChild();
		UpdateSuggestions(input_.data());
		const auto callback = [](ImGuiInputTextCallbackData* data) -> int {
			auto&		self = *static_cast<TerminalPanel*>(data->UserData);
			std::string replacement;
			if(!self.pendingCompletion_.empty()) {
				replacement = std::move(self.pendingCompletion_);
				self.pendingCompletion_.clear();
			} else if(data->EventFlag == ImGuiInputTextFlags_CallbackEdit) {
				self.UpdateSuggestions(std::string_view(data->Buf, data->BufTextLen));
				return 0;
			} else if(data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
				self.UpdateSuggestions(std::string_view(data->Buf, data->BufTextLen));
				if(!self.suggestions_.empty()) {
					const int count			 = static_cast<int>(self.suggestions_.size());
					self.suggestionIndex_	 = (self.suggestionIndex_ + (data->EventKey == ImGuiKey_UpArrow ? count - 1 : 1)) % count;
					self.scrollToSuggestion_ = true;
					return 0;
				}
				const auto& history = self.console_.History();
				if(history.empty()) return 0;
				if(data->EventKey == ImGuiKey_UpArrow) {
					if(self.historyIndex_ < 0)
						self.historyIndex_ = static_cast<int>(history.size()) - 1;
					else if(self.historyIndex_ > 0)
						--self.historyIndex_;
				} else if(self.historyIndex_ >= 0 && ++self.historyIndex_ >= static_cast<int>(history.size()))
					self.historyIndex_ = -1;
				if(self.historyIndex_ >= 0) replacement = history[self.historyIndex_];
			} else if(data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
				self.UpdateSuggestions(std::string_view(data->Buf, data->BufTextLen));
				if(!self.suggestions_.empty())
					replacement = '/' + self.suggestions_[self.suggestionIndex_] + ' ';
				else {
					auto options = self.console_.Complete(std::string_view(data->Buf, data->CursorPos));
					if(options.size() != 1) return 0;
					replacement = (data->Buf[0] == '/' ? "/" : "") + options.front() + ' ';
				}
			} else
				return 0;
			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, replacement.c_str());
			self.UpdateSuggestions(std::string_view(data->Buf, data->BufTextLen));
			return 0;
		};
		if(focusInput_) {
			ImGui::SetKeyboardFocusHere();
			focusInput_ = false;
		}
		bool	   execute	   = ImGui::InputText(context.text.Label("console.input", "input").c_str(), input_.data(), input_.size(),
												  ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCompletion |
													  ImGuiInputTextFlags_CallbackEdit | ImGuiInputTextFlags_CallbackAlways,
												  callback, this);
		const auto inputOrigin = ImGui::GetItemRectMin();
		const auto inputWidth  = ImGui::GetItemRectSize().x;
		const bool inputActive = ImGui::IsItemActive();
		UpdateSuggestions(input_.data());
		if(inputActive) suggestionsOpen_ = true;
		if(suggestions_.empty()) suggestionsOpen_ = false;
		if(execute && !suggestions_.empty()) {
			pendingCompletion_ = '/' + suggestions_[suggestionIndex_] + ' ';
			focusInput_		   = true;
			execute			   = false;
		}
		ImGui::SameLine();
		execute |= ImGui::Button(context.text.Label("console.execute", "execute").c_str());
		if(!suggestions_.empty() && suggestionsOpen_) {
			ImGui::SetNextWindowPos(inputOrigin, ImGuiCond_Always, {0, 1});
			const auto maxHeight = ImGui::GetTextLineHeightWithSpacing() * 8 + ImGui::GetStyle().WindowPadding.y * 2;
			ImGui::SetNextWindowSizeConstraints({inputWidth, 0}, {inputWidth, maxHeight});
			if(ImGui::Begin("###TerminalSuggestions", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
				// Keep the list above the terminal without moving keyboard focus out of InputText.
				ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
				for(size_t i = 0; i < suggestions_.size(); ++i) {
					const bool selected = static_cast<int>(i) == suggestionIndex_;
					const auto label	= '/' + suggestions_[i];
					if(ImGui::Selectable(label.c_str(), selected)) {
						pendingCompletion_ = label + ' ';
						focusInput_		   = true;
					}
					if(selected && scrollToSuggestion_) ImGui::SetScrollHereY();
				}
				scrollToSuggestion_ = false;
				if(ImGui::IsMouseClicked(0) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && !inputActive)
					suggestionsOpen_ = false;
			}
			ImGui::End();
		}
		if(execute) focusInput_ = true;
		if(execute && input_[0]) {
			output_.emplace_back(std::string("> ") + input_.data());
			auto result = console_.Execute(input_.data());
			if(!result.text.empty()) output_.push_back(result.translate ? context.text.Text(result.text) : result.text);
			while(output_.size() > 256) output_.pop_front();
			input_.fill(0);
			historyIndex_	= -1;
			scrollToBottom_ = true;
		}
		ImGui::TextUnformatted(context.text.Text("console.help").c_str());
	}
	void ContentBrowserPanel::Draw(const EditorContext& context) {
		if(ImGui::Button(context.text.Label("asset.up", "up").c_str())) assets_.Navigate(assets_.Current().parent_path());
		ImGui::SameLine();
		if(ImGui::Button(context.text.Label("asset.refresh", "refresh").c_str())) assets_.Refresh();
		ImGui::SameLine();
		ImGui::TextUnformatted(AssetCatalog::Utf8(assets_.Current()).c_str());
		UI::SearchBar("ContentBrowserSearch", search_, context.text.Text("search").c_str());
		if(!ImGui::BeginTable("browser", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) return;
		ImGui::TableSetupColumn("folders", ImGuiTableColumnFlags_WidthFixed, 180);
		ImGui::TableSetupColumn("assets", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		std::optional<std::filesystem::path>					  navigate;
		std::function<void(const std::filesystem::path&, size_t)> tree = [&](const auto& path, size_t depth) {
			auto id = AssetCatalog::Utf8(path);
			ImGui::PushID(id.c_str());
			bool open = ImGui::TreeNodeEx("folder", ImGuiTreeNodeFlags_OpenOnArrow, "%s", AssetCatalog::Utf8(path.filename()).c_str());
			if(ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) navigate = path;
			if(open) {
				if(depth < 12) {
					auto entries = assets_.Snapshot(path);
					for(const auto& entry : *entries)
						if(entry.directory) tree(entry.path, depth + 1);
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		};
		tree(assets_.Root(), 0);
		ImGui::TableNextColumn();
		ImGui::BeginChild("asset-list", {0, 0});
		auto				entries = assets_.Snapshot(assets_.Current());
		std::vector<size_t> matching;
		for(size_t i = 0; i < entries->size(); ++i)
			if(search_.empty() || (*entries)[i].name.find(search_) != std::string::npos) matching.push_back(i);
		ImGuiListClipper assetClipper;
		assetClipper.Begin(static_cast<int>(matching.size()));
		while(assetClipper.Step())
			for(int row = assetClipper.DisplayStart; row < assetClipper.DisplayEnd; ++row) {
				const auto& entry = (*entries)[matching[row]];
				auto		path  = AssetCatalog::Utf8(entry.path);
				ImGui::PushID(path.c_str());
				auto label = entry.name + "  [" + context.text.Text(entry.typeKey) + ']';
				if(ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) {
					if(entry.directory)
						navigate = entry.path;
					else if(open_)
						open_(entry.path);
				}
				if(!entry.directory && ImGui::BeginDragDropSource()) {
					// OS handleやResource pointerでなくUTF-8 pathをコピーする拡張用payload。
					ImGui::SetDragDropPayload("NEXUS_ASSET_PATH", path.c_str(), path.size() + 1);
					ImGui::TextUnformatted(entry.name.c_str());
					ImGui::EndDragDropSource();
				}
				if(context.assets && !entry.directory && (entry.typeKey == "asset.model" || entry.typeKey == "asset.texture") && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
					ImGui::BeginTooltip();
					context.assets->DrawPreview(path, entry.typeKey == "asset.texture", -1, 160);
					ImGui::EndTooltip();
				}
				ImGui::PopID();
			}
		ImGui::EndChild();
		ImGui::EndTable();
		if(navigate) assets_.Navigate(*navigate);
	}
	void PreferencesPanel::Draw(const EditorContext& context) {
		bool changed = false;
		if(!ImGui::BeginTable("PreferencesLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) return;
		ImGui::TableSetupColumn("Categories", ImGuiTableColumnFlags_WidthFixed, 180);
		ImGui::TableSetupColumn("Settings", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		const char* categories[] = {"settings.general", "settings.levelEditor", "settings.rendering"};
		for(int i = 0; i < 3; ++i)
			if(ImGui::Selectable(context.text.Label(categories[i], categories[i]).c_str(), category_ == i)) category_ = i;
		ImGui::TableSetColumnIndex(1);
		ImGui::BeginChild("PreferencesContent");
		ImGui::SeparatorText(context.text.Text(categories[category_]).c_str());
		if(category_ == 0) {
			int		   language	 = settings_.user.language == "ja" ? 1 : 0;
			const auto languages = context.text.Text("language.english") + '\0' + context.text.Text("language.japanese") + '\0';
			if(ImGui::Combo(context.text.Label("settings.language", "language").c_str(), &language, languages.c_str())) {
				settings_.user.language = language == 1 ? "ja" : "en";
				changed					= true;
			}
			int		   theme  = settings_.user.theme == "Light" ? 1 : 0;
			const auto themes = context.text.Text("theme.dark") + '\0' + context.text.Text("theme.light") + '\0';
			if(ImGui::Combo(context.text.Label("settings.theme", "theme").c_str(), &theme, themes.c_str())) {
				settings_.user.theme = theme ? "Light" : "Dark";
				changed				 = true;
			}
			changed |= ImGui::SliderFloat(context.text.Label("settings.font", "font").c_str(), &settings_.user.fontSize, 10, 40);
			changed |= ImGui::SliderFloat(context.text.Label("settings.scale", "scale").c_str(), &settings_.user.uiScale, 0.5f, 3);
			changed |= ImGui::Checkbox(context.text.Label("settings.layout", "layout").c_str(), &settings_.user.autoSaveLayout);
		} else if(category_ == 1) {
			changed |= ImGui::Checkbox(context.text.Label("settings.gizmo", "gizmo").c_str(), &settings_.editor.gizmo);
			changed |= ImGui::Checkbox(context.text.Label("settings.viewportStartup", "viewportStartup").c_str(), &settings_.editor.viewport);
		} else {
			const char* modes[] = {"Off", "DDGI", "RTGI", "Baked"};
			int			mode	= 0;
			for(int i = 0; i < 4; ++i)
				if(settings_.editor.giMode == modes[i]) mode = i;
			const auto labels = context.text.Text("settings.giOff") + '\0' + "DDGI" + '\0' + "RTGI" + '\0' + context.text.Text("settings.giBaked") + '\0';
			if(ImGui::Combo(context.text.Label("settings.giMode", "giMode").c_str(), &mode, labels.c_str())) {
				settings_.editor.giMode = modes[mode];
				changed					= true;
			}
			ImGui::TextWrapped("%s", context.text.Text("settings.giHelp").c_str());
		}
		ImGui::EndChild();
		ImGui::EndTable();
		if(changed) changed_();
	}
	void ViewportPanel::Draw(const EditorContext& context) {
		static constexpr const char* states[] = {"runtime.disconnected", "runtime.starting", "runtime.connected", "runtime.running", "runtime.paused", "runtime.stopping", "runtime.error"};
		auto						 state	  = static_cast<size_t>(session_.State());
		// 操作ボタンは共通ツールバーで描画する。Panelは映像とオーバーレイに領域を使う。
		if(!session_.IsLocalPreview()) {
			ImGui::TextUnformatted(context.text.Text(states[state < 7 ? state : 6]).c_str());
		}
		const auto size = ImGui::GetContentRegionAvail();
		if(size.x < 1 || size.y < 1) {
			session_.Resize(0, 0);
			return;
		}
		session_.Resize(static_cast<uint32_t>(std::min(size.x, 16384.0f)), static_cast<uint32_t>(std::min(size.y, 16384.0f)));
		if(auto image = session_.Image(); image && image->IsValid()) {
			const auto origin = ImGui::GetCursorScreenPos();
			ImGui::Image(static_cast<ImTextureID>(image->nativeId), size);
			if(session_.IsLocalPreview()) {
				const auto	 imageEnd = ImGui::GetCursorScreenPos();
				const float	 padding  = ImGui::GetStyle().WindowPadding.x;
				const float	 line	  = ImGui::GetTextLineHeightWithSpacing();
				const float	 header	  = ImGui::GetFrameHeightWithSpacing();
				const float	 requestedWidths[]{220, 320, 480};
				float		 previewWidth = std::min(requestedWidths[tools_.PreviewSize()] * ImGui::GetFontSize() / 16,
													 std::min(size.x * .48f, (size.y - header - padding * 4) * 16 / 9));
				const bool	 showPreview  = context.session.GetCameraPreview().IsValid() && previewWidth >= 120;
				const ImVec2 previewSize{previewWidth, previewWidth * 9 / 16 + header + padding * 2};
				const ImVec2 previewOrigin{origin.x + size.x - previewSize.x - padding, origin.y + size.y - previewSize.y - padding};
				session_.SetCameraPreviewVisible(showPreview);

				// 操作通知は短時間、問題の診断は解消されるまで表示する。
				std::vector<ViewportDiagnostics::Item> messages;
				if(tools_.ShowDiagnostics()) {
					for(const auto& [key, item] : context.session.Diagnostics().Items()) {
						static_cast<void>(key);
						messages.push_back(item);
					}
					if(logs_)
						if(auto snapshot = logs_->SnapshotIfChanged(logRevision_)) records_ = std::move(*snapshot);
					const auto cutoff = std::chrono::system_clock::now() - std::chrono::seconds(8);
					for(auto it = records_.rbegin(); it != records_.rend(); ++it) {
						if(it->timestamp < cutoff || it->level < LogLevel::Warning) continue;
						const std::string message = "[" + it->category + "] " + it->message;
						if(std::ranges::none_of(messages, [&](const auto& item) { return item.text == message; }))
							messages.push_back({message, it->level >= LogLevel::Error});
					}
					std::stable_sort(messages.begin(), messages.end(), [](const auto& a, const auto& b) { return a.error > b.error; });
				}
				const size_t shown			 = std::min<size_t>(4, messages.size());
				const float	 diagnosticWidth = std::min(size.x - padding * 2, ImGui::GetFontSize() * 38);
				const ImVec2 diagnosticOrigin{origin.x + padding, origin.y + padding};
				const ImVec2 diagnosticSize{diagnosticWidth, line * static_cast<float>(shown) + header + padding * 2};
				const bool	 showMessages = shown && diagnosticWidth >= 120 && diagnosticSize.y < size.y * .5f;
				const auto	 mouse		  = ImGui::GetMousePos();
				const auto	 inside		  = [&](ImVec2 p, ImVec2 extent) {
					return mouse.x >= p.x && mouse.y >= p.y && mouse.x < p.x + extent.x && mouse.y < p.y + extent.y;
				};
				// 子パネルを描く前に画像のhoverを使い、オーバーレイから背後への入力を遮断する。
				tools_.DrawOverlay(origin.x, origin.y, size.x, size.y,
								   (showPreview && inside(previewOrigin, previewSize)) || (showMessages && inside(diagnosticOrigin, diagnosticSize)));
				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(.035f, .045f, .06f, .94f));
				ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6);
				const auto flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
				if(showMessages) {
					ImGui::SetCursorScreenPos(diagnosticOrigin);
					ImGui::BeginChild("ViewportDiagnostics", diagnosticSize, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, flags);
					for(size_t i = 0; i < shown; ++i) {
						auto message = messages[i].text;
						std::replace(message.begin(), message.end(), '\n', ' ');
						std::replace(message.begin(), message.end(), '\r', ' ');
						ImGui::TextColored(messages[i].error ? ImVec4(1, .32f, .3f, 1) : ImVec4(1, .78f, .25f, 1),
										   "%s %s", context.text.Text(messages[i].error ? "log.error" : "log.warning").c_str(), message.c_str());
						if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s", messages[i].text.c_str());
					}
					if(ImGui::SmallButton(context.text.Text("viewport.openLog").c_str())) context.commands.Execute("viewport.openLog");
					if(messages.size() > shown) {
						ImGui::SameLine();
						ImGui::TextDisabled("+%zu", messages.size() - shown);
					}
					ImGui::EndChild();
				}
				if(showPreview) {
					ImGui::SetCursorScreenPos(previewOrigin);
					ImGui::BeginChild("CameraPreview", previewSize, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, flags);
					auto*		level	   = context.session.GetPreviewLevel();
					const auto	entity	   = level ? level->FindEntity(context.session.GetCameraPreview()) : Entity{};
					const auto* name	   = entity.GetComponent<NameComponent>();
					const auto* camera	   = entity.GetComponent<CameraComponent>();
					const float closeWidth = std::max(ImGui::GetFrameHeight(), ImGui::CalcTextSize("X").x + ImGui::GetStyle().FramePadding.x * 2);
					ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - closeWidth - ImGui::GetStyle().ItemSpacing.x));
					if(ImGui::BeginCombo("##PreviewCamera", name ? name->name.c_str() : "Camera")) {
						if(auto* editLevel = context.session.GetLevel(); editLevel && editLevel->GetScene())
							editLevel->GetScene()->ForEach<CameraComponent, PersistentIdComponent>([&](Entity candidate, CameraComponent&, PersistentIdComponent& id) {
								const auto* candidateName = candidate.GetComponent<NameComponent>();
								const auto	label		  = (candidateName ? candidateName->name : "Camera") + "###" + std::to_string(id.value.high) + ":" + std::to_string(id.value.low);
								if(ImGui::Selectable(label.c_str(), id.value == context.session.GetCameraPreview())) context.session.SetCameraPreview(id.value);
							});
						ImGui::EndCombo();
					}
					ImGui::SameLine();
					if(ImGui::Button("X##StopPreview", {closeWidth, 0})) context.session.ClearCameraPreview();
					if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s", context.text.Text("viewport.previewStop").c_str());
					const auto	 available = ImGui::GetContentRegionAvail();
					const float	 w		   = std::min(available.x, available.y * 16 / 9);
					const ImVec2 position  = ImGui::GetCursorScreenPos();
					if(auto preview = session_.CameraPreviewImage(); preview && preview->IsValid() && w > 0) {
						ImGui::SetCursorScreenPos({position.x + (available.x - w) * .5f, position.y + (available.y - w * 9 / 16) * .5f});
						ImGui::Image(static_cast<ImTextureID>(preview->nativeId), {w, w * 9 / 16});
						if(camera && !camera->enabled) ImGui::GetWindowDrawList()->AddText({position.x + 4, position.y + 4}, IM_COL32(255, 200, 65, 255), context.text.Text("viewport.cameraDisabled").c_str());
					} else
						ImGui::TextWrapped("%s", context.text.Text("viewport.previewPending").c_str());
					ImGui::EndChild();
				}
				ImGui::PopStyleVar();
				ImGui::PopStyleColor();
				ImGui::SetCursorScreenPos(imageEnd);
				ImGui::Dummy({0, 0});
				return;
			}
			if(ImGui::IsItemHovered()) {
				const auto mouse = ImGui::GetMousePos();
				float	   x = (mouse.x - origin.x) / size.x, y = (mouse.y - origin.y) / size.y;
				session_.Input(0, 0, x, y);
				for(uint32_t button = 0; button < 3; ++button) {
					if(ImGui::IsMouseClicked(button)) session_.Input(1, button, x, y);
					if(ImGui::IsMouseReleased(button)) session_.Input(2, button, x, y);
				}
			}
		} else
			ImGui::TextWrapped("%s", context.text.Text(session_.IsLocalPreview() ? "viewport.localUnavailable" : "viewport.unavailable").c_str());
	}
} // namespace NexusEngine
