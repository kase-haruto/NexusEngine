#include "AssetPropertyWidget.h"
#include "SearchBar.h"
#include <cstring>
#include <algorithm>

namespace NexusEngine {
void AssetPropertyWidget::Configure(std::filesystem::path root, Validate validate, Model model, Preview preview) {
    catalog_ = std::make_unique<AssetCatalog>(std::move(root));
    validate_ = std::move(validate); model_ = std::move(model); preview_ = std::move(preview);
}
void AssetPropertyWidget::DrawPreview(const std::string& path, bool texture, int node, float size) {
    if(!preview_) return;
    auto image = preview_(path, texture, node);
    if(image._TexData || image._TexID != ImTextureID_Invalid) ImGui::Image(image, {size, size});
}
UI::ItemResult AssetPropertyWidget::Draw(const UI::ItemOptions& options, PropertyValue& value,
    const PropertyDescriptor& property, const LocalizationService& text, const std::string& mesh, int node) {
    if(!catalog_) { ImGui::TextDisabled("%s", text.Text("asset.unavailable").c_str()); return {}; }
    bool changed = false;
    ImGui::TextUnformatted(std::string(options.label).c_str());
    if(property.kind == PropertyKind::MeshNode) {
        auto* number = std::get_if<int>(&value);
        const auto* asset = model_ ? model_(mesh) : nullptr;
        if(!number || !asset) return {};
        std::string current = *number == -1 ? text.Text("asset.allNodes") :
            (*number >= 0 && static_cast<size_t>(*number) < asset->nodes.size() ? asset->nodes[*number].name : text.Text("asset.missing"));
        if(ImGui::BeginCombo("##node", current.c_str())) {
            if(ImGui::Selectable(text.Text("asset.allNodes").c_str(), *number == -1)) { *number = -1; changed = true; }
            for(size_t index = 0; index < asset->nodes.size(); ++index) if(!asset->nodes[index].meshIndices.empty()) {
                ImGui::PushID(static_cast<int>(index));
                auto label = asset->nodes[index].name + " [" + std::to_string(index) + ']';
                if(ImGui::Selectable(label.c_str(), *number == static_cast<int>(index))) { *number = static_cast<int>(index); changed = true; }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    } else if(auto* path = std::get_if<std::string>(&value)) {
        const bool texture = property.kind == PropertyKind::TextureAsset;
        const auto assign = [&](const std::filesystem::path& dropped) {
            auto result = validate_(dropped, texture);
            if(!result) { error_ = result.error().GetMessageText(); return; }
            changed = *path != *result; *path = std::move(*result); error_.clear();
        };
        const auto label = path->empty() ? text.Text("asset.default") : *path;
        ImGui::Button((label + "###asset").c_str(), {std::max(60.0f, ImGui::GetContentRegionAvail().x - 40), 0});
        if(ImGui::BeginDragDropTarget()) {
            if(const auto* payload = ImGui::AcceptDragDropPayload("NEXUS_ASSET_PATH")) {
                // Payload長を検証し、外部の任意byte列をC文字列として読まない。
                if(payload->Data && payload->DataSize > 1 && payload->DataSize <= 32768) {
                    const auto* bytes = static_cast<const char*>(payload->Data);
                    if(bytes[payload->DataSize - 1] == '\0' && std::strlen(bytes) == static_cast<size_t>(payload->DataSize - 1))
                        assign(std::filesystem::path(reinterpret_cast<const char8_t*>(bytes)));
                }
            }
            ImGui::EndDragDropTarget();
        }
        if(ImGui::IsItemClicked()) ImGui::OpenPopup("asset-picker");
        ImGui::SameLine(); if(ImGui::SmallButton("X")) { changed = !path->empty(); path->clear(); error_.clear(); }
        if(ImGui::BeginPopup("asset-picker")) {
            ImGui::TextUnformatted(text.Text(texture ? "asset.chooseTexture" : "asset.chooseMesh").c_str());
            if(ImGui::Button(text.Text("asset.up").c_str())) catalog_->Navigate(catalog_->Current().parent_path());
            ImGui::SameLine(); ImGui::TextUnformatted(AssetCatalog::Utf8(catalog_->Current().filename()).c_str());
            UI::SearchBar("filter", search_, text.Text("search").c_str());
            ImGui::BeginChild("choices", {350, 280});
            std::optional<std::filesystem::path> navigate;
            for(const auto& entry : *catalog_->Snapshot(catalog_->Current())) {
                if(!entry.directory && entry.typeKey != (texture ? "asset.texture" : "asset.model")) continue;
                if(!search_.empty() && entry.name.find(search_) == std::string::npos) continue;
                if(ImGui::Selectable((entry.directory ? "[+] " + entry.name : entry.name).c_str())) {
                    if(entry.directory) navigate = entry.path;
                    else { assign(entry.path); if(error_.empty()) ImGui::CloseCurrentPopup(); }
                }
            }
            if(navigate) catalog_->Navigate(*navigate);
            ImGui::EndChild(); ImGui::EndPopup();
        }
        if(!texture || !path->empty()) DrawPreview(*path, texture, texture ? -1 : node);
    }
    if(!error_.empty()) ImGui::TextWrapped("%s", error_.c_str());
    return changed ? UI::ItemResult{UI::ItemStatus::Activated | UI::ItemStatus::Edited | UI::ItemStatus::EditCompleted} : UI::ItemResult{};
}
}
