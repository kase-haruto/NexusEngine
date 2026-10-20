#include "UtilityPanels.h"
#include "Editor/Gui/Widgets/SearchBar.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Services/SelectionService.h"
#include "ThirdParty/imgui/imgui.h"
#include <cstdio>
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace NexusEngine {
namespace {
std::string LogLine(const OwnedLogRecord& record, const LocalizationService& text) {
    auto time = std::chrono::system_clock::to_time_t(record.timestamp);
    std::tm local{}; localtime_s(&local, &time);
    static constexpr const char* keys[] = {"log.trace", "log.debug", "log.info", "log.warning", "log.error", "log.critical"};
    auto level = static_cast<size_t>(record.level);
    std::ostringstream out;
    out << std::put_time(&local, "%H:%M:%S") << " [" << text.Text(keys[level < 6 ? level : 5]) << "] ["
        << record.category << "] " << record.message << " (" << record.source << ')';
    return out.str();
}
}
void OutputLogPanel::Draw(const EditorContext& context) {
    if(ImGui::Button(context.text.Label("clear", "clear").c_str())) buffer_->Clear();
    ImGui::SameLine();
    const bool copy = ImGui::Button(context.text.Label("copy", "copy").c_str());
    ImGui::SameLine(); ImGui::Checkbox(context.text.Label("log.autoscroll", "auto").c_str(), &autoScroll_);
    UI::SearchBar("OutputLogSearch", search_, context.text.Text("search").c_str());
    static constexpr const char* keys[] = {"log.trace", "log.debug", "log.info", "log.warning", "log.error", "log.critical"};
    for(size_t i = 0; i < levels_.size(); ++i) {
        if(i) ImGui::SameLine();
        ImGui::Checkbox(context.text.Label(keys[i], keys[i]).c_str(), &levels_[i]);
    }
    // mutex下でコピーしたsnapshotだけを描く。UI処理中はログthreadを妨げない。
    const auto records = buffer_->Snapshot();
    std::string clipboard;
    ImGui::BeginChild("records", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1;
    for(const auto& record : records) {
        auto level = static_cast<size_t>(record.level);
        if(level >= levels_.size() || !levels_[level]) continue;
        auto line = LogLine(record, context.text);
        if(!search_.empty() && line.find(search_) == line.npos) continue;
        ImGui::TextUnformatted(line.c_str());
        if(copy) clipboard += line + '\n';
    }
    if(autoScroll_ && atBottom) ImGui::SetScrollHereY(1);
    ImGui::EndChild();
    if(copy) ImGui::SetClipboardText(clipboard.c_str());
}
void TerminalPanel::Draw(const EditorContext& context) {
    ImGui::BeginChild("results", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2));
    for(const auto& line : output_) ImGui::TextUnformatted(line.c_str());
    ImGui::EndChild();
    const auto callback = [](ImGuiInputTextCallbackData* data) -> int {
        auto& self = *static_cast<TerminalPanel*>(data->UserData);
        std::string replacement;
        if(data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
            const auto& history = self.console_.History();
            if(history.empty()) return 0;
            if(data->EventKey == ImGuiKey_UpArrow) {
                if(self.historyIndex_ < 0) self.historyIndex_ = static_cast<int>(history.size()) - 1;
                else if(self.historyIndex_ > 0) --self.historyIndex_;
            } else if(self.historyIndex_ >= 0 && ++self.historyIndex_ >= static_cast<int>(history.size())) self.historyIndex_ = -1;
            if(self.historyIndex_ >= 0) replacement = history[self.historyIndex_];
        } else if(data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
            auto options = self.console_.Complete(std::string_view(data->Buf, data->CursorPos));
            if(options.size() != 1) return 0;
            replacement = options.front() + ' ';
        } else return 0;
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, replacement.c_str()); return 0;
    };
    bool execute = ImGui::InputText(context.text.Label("console.input", "input").c_str(), input_.data(), input_.size(),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCompletion, callback, this);
    ImGui::SameLine(); execute |= ImGui::Button(context.text.Label("console.execute", "execute").c_str());
    if(execute && input_[0]) {
        output_.emplace_back(std::string("> ") + input_.data());
        auto result = console_.Execute(input_.data());
        if(!result.text.empty()) output_.push_back(result.translate ? context.text.Text(result.text) : result.text.substr(0, 8192));
        while(output_.size() > 256) output_.pop_front();
        input_.fill(0); historyIndex_ = -1;
    }
    ImGui::TextUnformatted(context.text.Text("console.help").c_str());
}
void ContentBrowserPanel::Draw(const EditorContext& context) {
    if(ImGui::Button(context.text.Label("asset.up", "up").c_str())) assets_.Navigate(assets_.Current().parent_path());
    ImGui::SameLine(); ImGui::TextUnformatted(AssetCatalog::Utf8(assets_.Current()).c_str());
    UI::SearchBar("ContentBrowserSearch", search_, context.text.Text("search").c_str());
    if(!ImGui::BeginTable("browser", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) return;
    ImGui::TableSetupColumn("folders", ImGuiTableColumnFlags_WidthFixed, 180);
    ImGui::TableSetupColumn("assets", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow(); ImGui::TableNextColumn();
    std::optional<std::filesystem::path> navigate;
    std::function<void(const std::filesystem::path&, size_t)> tree = [&](const auto& path, size_t depth) {
        auto id = AssetCatalog::Utf8(path);
        ImGui::PushID(id.c_str());
        bool open = ImGui::TreeNodeEx("folder", ImGuiTreeNodeFlags_OpenOnArrow, "%s", AssetCatalog::Utf8(path.filename()).c_str());
        if(ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) navigate = path;
        if(open) {
            if(depth < 12) for(const auto& entry : assets_.List(path)) if(entry.directory) tree(entry.path, depth + 1);
            ImGui::TreePop();
        }
        ImGui::PopID();
    };
    tree(assets_.Root(), 0);
    ImGui::TableNextColumn();
    for(const auto& entry : assets_.List(assets_.Current())) {
        if(!search_.empty() && entry.name.find(search_) == entry.name.npos) continue;
        auto path = AssetCatalog::Utf8(entry.path);
        ImGui::PushID(path.c_str());
        auto label = entry.name + "  [" + context.text.Text(entry.typeKey) + ']';
        if(ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) {
            if(entry.directory) navigate = entry.path; else if(open_) open_(entry.path);
        }
        if(!entry.directory && ImGui::BeginDragDropSource()) {
            // OS handleやResource pointerでなくUTF-8 pathをコピーする拡張用payload。
            ImGui::SetDragDropPayload("NEXUS_ASSET_PATH", path.c_str(), path.size() + 1);
            ImGui::TextUnformatted(entry.name.c_str()); ImGui::EndDragDropSource();
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
    if(navigate) assets_.Navigate(*navigate);
}
void PreferencesPanel::Draw(const EditorContext& context) {
    bool changed = false;
    int language = settings_.user.language == "ja" ? 1 : 0;
    const auto languages = context.text.Text("language.english") + '\0' + context.text.Text("language.japanese") + '\0';
    if(ImGui::Combo(context.text.Label("settings.language", "language").c_str(), &language, languages.c_str())) {
        settings_.user.language = language == 1 ? "ja" : "en"; changed = true;
    }
    int theme = settings_.user.theme == "Light" ? 1 : 0;
    const auto themes = context.text.Text("theme.dark") + '\0' + context.text.Text("theme.light") + '\0';
    if(ImGui::Combo(context.text.Label("settings.theme", "theme").c_str(), &theme, themes.c_str())) {
        settings_.user.theme = theme ? "Light" : "Dark"; changed = true;
    }
    changed |= ImGui::SliderFloat(context.text.Label("settings.font", "font").c_str(), &settings_.user.fontSize, 10, 40);
    changed |= ImGui::SliderFloat(context.text.Label("settings.scale", "scale").c_str(), &settings_.user.uiScale, 0.5f, 3);
    changed |= ImGui::Checkbox(context.text.Label("settings.layout", "layout").c_str(), &settings_.user.autoSaveLayout);
    if(changed) changed_();
}
void ViewportPanel::Draw(const EditorContext& context) {
    static constexpr const char* states[] = {"runtime.disconnected", "runtime.starting", "runtime.connected", "runtime.running", "runtime.paused", "runtime.stopping", "runtime.error"};
    auto state = static_cast<size_t>(session_.State());
    ImGui::TextUnformatted(context.text.Text(session_.IsLocalPreview() ? "runtime.localPreview" : states[state < 7 ? state : 6]).c_str());
    const auto size = ImGui::GetContentRegionAvail();
    if(size.x < 1 || size.y < 1) { session_.Resize(0, 0); return; }
    session_.Resize(static_cast<uint32_t>(std::min(size.x, 16384.0f)), static_cast<uint32_t>(std::min(size.y, 16384.0f)));
    if(auto image = session_.Image(); image && image->IsValid()) {
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::Image(static_cast<ImTextureID>(image->nativeId), size);
        if(ImGui::IsItemHovered()) {
            const auto mouse = ImGui::GetMousePos();
            float x = (mouse.x - origin.x) / size.x, y = (mouse.y - origin.y) / size.y;
            session_.Input(0, 0, x, y);
            for(uint32_t button = 0; button < 3; ++button) {
                if(ImGui::IsMouseClicked(button)) session_.Input(1, button, x, y);
                if(ImGui::IsMouseReleased(button)) session_.Input(2, button, x, y);
            }
        }
    } else ImGui::TextWrapped("%s", context.text.Text(session_.IsLocalPreview() ? "viewport.localUnavailable" : "viewport.unavailable").c_str());
}
}
