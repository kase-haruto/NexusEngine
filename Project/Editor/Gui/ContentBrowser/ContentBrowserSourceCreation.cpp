#include "ContentBrowserPanel.h"
#include "Editor/Gui/Icons/EditorIcons.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Services/EditorCommandRegistry.h"
#include <cstdio>

namespace NexusEngine {
void ContentBrowserPanel::RequestSourceClone(const std::filesystem::path &file) {
    const bool cpp = file.extension() != ".cs";
    RequestSource(cpp, file.parent_path());
    sourceClone_ = file;
    auto declaration = file;
    if (cpp && file.extension() == ".cpp") {
        auto header = file;
        header.replace_extension(".h");
        std::error_code error;
        if (std::filesystem::is_regular_file(header, error)) {
            declaration = header;
        }
    }
    const auto metadata = ContentMetadataProvider::Read(declaration, sources_->Root(), false);
    sourceCloneClass_ =
        metadata && !metadata->className.empty() ? metadata->className : AssetCatalog::Utf8(file.stem());
    const auto text = ContentMetadataProvider::Text(declaration);
    const bool behaviour = text && (text->find("ScriptType") != std::string::npos ||
                                    text->find("EntityBehaviourContext") != std::string::npos);
    sourceKind_ = behaviour ? 0 : 1;
    std::error_code error;
    if (cpp && !behaviour) {
        auto pair = file;
        pair.replace_extension(file.extension() == ".h" ? ".cpp" : ".h");
        if (!std::filesystem::is_regular_file(pair, error)) {
            sourceKind_ = file.extension() == ".h" ? 2 : 3;
        }
    }
    std::snprintf(sourceName_.data(), sourceName_.size(), "%s_Copy", sourceCloneClass_.c_str());
}

void ContentBrowserPanel::RequestSource(bool cpp, const std::filesystem::path &hint) {
    sourceClone_.clear();
    sourceCloneClass_.clear();
    sourceCpp_ = cpp;
    sourceRequested_ = true;
    sourceModule_ = 0;
    sourceModules_.clear();
    sourceName_.fill(0);
    std::snprintf(sourceName_.data(), sourceName_.size(), "%s", cpp ? "NewBehaviour" : "NewScript");
    sourceError_.clear();
    sourceNamespace_.fill(0);
    sourceBase_.fill(0);
    sourceBaseHeader_.fill(0);
    sourceDirectory_.fill(0);
    sourceKind_ = 0;
    const auto modules = creation_->Modules(cpp);
    if (!modules) {
        sourceError_ = modules.error().GetMessageText();
        return;
    }
    sourceModules_ = *modules;
    // Source内の右クリックでは、そのfolderを所有するModuleを初期選択する。
    for (size_t index = 0; index < sourceModules_.size(); ++index) {
        auto cursor = hint.lexically_normal();
        while (!cursor.empty()) {
            if (cursor == sourceModules_[index].directory.lexically_normal()) {
                sourceModule_ = static_cast<int>(index);
                const auto relative = hint.lexically_relative(sourceModules_[index].directory);
                if (relative != ".") {
                    std::snprintf(sourceDirectory_.data(), sourceDirectory_.size(), "%s",
                                  AssetCatalog::Utf8(relative).c_str());
                }
                return;
            }
            const auto parent = cursor.parent_path();
            if (parent == cursor) {
                break;
            }
            cursor = parent;
        }
    }
}
void ContentBrowserPanel::DrawCreateMenu(const EditorContext &context, const std::filesystem::path &directory) {
    const bool available = creation_ && creation_->Available();
    const auto root = services_->RootFor(directory);
    const bool enabled = available && creation_->CanCreate() && !context.session.IsPlaying() &&
                         (!root || !services_->roots[*root].readOnly);
    if (ImGui::BeginMenu(context.text.Text("browser.create").c_str(), enabled)) {
        if (UI::IconMenuItem("source_csharp", context.text.Text("source.create.csharp").c_str())) {
            RequestSource(false, directory);
        }
        if (UI::IconMenuItem("source_cpp", context.text.Text("source.create.cpp").c_str())) {
            RequestSource(true, directory);
        }
        ImGui::EndMenu();
    }
    if (!available) {
        ImGui::TextDisabled("%s", context.text.Text("source.create.projectRequired").c_str());
    }
}
void ContentBrowserPanel::DrawSourceCreation(const EditorContext &context) {
    if (creation_ && sourceRevision_ != creation_->Revision()) {
        sourceRevision_ = creation_->Revision();
        sourceError_ = creation_->ErrorText();
        if (waitingForCreation_ && !creation_->CreatedFile().empty()) {
            // workerはImGui/catalogを操作しない。成功したframe境界で一覧を更新する。
            if (sources_) {
                sources_->Refresh();
                sources_->Navigate(creation_->CreatedFile().parent_path());
                Navigate(1, sources_->Current());
            }
            if (sourceOpen_ && open_) {
                Open(creation_->CreatedFile());
            }
        }
        waitingForCreation_ = false;
    }

    const auto title = context.text.Label("source.create.title", "CreateSource");
    if (sourceRequested_) {
        sourceRequested_ = false;
        ImGui::OpenPopup(title.c_str());
    }
    if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(context.text.Text(sourceCpp_ ? "source.create.cpp" : "source.create.csharp").c_str());
        ImGui::TextWrapped("%s", context.text.Text("source.create.help").c_str());
        if (sourceModules_.empty()) {
            ImGui::TextWrapped("%s", context.text.Text("source.create.noModule").c_str());
            if (ImGui::Button(context.text.Text("source.create.modules").c_str()) &&
                context.commands.Execute("project.development")) {
                ImGui::CloseCurrentPopup();
            }
        } else {
            const auto &module = sourceModules_[static_cast<size_t>(sourceModule_)];
            ImGui::SetNextItemWidth(360);
            if (ImGui::BeginCombo(context.text.Text("source.create.module").c_str(), module.id.c_str())) {
                for (size_t index = 0; index < sourceModules_.size(); ++index) {
                    if (ImGui::Selectable(sourceModules_[index].id.c_str(), sourceModule_ == static_cast<int>(index))) {
                        sourceModule_ = static_cast<int>(index);
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(360);
            ImGui::InputText(context.text.Label("source.create.name", "SourceName").c_str(), sourceName_.data(),
                             sourceName_.size());
            const char *kinds[]{"Behaviour", "Class", "Header", "Source"};
            ImGui::Combo(context.text.Text("cb.sourceKind").c_str(), &sourceKind_, kinds, sourceCpp_ ? 4 : 2);
            ImGui::InputText("Namespace", sourceNamespace_.data(), sourceNamespace_.size());
            ImGui::InputText(context.text.Text("cb.baseClass").c_str(), sourceBase_.data(), sourceBase_.size());
            if (sourceCpp_) {
                ImGui::InputText(context.text.Text("cb.baseHeader").c_str(), sourceBaseHeader_.data(),
                                 sourceBaseHeader_.size());
            }
            ImGui::InputText(context.text.Text("cb.sourceDirectory").c_str(), sourceDirectory_.data(),
                             sourceDirectory_.size());
            const auto &selected = sourceModules_[static_cast<size_t>(sourceModule_)];
            const auto file =
                selected.directory / std::filesystem::path(reinterpret_cast<const char8_t *>(sourceDirectory_.data())) /
                (std::string(sourceName_.data()) + (sourceCpp_ ? (sourceKind_ == 2 ? ".h" : ".cpp") : ".cs"));
            ImGui::TextWrapped("%s: %s", context.text.Text("source.create.destination").c_str(),
                               AssetCatalog::Utf8(file).c_str());
            ImGui::Checkbox(context.text.Text("source.create.open").c_str(), &sourceOpen_);
            ImGui::BeginDisabled(context.session.IsPlaying() || !creation_ || !creation_->CanCreate());
            if (ImGui::Button(context.text.Label("source.create.confirm", "CreateSourceConfirm").c_str())) {
                auto result = creation_->Create(
                    selected, sourceName_.data(),
                    SourceCreationOptions{kinds[sourceKind_], sourceNamespace_.data(), sourceBase_.data(),
                                          sourceBaseHeader_.data(), sourceDirectory_.data(),
                                          sourceClone_.empty()
                                              ? std::string{}
                                              : AssetCatalog::Utf8(sourceClone_.lexically_relative(selected.directory)),
                                          sourceCloneClass_});
                if (result) {
                    waitingForCreation_ = true;
                    sourceError_.clear();
                    ImGui::CloseCurrentPopup();
                } else {
                    sourceError_ = result.error().GetMessageText();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ImGui::Button(context.text.Text("cancel").c_str())) {
            ImGui::CloseCurrentPopup();
        }
        if (!sourceError_.empty()) {
            ImGui::TextWrapped("%s", sourceError_.c_str());
        }
        ImGui::EndPopup();
    }
    // 非同期生成失敗も、modalを閉じた後にBrowser内へ残して通知する。
    if (!sourceError_.empty()) {
        ImGui::TextWrapped("%s", sourceError_.c_str());
    }
}

} // namespace NexusEngine
