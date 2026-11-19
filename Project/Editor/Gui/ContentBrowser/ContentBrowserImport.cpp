#include "ContentBrowserPanel.h"
#include "Editor/Services/ContentBrowserActions.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/PropertyEditService.h"
#include "Editor/Gui/Widgets/AssetPropertyWidget.h"
#include "ThirdParty/nlohmann/json.hpp"
#include "Platform/FileSystem/AtomicFile.h"
#include "Foundation/Utility/String/ConvertString.h"

namespace NexusEngine {
void ContentBrowserPanel::RequestImport(const std::filesystem::path &reimport) {
    if (import_.Busy()) {
        return;
    }
    import_.Cancel();
    importSources_.clear();
    importOptions_ = {};
    importDestination_ = roots_[rootIndex_].current;
    if (!reimport.empty()) {
        // 外部の絶対パスは共有assetに保存せず、利用者別の設定だけに記録する。
        const auto reference = services_->Reference(reimport);
        const auto origin = reference ? services_->Origin(*reference) : std::optional<ContentImportOrigin>{};
        if (!origin) {
            error_ = "No local reimport origin. Use Import and enable Replace to select an origin.";
            return;
        }
        importSources_.push_back(origin->source);
        importOptions_.dependencies = origin->dependencies;
        const auto current = Sha256File(reimport);
        if (current && *current != origin->hash) {
            error_ = "This asset was edited locally after import. Review the replacement before applying.";
        }
        importDestination_ = reimport.parent_path();
        importOptions_.replace = true;
        importOptions_.primaryName = reimport.filename();
    } else {
        const auto files = ContentBrowserActions::ChooseImportFiles();
        if (!files) {
            error_ = files.error().GetMessageText();
            return;
        }
        importSources_ = *files;
    }
    importRequested_ = !importSources_.empty();
}
void ContentBrowserPanel::DrawImport(const EditorContext &context) {
    import_.Poll();
    if (importRequested_) {
        importRequested_ = false;
        ImGui::OpenPopup("ContentImportReview");
    }
    if (!ImGui::BeginPopupModal("ContentImportReview", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextUnformatted(context.text.Text("cb.import").c_str());
    ImGui::TextWrapped("%s", AssetCatalog::Utf8(importDestination_).c_str());
    ImGui::BeginDisabled(import_.Busy() || import_.Plan().has_value());
    ImGui::Checkbox(context.text.Text("cb.importDependencies").c_str(), &importOptions_.dependencies);
    ImGui::Checkbox(context.text.Text("cb.importReplace").c_str(), &importOptions_.replace);
    ImGui::EndDisabled();
    if (import_.Plan()) {
        ImGui::BeginChild("ImportFiles", {560, 200}, ImGuiChildFlags_Borders);
        for (const auto &file : import_.Plan()->files) {
            ImGui::TextWrapped("%s%s", file.existed ? "Replace: " : "Add: ",
                               AssetCatalog::Utf8(file.target.lexically_relative(import_.Plan()->root)).c_str());
        }
        ImGui::EndChild();
        ImGui::BeginDisabled(context.session.IsPlaying());
        if (ImGui::Button(context.text.Text("cb.apply").c_str())) {
            struct PendingOrigin {
                std::filesystem::path source, target;
                std::string sourceHash;
            };
            std::vector<PendingOrigin> origins;
            // Origin登録のためだけに大きい画像・backup byte列をUI threadで複製しない。
            for (const auto &file : import_.Plan()->files) {
                origins.push_back({file.source, file.target, file.sourceHash});
            }
            const auto root = services_->RootFor(importDestination_);
            const auto result = root ? import_.Publish(*services_->roots[*root].catalog, services_->documents,
                                                       context.properties.History(), fileRevision_)
                                     : Result<void>{std::unexpected(
                                           Error(ErrorCategory::Resource, 299, "Import destination is unavailable."))};
            if (!result) {
                error_ = result.error().GetMessageText();
            } else {
                std::vector<std::pair<ContentReference, ContentImportOrigin>> recorded;
                for (const auto &file : origins) {
                    const auto reference = services_->Reference(file.target);
                    if (reference) {
                        recorded.push_back({*reference, {file.source, file.sourceHash, importOptions_.dependencies}});
                    }
                }
                const auto remembered = services_->RememberOrigins(recorded);
                if (!remembered) {
                    error_ = remembered.error().GetMessageText();
                }
                if (context.assets) {
                    context.assets->RefreshPreviews();
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
    } else if (import_.Busy()) {
        ImGui::TextUnformatted(context.text.Text("cb.importPreparing").c_str());
    } else if (ImGui::Button(context.text.Text("cb.importPrepare").c_str())) {
        const auto root = services_->RootFor(importDestination_);
        if (root) {
            const auto result = import_.Start(importSources_, services_->roots[*root].catalog->Root(),
                                              importDestination_, importOptions_);
            if (!result) {
                error_ = result.error().GetMessageText();
            }
        }
    }
    if (!import_.ErrorText().empty()) {
        ImGui::TextWrapped("%s", import_.ErrorText().c_str());
    }
    ImGui::TextWrapped("%s", error_.c_str());
    ImGui::SameLine();
    if (ImGui::Button(context.text.Text("cancel").c_str())) {
        import_.Cancel();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
} // namespace NexusEngine
