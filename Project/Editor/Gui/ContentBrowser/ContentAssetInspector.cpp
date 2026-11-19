#include "ContentAssetInspector.h"
#include "Editor/Services/ContentBrowserServices.h"
#include "Editor/Services/SelectionService.h"
#include "Editor/Gui/Icons/EditorIcons.h"
#include "Editor/Services/EditorCommandRegistry.h"
#include "Editor/Services/LocalizationService.h"
#include "ThirdParty/imgui/imgui.h"

namespace NexusEngine::UI {
bool DrawContentAssetInspector(const EditorContext &context) {
    // Drag中は既存Entity Detailsを維持し、Texture/Model/Sourceのdrop領域を隠さない。
    if (ImGui::GetDragDropPayload()) {
        return false;
    }
    if (!context.browser || !context.browser->SelectionActive(context.selection.Revision())) {
        return false;
    }
    auto &service = *context.browser;
    service.Poll();
    const auto selected = service.Selected();
    if (selected.empty()) {
        return false;
    }
    const auto &file = selected.back();
    ImGui::TextWrapped("%s", AssetCatalog::Utf8(file.filename()).c_str());
    ImGui::TextDisabled("%zu selected", selected.size());
    ImGui::Separator();
    ImGui::TextWrapped("%s", AssetCatalog::Utf8(file).c_str());
    if (ImGui::Button(context.text.Text("cb.entityProperties").c_str())) {
        service.ClearSelection();
        return true;
    }
    if (service.documents.Dirty(file)) {
        EditorIcon("dirty");
        ImGui::SameLine();
        ImGui::TextUnformatted(context.text.Text("cb.unsaved").c_str());
        if (ImGui::Button(context.text.Text("cb.saveAsset").c_str())) {
            const auto result = service.documents.Save(file);
            if (!result) {
                ImGui::TextWrapped("%s", result.error().GetMessageText().c_str());
            }
        }
    }
    if (service.metadata.Busy()) {
        EditorIcon("loading");
        ImGui::SameLine();
        ImGui::TextUnformatted(context.text.Text("cb.readMetadata").c_str());
    }
    if (const auto &metadata = service.metadata.Value()) {
        if (!metadata->guid.empty()) {
            ImGui::TextWrapped("GUID: %s", metadata->guid.c_str());
        }
        const auto root = service.RootFor(file);
        if (root && !metadata->guid.empty()) {
            const auto origin = service.Origin({service.roots[*root].id, "asset://" + metadata->guid});
            if (origin) {
                ImGui::TextWrapped("%s: %s", context.text.Text("cb.importOrigin").c_str(),
                                   AssetCatalog::Utf8(origin->source).c_str());
            }
        }
        if (!metadata->language.empty()) {
            ImGui::Text("Language: %s", metadata->language.c_str());
        }
        if (!metadata->nameSpace.empty()) {
            ImGui::TextWrapped("Namespace: %s", metadata->nameSpace.c_str());
        }
        if (!metadata->className.empty()) {
            ImGui::TextWrapped("Class: %s", metadata->className.c_str());
        }
        for (const auto &[name, value] : metadata->fields) {
            ImGui::TextWrapped("%s: %s", name.c_str(), value.c_str());
        }
        if (!metadata->error.empty()) {
            EditorIcon("warning");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", metadata->error.c_str());
        }
    }
    if (ImGui::Button(context.text.Text("cb.dependencies").c_str())) {
        service.Select(std::vector<std::filesystem::path>(selected.begin(), selected.end()),
                       context.selection.Revision(), ContentInspectorMode::Dependencies);
    }
    ImGui::SameLine();
    if (ImGui::Button(context.text.Text("cb.references").c_str())) {
        service.Select(std::vector<std::filesystem::path>(selected.begin(), selected.end()),
                       context.selection.Revision(), ContentInspectorMode::References);
    }
    if (service.InspectorMode() != ContentInspectorMode::Metadata) {
        service.dependencies.Request(service.GraphRoots());
        if (service.dependencies.Busy()) {
            EditorIcon("loading");
            ImGui::SameLine();
            ImGui::TextUnformatted(context.text.Text("cb.buildDependencies").c_str());
        }
        const auto graph = service.dependencies.Snapshot();
        if (service.InspectorMode() == ContentInspectorMode::References) {
            const auto found = graph->references.find(file);
            if (found != graph->references.end()) {
                for (const auto &from : found->second) {
                    ImGui::BulletText("%s", AssetCatalog::Utf8(from).c_str());
                }
            } else if (!service.dependencies.Busy()) {
                ImGui::TextUnformatted(context.text.Text("cb.noReferences").c_str());
            }
        } else {
            const auto found = graph->files.find(file);
            if (found != graph->files.end()) {
                for (const auto &dependency : found->second.dependencies) {
                    EditorIcon(dependency.missing ? "missing" : "file");
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s%s", dependency.reference.c_str(), dependency.missing ? " (missing)" : "");
                }
            }
        }
        if (!service.dependencies.ErrorText().empty()) {
            ImGui::TextWrapped("%s", service.dependencies.ErrorText().c_str());
        }
        size_t warningCount = 0;
        for (const auto &warning : graph->warnings) {
            if (++warningCount > 100) {
                break;
            }
            ImGui::TextWrapped("%s", warning.c_str());
        }
    }
    return true;
}
} // namespace NexusEngine::UI
