#include "ContentBrowserPanel.h"
#include "Editor/Services/ContentDragPayload.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Services/PropertyEditService.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Gui/Widgets/AssetPropertyWidget.h"
#include <algorithm>

namespace NexusEngine {
void ContentBrowserPanel::RequestMoves(std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves) {
    if (moves.empty() || mutation_.Busy()) {
        return;
    }
    const auto root = services_->RootFor(moves.front().first);
    if (!root || services_->roots[*root].readOnly) {
        error_ = "This root is read-only.";
        return;
    }
    const auto &value = services_->roots[*root];
    if (value.source) {
        std::vector<SourceCreationModule> modules;
        if (creation_) {
            for (bool cpp : {false, true}) {
                const auto found = creation_->Modules(cpp);
                if (found) {
                    modules.insert(modules.end(), found->begin(), found->end());
                }
            }
        }
        for (size_t index = 0; index < moves.size(); ++index) {
            const auto [from, to] = moves[index];
            const auto module = std::ranges::find_if(modules, [&](const auto &candidate) {
                const auto relative = from.lexically_relative(candidate.directory);
                return !relative.empty() && relative != "." && !relative.is_absolute() && *relative.begin() != "..";
            });
            if (module == modules.end()) {
                error_ = "Move sources within their owning game module.";
                return;
            }
            const auto relative = to.lexically_relative(module->directory);
            if (relative.empty() || relative == "." || relative.is_absolute() || *relative.begin() == "..") {
                error_ = "Cross-module source migration requires an explicit ABI and build change.";
                return;
            }
            const auto name = from.filename().string();
            if (name == "Main.cpp" || name == "GameComponents.h" || name.starts_with("NexusGenerated") ||
                name.ends_with(".csproj") || name.ends_with(".vcxproj")) {
                error_ = "Module entry points and generated registries are maintained by the project tools.";
                return;
            }
            // 対応するheader/cppを一緒に動かし、includeとsourceFilesの整合性を保つ。
            if (from.extension() == ".h" || from.extension() == ".cpp") {
                auto pair = from;
                pair.replace_extension(from.extension() == ".h" ? ".cpp" : ".h");
                std::error_code error;
                if (std::filesystem::is_regular_file(pair, error) && std::ranges::none_of(moves, [&](const auto &move) {
                        return move.first == pair;
                    })) {
                    auto destination = to;
                    destination.replace_extension(pair.extension());
                    moves.emplace_back(pair, destination);
                }
            }
        }
    }
    for (const auto &[from, to] : moves) {
        if (services_->RootFor(from) != root || services_->RootFor(to) != root) {
            error_ = "Choose files and a destination in the same writable root.";
            return;
        }
    }
    const auto result = mutation_.Start(value.catalog->Root(), std::move(moves));
    if (!result) {
        error_ = result.error().GetMessageText();
        return;
    }
    mutationRequested_ = true;
}
void ContentBrowserPanel::FolderDrop(const EditorContext &context, const std::filesystem::path &folder) {
    if (context.session.IsPlaying() || !ImGui::BeginDragDropTarget()) {
        return;
    }
    auto *payload = ImGui::AcceptDragDropPayload("NEXUS_CONTENT_ITEMS");
    const bool multi = payload != nullptr;
    if (!payload) {
        payload = ImGui::AcceptDragDropPayload("NEXUS_ASSET_PATH");
    }
    if (!payload) {
        payload = ImGui::AcceptDragDropPayload("NEXUS_SOURCE_PATH");
    }
    if (payload) {
        const auto files = multi ? ContentDragPayload::Decode(payload->Data, static_cast<size_t>(payload->DataSize))
                                 : ContentDragPayload::Legacy(payload->Data, static_cast<size_t>(payload->DataSize));
        if (files) {
            std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves;
            for (const auto &file : *files) {
                moves.emplace_back(file, folder / file.filename());
            }
            RequestMoves(std::move(moves));
        } else {
            error_ = files.error().GetMessageText();
        }
    }
    ImGui::EndDragDropTarget();
}
void ContentBrowserPanel::DrawMutation(const EditorContext &context) {
    mutation_.Poll();
    if (mutationRequested_) {
        mutationRequested_ = false;
        ImGui::OpenPopup("ContentMoveReview");
    }
    if (!ImGui::BeginPopupModal("ContentMoveReview", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextUnformatted(context.text.Text("cb.moveReview").c_str());
    if (mutation_.Busy()) {
        ImGui::TextUnformatted(context.text.Text("cb.importPreparing").c_str());
    }
    if (mutation_.Plan()) {
        ImGui::BeginChild("AffectedFiles", {620, 250}, ImGuiChildFlags_Borders);
        for (const auto &file : mutation_.Plan()->files) {
            ImGui::TextWrapped("%s%s", file.rewritten ? "Update references: " : "Move: ",
                               AssetCatalog::Utf8(file.afterPath.lexically_relative(mutation_.Plan()->root)).c_str());
        }
        ImGui::EndChild();
        ImGui::BeginDisabled(context.session.IsPlaying());
        if (ImGui::Button(context.text.Text("cb.apply").c_str())) {
            const auto result = mutation_.Publish(services_->documents, context.properties.History(), fileRevision_);
            if (result) {
                state_.selection.clear();
                services_->ClearSelection();
                if (context.assets) {
                    context.assets->RefreshPreviews();
                }
                ImGui::CloseCurrentPopup();
            } else {
                error_ = result.error().GetMessageText();
            }
        }
        ImGui::EndDisabled();
    }
    if (!mutation_.ErrorText().empty()) {
        ImGui::TextWrapped("%s", mutation_.ErrorText().c_str());
    }
    ImGui::TextWrapped("%s", error_.c_str());
    if (ImGui::Button(context.text.Text("cancel").c_str())) {
        mutation_.Cancel();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
} // namespace NexusEngine
