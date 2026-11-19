#include "ContentBrowserPanel.h"
#include "Editor/Gui/Icons/EditorIcons.h"
#include "Editor/Services/ContentBrowserActions.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Services/SceneEditService.h"
#include "Editor/Core/EditorSession.h"
#include "Foundation/Utility/String/ConvertString.h"
#include <algorithm>
#include <cstdio>
#include "Platform/FileSystem/AtomicFile.h"

namespace NexusEngine {
void ContentBrowserPanel::RunBatch(const EditorContext &context, ContentFileOperation operation,
                                   const std::filesystem::path &destination) {
    if (context.session.IsPlaying()) {
        return;
    }
    context.properties.Commit();
    if (operation != ContentFileOperation::Duplicate) {
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves;
        auto target = destination;
        if (operation == ContentFileOperation::Archive) {
            const auto id = CreateUuid();
            if (!id) {
                error_ = id.error().GetMessageText();
                return;
            }
            const auto root = services_->RootFor(state_.selection.front());
            if (!root) {
                return;
            }
            target = services_->roots[*root].catalog->Root() / ".nexus-trash" / *id;
        }
        for (const auto &file : state_.selection) {
            moves.emplace_back(file, target / file.filename());
        }
        RequestMoves(std::move(moves));
        return;
    }
    if (std::ranges::any_of(state_.selection, [](const auto &path) {
            std::error_code error;
            return std::filesystem::is_directory(path, error);
        })) {
        const auto root = services_->RootFor(state_.selection.front());
        if (!root) {
            return;
        }
        importOptions_ = {};
        importOptions_.dependencies = false;
        importDestination_ = services_->roots[*root].catalog->Root();
        const auto prepared = import_.DuplicateFolders(importDestination_, state_.selection);
        if (!prepared) {
            error_ = prepared.error().GetMessageText();
        } else {
            importRequested_ = true;
        }
        return;
    }
    const auto result = ContentFileTransaction::Execute(assets_, state_.selection, operation, destination,
                                                        context.properties.History(), fileRevision_);
    if (!result) {
        error_ = result.error().GetMessageText();
    } else {
        state_.selection.clear();
    }
}

void ContentBrowserPanel::DrawEntryActions(const EditorContext &context, const AssetEntry &entry) {
    auto &catalog = *roots_[rootIndex_].catalog;
    const auto &type = catalog.Types().Find(entry.typeKey);
    DrawCreateMenu(context, entry.directory ? entry.path : entry.path.parent_path());
    const auto entryRoot = services_->RootFor(entry.path);
    if (UI::IconMenuItem("info", context.text.Text("cb.inspect").c_str())) {
        services_->Select(state_.selection, context.selection.Revision());
    }
    if (UI::IconMenuItem("dependencies", context.text.Text("cb.dependencies").c_str(), false, !entry.directory)) {
        services_->Select(state_.selection, context.selection.Revision(), ContentInspectorMode::Dependencies);
    }
    if (UI::IconMenuItem("references", context.text.Text("cb.references").c_str(), false, !entry.directory)) {
        services_->Select(state_.selection, context.selection.Revision(), ContentInspectorMode::References);
    }
    if (ImGui::BeginMenu(context.text.Text("cb.addCollection").c_str())) {
        for (const auto &collection : services_->library.Collections()) {
            if (UI::IconMenuItem("collection", collection.name.c_str())) {
                std::vector<ContentReference> references;
                for (const auto &file : state_.selection) {
                    const auto reference = services_->Reference(file);
                    if (reference) {
                        references.push_back(*reference);
                    }
                }
                const auto result = services_->library.Add(collection.id, references);
                if (!result) {
                    error_ = result.error().GetMessageText();
                }
            }
        }
        ImGui::EndMenu();
    }
    if (virtualView_ == "Collection" &&
        UI::IconMenuItem("delete", context.text.Text("cb.removeCollectionItem").c_str())) {
        std::vector<ContentReference> references;
        for (const auto &file : state_.selection) {
            const auto reference = services_->Reference(file);
            if (reference) {
                references.push_back(*reference);
            }
        }
        const auto result = services_->library.Remove(collectionId_, references);
        if (!result) {
            error_ = result.error().GetMessageText();
        }
    }
    if (entryRoot && services_->roots[*entryRoot].readOnly) {
        ImGui::TextDisabled("%s", context.text.Text("cb.readOnlyRoot").c_str());
    }

    if (UI::IconMenuItem("reimport", context.text.Text("cb.reimport").c_str(), false,
                         !entry.directory && entryRoot && !services_->roots[*entryRoot].readOnly && !type.source &&
                             !context.session.IsPlaying())) {
        RequestImport(entry.path);
    }
    ImGui::Separator();
    if (UI::IconMenuItem(type.source ? "code_editor" : "open",
                         context.text.Text(type.source ? "source.open" : "cb.open").c_str())) {
        if (entry.directory) {
            pendingNavigate_ = ContentLocation{rootIndex_, entry.path};
        } else {
            Open(entry.path);
        }
    }
    if (UI::IconMenuItem("explorer", context.text.Text("cb.explorer").c_str())) {
        const auto result = ContentBrowserActions::ShowInExplorer(entry.path);
        if (!result) {
            error_ = result.error().GetMessageText();
        }
    }
    if (UI::IconMenuItem("copy_path", context.text.Text("cb.copyPath").c_str())) {
        std::string paths;
        for (const auto &selected : state_.selection) {
            if (!paths.empty()) {
                paths += '\n';
            }
            paths += AssetCatalog::Utf8(selected);
        }
        ImGui::SetClipboardText(paths.c_str());
    }
    if (UI::IconMenuItem("copy_path", context.text.Text("cb.copyRelativePath").c_str())) {
        std::string paths;
        for (const auto &selected : state_.selection) {
            if (!paths.empty()) {
                paths += '\n';
            }
            paths += AssetCatalog::Utf8(selected.lexically_relative(catalog.Root()));
        }
        ImGui::SetClipboardText(paths.c_str());
    }
    if (UI::IconMenuItem("favorite", context.text.Text("cb.toggleFavorite").c_str())) {
        for (const auto &selected : state_.selection) {
            state_.ToggleFavorite(selected);
        }
        Persist();
    }
    if (entry.directory &&
        UI::IconMenuItem("folder", context.text.Text("cb.newFolder").c_str(), false,
                         !context.session.IsPlaying() && entryRoot && !services_->roots[*entryRoot].readOnly)) {
        folderParent_ = entry.path;
        folderRequested_ = true;
    }
    const bool rename = !context.session.IsPlaying() && entryRoot && !services_->roots[*entryRoot].readOnly &&
                        state_.selection.size() == 1;
    if (UI::IconMenuItem("rename", context.text.Text("cb.renameMove").c_str(), false, rename)) {
        renameSource_ = entry.path;
        std::snprintf(renamePath_.data(), renamePath_.size(), "%s",
                      AssetCatalog::Utf8(entry.path.lexically_relative(
                                             entryRoot ? services_->roots[*entryRoot].catalog->Root() : catalog.Root()))
                          .c_str());
        renameRequested_ = true;
    }
    if (!rename && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", context.text.Text("cb.renameLimit").c_str());
    }
    const bool batch = !context.session.IsPlaying() && entryRoot && !services_->roots[*entryRoot].readOnly &&
                       !state_.selection.empty() && std::ranges::all_of(state_.selection, [&](const auto &file) {
                           return services_->RootFor(file) == entryRoot;
                       });
    const bool relocatable = batch;
    const bool duplicatable = batch &&
                              (!type.source || (creation_ && creation_->CanCreate() && state_.selection.size() == 1)) &&
                              std::ranges::all_of(state_.selection, [&](const auto &file) {
                                  const auto found = std::ranges::find(visible_, file, &AssetEntry::path);
                                  return found != visible_.end() && (!type.source || !found->directory);
                              });
    if (UI::IconMenuItem("duplicate", context.text.Text("cb.duplicateSelection").c_str(), false, duplicatable)) {
        if (type.source) {
            RequestSourceClone(entry.path);
        } else {
            RunBatch(context, ContentFileOperation::Duplicate);
        }
    }
    if (UI::IconMenuItem("move", context.text.Text("cb.moveSelection").c_str(), false, relocatable)) {
        moveRequested_ = true;
    }
    if (UI::IconMenuItem("delete", context.text.Text("cb.archiveSelection").c_str(), false, relocatable)) {
        archiveRequested_ = true;
    }
    for (const auto &action : type.actions) {
        if (UI::IconMenuItem(action.icon, context.text.Text(action.labelKey).c_str(), false,
                             action.execute && (!action.enabled || action.enabled(entry.path)))) {
            const auto result = action.execute(entry.path);
            if (!result) {
                error_ = result.error().GetMessageText();
            }
        }
    }
    if (type.source && sources_) {
        const auto types = SourceFileActions::TypesFor(sources_->Root(), entry.path, context.types);
        const bool editable = !context.session.IsPlaying() && !context.selection.All().empty();
        if (ImGui::BeginMenu(context.text.Text("source.attach").c_str(), editable && !types.empty())) {
            for (const auto &id : types) {
                const auto *component = context.types.Find(id);
                if (!component) {
                    continue;
                }
                auto *level = context.session.GetLevel();
                const bool attached = level && std::ranges::all_of(context.selection.All(), [&](PersistentId entity) {
                                          return component->contains(level->FindEntity(entity));
                                      });
                if (ImGui::MenuItem(context.text.Label(component->labelKey, id).c_str(), nullptr, attached,
                                    !attached)) {
                    attach_ = id;
                }
            }
            ImGui::EndMenu();
        }
    }
}
void ContentBrowserPanel::DrawDialogs(const EditorContext &context) {
    auto &catalog = *roots_[rootIndex_].catalog;
    if (folderRequested_) {
        folderRequested_ = false;
        if (folderParent_.empty()) {
            folderParent_ = roots_[rootIndex_].current;
        }
        folderName_.fill(0);
        ImGui::OpenPopup("ContentNewFolder");
    }
    if (ImGui::BeginPopupModal("ContentNewFolder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(context.text.Text("cb.newFolder").c_str());
        ImGui::InputText("##FolderName", folderName_.data(), folderName_.size());
        ImGui::BeginDisabled(context.session.IsPlaying());
        if (ImGui::Button(context.text.Text("cb.apply").c_str())) {
            const auto result = ContentBrowserActions::CreateFolder(catalog, folderParent_, folderName_.data());
            if (result) {
                folderParent_.clear();
                ImGui::CloseCurrentPopup();
            } else {
                error_ = result.error().GetMessageText();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(context.text.Text("cancel").c_str())) {
            folderParent_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (archiveRequested_) {
        archiveRequested_ = false;
        ImGui::OpenPopup("ContentArchiveSelection");
    }
    if (ImGui::BeginPopupModal("ContentArchiveSelection", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", context.text.Text("cb.archiveHelp").c_str());
        if (ImGui::Button(context.text.Text("cb.apply").c_str())) {
            RunBatch(context, ContentFileOperation::Archive);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(context.text.Text("cancel").c_str())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (moveRequested_) {
        moveRequested_ = false;
        renamePath_.fill(0);
        ImGui::OpenPopup("ContentMoveSelection");
    }
    if (ImGui::BeginPopupModal("ContentMoveSelection", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", context.text.Text("cb.moveHelp").c_str());
        ImGui::InputText("##DestinationFolder", renamePath_.data(), renamePath_.size());
        if (ImGui::Button(context.text.Text("cb.apply").c_str())) {
            const auto root =
                state_.selection.empty() ? std::optional<size_t>{} : services_->RootFor(state_.selection.front());
            const auto target = (root ? services_->roots[*root].catalog->Root() : assets_.Root()) /
                                ConvertString(std::string(renamePath_.data()));
            RunBatch(context, ContentFileOperation::Move, target);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(context.text.Text("cancel").c_str())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (renameRequested_) {
        renameRequested_ = false;
        ImGui::OpenPopup("ContentRenameAsset");
    }
    if (ImGui::BeginPopupModal("ContentRenameAsset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", context.text.Text("cb.renameHelp").c_str());
        ImGui::SetNextItemWidth(480);
        ImGui::InputText("##NewAssetPath", renamePath_.data(), renamePath_.size());
        ImGui::BeginDisabled(context.session.IsPlaying());
        if (ImGui::Button(context.text.Text("cb.apply").c_str())) {
            context.properties.Commit();
            const auto root = services_->RootFor(renameSource_);
            if (root) {
                RequestMoves({{renameSource_, services_->roots[*root].catalog->Root() /
                                                  ConvertString(std::string(renamePath_.data()))}});
                if (mutationRequested_) {
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(context.text.Text("cancel").c_str())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
} // namespace NexusEngine
