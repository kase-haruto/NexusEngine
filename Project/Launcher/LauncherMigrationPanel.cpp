#include "LauncherMigrationPanel.h"
#include "Platform/Window/FileDialog.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Foundation/Utility/String/ConvertString.h"
#include "imgui.h"
#include <algorithm>
#include <utility>
#include <stdexcept>

namespace NexusEngine {
namespace {
std::string Utf8(const std::filesystem::path& path) { const auto bytes = path.generic_u8string(); return {reinterpret_cast<const char*>(bytes.data()),bytes.size()}; }
void Copy(std::array<char,4096>& buffer, const std::filesystem::path& file) {
    const auto text = Utf8(file);
    if(text.size() >= buffer.size()) throw std::runtime_error("Recovery path exceeds UI input limit.");
    std::copy(text.begin(),text.end(),buffer.begin()); buffer[text.size()] = 0;
}
}
void LauncherMigrationPanel::Open(std::filesystem::path project, std::filesystem::path target) {
    project_ = std::move(project); requestedTarget_ = std::move(target); selected_ = -1; accept_ = false; open_ = true;
    review_ = std::make_shared<std::optional<ProjectMigrationPlan>>(); applied_ = std::make_shared<std::optional<std::filesystem::path>>();
    restored_ = std::make_shared<bool>(false); recovery_[0] = 0; message_.clear();
}
void LauncherMigrationPanel::Review(const std::filesystem::path& target, const Submit& submit) {
    review_ = std::make_shared<std::optional<ProjectMigrationPlan>>(); accept_ = false; message_.clear();
    submit("Reviewing Engine compatibility...",[file = project_, target, output = review_](std::stop_token) -> Result<void> {
        auto plan = ProjectMigration::Prepare(file,target); if(!plan) return std::unexpected(plan.error());
        *output = std::move(*plan); return {};
    });
}
void LauncherMigrationPanel::Draw(HWND owner, std::span<const RegisteredEngine* const> engines, bool busy, const std::string& operationError, const Submit& submit) {
    if(open_) { ImGui::OpenPopup("Change Project Engine"); open_ = false; }
    const auto size = ImGui::GetMainViewport()->WorkSize; const auto scale = ImGui::GetStyle().FontScaleDpi;
    const ImVec2 maximum{std::max(160.0f,size.x - 32),std::max(160.0f,size.y - 32)};
    ImGui::SetNextWindowSizeConstraints({std::min(480 * scale,maximum.x),std::min(240 * scale,maximum.y)},maximum);
    ImGui::SetNextWindowSize({std::max(160.0f,std::min(860 * scale,size.x - 32)),std::max(160.0f,std::min(680 * scale,size.y - 32))},ImGuiCond_Appearing);
    if(!ImGui::BeginPopupModal("Change Project Engine",nullptr,ImGuiWindowFlags_NoSavedSettings)) return;
    // future.get後にだけ結果を読む。submit後の同一frameではworkerが書くboxへ触らない。
    auto plan = !busy ? *review_ : std::optional<ProjectMigrationPlan>{};
    if(!busy && *applied_) {
        Copy(recovery_,**applied_); applied_->reset(); review_->reset(); plan.reset(); accept_ = false;
        message_ = "Engine requirement changed. Rebuild and validate GameRuntime and GameEditor. Recovery receipt is shown below.";
    }
    if(!busy && *restored_) { *restored_ = false; review_->reset(); plan.reset(); accept_ = false; message_ = "Original Descriptor restored. Rebuild with its Engine before opening the project."; }
    ImGui::TextWrapped("Project: %s",Utf8(project_).c_str());
    ImGui::TextWrapped("Close this project's Editor and Runtime before changing its Engine.");
    ImGui::BeginDisabled(busy);
    const bool hasSelection = selected_ >= 0 && selected_ < static_cast<int>(engines.size());
    const std::string selection = hasSelection ? engines[selected_]->identity.id + " / " + engines[selected_]->identity.version + " / " + engines[selected_]->configuration : "Explicitly select a target Engine";
    if(ImGui::BeginCombo("Target Engine",selection.c_str())) {
        for(size_t index = 0; index < engines.size(); ++index) {
            const auto& engine = *engines[index];
            const auto label = engine.identity.id + " / " + engine.identity.version + " / " + engine.identity.association + " / " + engine.configuration + "##" + std::to_string(index);
            ImGui::BeginDisabled(!engine.installation);
            if(ImGui::Selectable(label.c_str(),selected_ == static_cast<int>(index))) { selected_ = static_cast<int>(index); review_->reset(); plan.reset(); accept_ = false; }
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    const bool valid = selected_ >= 0 && selected_ < static_cast<int>(engines.size()) && engines[selected_]->installation.has_value();
    if(plan && (!valid || !SameFilePath(plan->targetManifest,engines[selected_]->manifest))) { plan.reset(); review_->reset(); accept_ = false; }
    ImGui::BeginDisabled(!valid);
    if(ImGui::Button("Review compatibility")) { Review(engines[selected_]->manifest,submit); plan.reset(); }
    ImGui::EndDisabled();
    if(!busy && !requestedTarget_.empty()) {
        const auto target = std::exchange(requestedTarget_,{});
        bool found = false;
        for(size_t index = 0; index < engines.size(); ++index) if(SameFilePath(engines[index]->manifest,target) && engines[index]->installation) {
            selected_ = static_cast<int>(index); Review(engines[index]->manifest,submit); found = true; break;
        }
        if(!found) message_ = "The explicit target is not a valid registered Engine. Register it first; no other Engine was selected.";
    }
    if(plan) {
        ImGui::Separator();
        ImGui::TextWrapped("Before: %s / %s / %s",plan->previous.id.c_str(),plan->previous.version.c_str(),plan->previous.association.c_str());
        ImGui::TextWrapped("After: %s / %s / %s",plan->target.id.c_str(),plan->target.version.c_str(),plan->target.association.c_str());
        for(const auto& note : plan->notes) ImGui::TextWrapped("%s",note.c_str());
        ImGui::Checkbox("##accept migration",&accept_); ImGui::SameLine();
        ImGui::TextWrapped("I closed the project and accept rebuilding and checking API/importer compatibility.");
        if(ImGui::IsItemClicked()) accept_ = !accept_;
        ImGui::BeginDisabled(!accept_);
        if(ImGui::Button("Apply reviewed Engine change")) {
            applied_ = std::make_shared<std::optional<std::filesystem::path>>();
            submit("Backing up and changing Engine...",[plan = *plan, output = applied_](std::stop_token) -> Result<void> {
                auto result = ProjectMigration::Apply(plan,true); if(!result) return std::unexpected(result.error()); *output = *result; return {};
            });
        }
        ImGui::EndDisabled();
    }
    ImGui::Separator();
    ImGui::TextWrapped("Recovery changes only the Descriptor and refuses to overwrite later edits.");
    ImGui::InputText("Recovery.json",recovery_.data(),recovery_.size());
    if(ImGui::Button("Browse recovery...")) {
        auto file = FileDialog::Recovery(owner);
        if(!file) message_ = file.error().GetMessageText(); else if(*file) Copy(recovery_,**file);
    }
    ImGui::SameLine(); ImGui::BeginDisabled(!recovery_[0]);
    if(ImGui::Button("Restore original Engine requirement")) {
        restored_ = std::make_shared<bool>(false);
        submit("Restoring original Descriptor...",[file = project_, receipt = std::filesystem::path(ConvertString(std::string(recovery_.data()))), output = restored_](std::stop_token) -> Result<void> {
            auto result = ProjectMigration::Restore(file,receipt); if(!result) return result; *output = true; return {};
        });
    }
    ImGui::EndDisabled();
    if(ImGui::Button("Close review")) ImGui::CloseCurrentPopup();
    ImGui::EndDisabled();
    if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
    if(!operationError.empty()) { ImGui::PushStyleColor(ImGuiCol_Text,{1,0.55f,0.5f,1}); ImGui::TextWrapped("%s",operationError.c_str()); ImGui::PopStyleColor(); }
    if(busy) ImGui::TextWrapped("Operation in progress...");
    ImGui::EndPopup();
}
}
