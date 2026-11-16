#include "CollisionLayerEditor.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Gui/Widgets/SearchBar.h"
#include "Runtime/Scene/Components/ColliderComponent.h"
#include "Runtime/Scene/Components/NameComponent.h"
#include "ThirdParty/imgui/imgui.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace NexusEngine {
Result<void> CollisionLayerEditor::Initialize(EditorSession& session, const std::filesystem::path& file) {
    auto loaded = CollisionLayerSettings::Load(file);
    if(!loaded) {
        error = loaded.error().GetMessageText();
        return std::unexpected(loaded.error());
    }
    // v13で編集中だった表は、共有設定が未作成の場合だけProjectへ移行する。
    std::error_code io;
    if(!std::filesystem::exists(file, io)) {
        if(session.GetLevel()) {
            loaded->matrix = session.GetLevel()->CollisionLayers();
        }
        // HostのResource Root設定で既定表へ置換済みでも、開いているv13文書の設定を失わない。
        const auto* document = session.GetLevelDocument();
        if(document && document->GetFilePath()) {
            std::ifstream legacy(*document->GetFilePath(), std::ios::binary);
            std::string line;
            if(std::getline(legacy, line) && (line == "NEXUS_LEVEL 13" || line == "NEXUS_LEVEL 13\r")) {
                while(std::getline(legacy, line) && !line.starts_with("ENTITY ")) {
                    if(line.starts_with("COLLISION_MATRIX ")) {
                        std::istringstream row(line);
                        std::string tag;
                        row >> tag;
                        auto imported = *loaded;
                        bool valid = true;
                        for(auto& mask : imported.matrix) {
                            uint64_t value = 0;
                            if(!(row >> value) || value > UINT32_MAX) {
                                valid = false;
                                break;
                            }
                            mask = static_cast<uint32_t>(value);
                        }
                        if(valid && imported.IsValid()) {
                            *loaded = std::move(imported);
                        }
                        break;
                    }
                }
            }
        }
        if(loaded->matrix != DefaultCollisionMatrix()) {
            auto saved = loaded->Save(file);
            if(!saved) {
                error = saved.error().GetMessageText();
                return saved;
            }
        }
    }
    auto configured = session.SetCollisionConfiguration(file);
    if(!configured) {
        error = configured.error().GetMessageText();
        return configured;
    }
    session_ = &session;
    file_ = file;
    settings_ = std::move(*loaded);
    history_.Reset();
    error.clear();
    return {};
}
const CollisionLayerSettings& CollisionLayerEditor::Settings() const noexcept {
    return settings_;
}
bool CollisionLayerEditor::BindLevel() {
    if(!session_ || !session_->GetLevel()) {
        return false;
    }
    const auto result = session_->GetLevel()->SetCollisionConfiguration(file_);
    if(!result) {
        error = result.error().GetMessageText();
    }
    return result.has_value();
}
bool CollisionLayerEditor::Publish(const CollisionLayerSettings& next) {
    if(!session_ || session_->IsPlaying() || !next.IsValid()) {
        error = "Invalid or duplicate layer name, or Play is running.";
        return false;
    }
    // 他のEditorで変更された設定へ、古いUndoや編集を上書きしない。
    auto current = CollisionLayerSettings::Load(file_);
    if(!current || *current != settings_) {
        error = "Collision settings changed externally. Reopen the Editor before editing.";
        return false;
    }
    auto saved = next.Save(file_);
    if(!saved) {
        error = saved.error().GetMessageText();
        return false;
    }
    settings_ = next;
    selected = settings_.Resolve(selected);
    static_cast<void>(BindLevel());
    error.clear();
    return true;
}
bool CollisionLayerEditor::Apply(const CollisionLayerSettings& next) {
    if(next == settings_) {
        return true;
    }
    const auto before = settings_;
    if(!Publish(next)) {
        return false;
    }
    history_.Record({"physics.matrix.title", [this, before]() {
        return Publish(before);
    }, [this, next]() {
        return Publish(next);
    }});
    return true;
}
bool CollisionLayerEditor::Undo() {
    return history_.Undo();
}
bool CollisionLayerEditor::Redo() {
    return history_.Redo();
}
bool CollisionLayerEditor::CanUndo() const {
    return session_ && !session_->IsPlaying() && history_.CanUndo();
}
bool CollisionLayerEditor::CanRedo() const {
    return session_ && !session_->IsPlaying() && history_.CanRedo();
}
size_t CollisionLayerEditor::Usage(uint32_t layer) const {
    size_t count = 0;
    auto* level = session_ ? session_->GetLevel() : nullptr;
    if(level && level->GetScene()) {
        level->GetScene()->ForEachEntity([&](Entity entity) {
            const auto* collider = entity.GetComponent<ColliderComponent>();
            const auto* body = entity.GetComponent<RigidBodyComponent>();
            if(collider || body) {
                const auto membership = collider ? collider->collisionLayer : body->collisionLayer;
                count += membership >= 0 && settings_.Resolve(static_cast<uint32_t>(membership)) == layer;
            }
        });
    }
    return count;
}
std::vector<uint32_t> CollisionLayerEditor::VisibleLayers() const {
    std::vector<uint32_t> layers;
    std::array<bool, 32> used{};
    auto* level = session_ ? session_->GetLevel() : nullptr;
    if(level && level->GetScene()) {
        level->GetScene()->ForEachEntity([&](Entity entity) {
            const auto* collider = entity.GetComponent<ColliderComponent>();
            const auto* body = entity.GetComponent<RigidBodyComponent>();
            if(collider || body) {
                const auto membership = collider ? collider->collisionLayer : body->collisionLayer;
                const auto resolved = membership >= 0 ? settings_.Resolve(static_cast<uint32_t>(membership)) : 32;
                if(resolved < 32) {
                    used[resolved] = true;
                }
            }
        });
    }
    for(uint32_t layer = 0; layer < 32; ++layer) {
        // 旧Levelで番号だけ使われていた所属も隠さず、名前を設定できるようにする。
        if(settings_.Resolve(layer) == layer && (!settings_.names[layer].empty() || used[layer])) {
            layers.push_back(layer);
        }
    }
    return layers;
}
bool CollisionLayerEditor::DeleteLayer(uint32_t source, uint32_t destination) {
    if(source == 0 || source >= 32 || destination >= 32 || source == destination || settings_.names[destination].empty()) {
        return false;
    }
    auto next = settings_;
    // 保存済みLevel内の旧IDにも適用する。削除したIDは再利用せず、別の意味へ突然変化させない。
    for(uint32_t layer = 0; layer < 32; ++layer) {
        if(settings_.Resolve(layer) == source) {
            next.redirects[layer] = destination;
        }
    }
    next.names[source].clear();
    return Apply(next);
}
CollisionLayersPanel::CollisionLayersPanel(CollisionLayerEditor& editor) : editor_(editor) {
}
std::string_view CollisionLayersPanel::Id() const noexcept {
    return "CollisionLayers";
}
std::string_view CollisionLayersPanel::TitleKey() const noexcept {
    return "physics.settings";
}
std::array<float, 2> CollisionLayersPanel::InitialSize() const noexcept {
    return {1100, 680};
}
void CollisionLayersPanel::Draw(const EditorContext& context) {
    const auto text = [&](const char* key) {
        return context.text.Text(key);
    };
    editor_.focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGui::TextWrapped("%s", text("physics.project.help").c_str());
    ImGui::BeginDisabled(!editor_.CanUndo());
    if(ImGui::Button(text("edit.undo").c_str())) {
        editor_.Undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!editor_.CanRedo());
    if(ImGui::Button(text("edit.redo").c_str())) {
        editor_.Redo();
    }
    ImGui::EndDisabled();
    if(!editor_.error.empty()) {
        ImGui::TextWrapped("%s", editor_.error.c_str());
    }
    const auto layers = editor_.VisibleLayers();
    const auto& settings = editor_.Settings();
    if(!ImGui::BeginTable("PhysicsLayout", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        return;
    }
    ImGui::TableSetupColumn("Layers", ImGuiTableColumnFlags_WidthFixed, 210);
    ImGui::TableSetupColumn("Matrix", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 240);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::BeginChild("LayerNames", {0, 0});
    ImGui::SeparatorText(text("physics.layers").c_str());
    UI::SearchBar("LayerSearch", search_, text("search").c_str());
    for(auto layer : layers) {
        const auto label = settings.Label(layer);
        if(!search_.empty() && label.find(search_) == std::string::npos) {
            continue;
        }
        ImGui::PushID(static_cast<int>(layer));
        if(ImGui::Selectable(label.c_str(), editor_.selected == layer)) {
            editor_.selected = layer;
        }
        ImGui::PopID();
    }
    if(ImGui::Button(text("physics.layer.add").c_str())) {
        addName_.fill(0);
        ImGui::OpenPopup("AddLayer");
    }
    if(ImGui::BeginPopup("AddLayer")) {
        ImGui::InputText(text("physics.layer.name").c_str(), addName_.data(), addName_.size());
        if(ImGui::Button(text("apply").c_str()) && addName_[0]) {
            auto next = settings;
            bool added = false;
            for(uint32_t layer = 1; layer < 32; ++layer) {
                if(next.names[layer].empty() && next.redirects[layer] == layer && editor_.Usage(layer) == 0) {
                    next.names[layer] = addName_.data();
                    added = editor_.Apply(next);
                    if(added) {
                        editor_.selected = layer;
                        nameLayer_ = 32;
                    }
                    break;
                }
            }
            if(added) {
                ImGui::CloseCurrentPopup();
            } else if(editor_.error.empty()) {
                editor_.error = text("physics.layer.full");
            }
        }
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::TableSetColumnIndex(1);
    ImGui::BeginChild("MatrixRegion", {0, 0});
    ImGui::SeparatorText(text("physics.matrix.title").c_str());
    // 空の31枠を並べず、名前付き・使用中のレイヤーだけで対称pairを一度ずつ描く。
    const auto flags = ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;
    auto next = settings;
    bool changed = false;
    if(ImGui::BeginTable("LayerPairs", static_cast<int>(layers.size() + 1), flags, {0, 0})) {
        ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthFixed, 125);
        for(auto layer : layers) {
            ImGui::TableSetupColumn(settings.Label(layer).c_str(), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_AngledHeader, 28);
        }
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableHeadersRow();
        const int hoveredColumn = ImGui::TableGetHoveredColumn();
        for(size_t row = 0; row < layers.size(); ++row) {
            const auto first = layers[row];
            ImGui::TableNextRow();
            if(hoveredColumn > 0 && hoveredColumn <= static_cast<int>(layers.size())) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(80, 140, 220, 70), hoveredColumn);
            }
            ImGui::TableSetColumnIndex(0);
            if(ImGui::Selectable(settings.Label(first).c_str(), editor_.selected == first)) {
                editor_.selected = first;
            }
            for(size_t column = row; column < layers.size(); ++column) {
                const auto second = layers[column];
                ImGui::TableSetColumnIndex(static_cast<int>(column + 1));
                ImGui::PushID(static_cast<int>(first * 32 + second));
                bool enabled = (next.matrix[first] & (uint32_t{1} << second)) != 0;
                if(ImGui::Checkbox("##Pair", &enabled)) {
                    next.matrix[first] ^= uint32_t{1} << second;
                    if(first != second) {
                        next.matrix[second] ^= uint32_t{1} << first;
                    }
                    changed = true;
                }
                if(ImGui::IsItemHovered()) {
                    // 対象の行・列を同時に強調し、斜め見出しでも組合せを見失わない。
                    const auto color = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, color);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, color);
                    ImGui::SetTooltip("%s <-> %s", settings.Label(first).c_str(), settings.Label(second).c_str());
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if(changed) {
        editor_.Apply(next);
    }
    ImGui::EndChild();
    ImGui::TableSetColumnIndex(2);
    ImGui::BeginChild("LayerDetails", {0, 0});
    const auto selected = settings.Resolve(editor_.selected);
    ImGui::SeparatorText(settings.Label(selected).c_str());
    ImGui::Text("%s: %zu", text("physics.layer.usage").c_str(), editor_.Usage(selected));
    if(nameLayer_ != selected || displayedName_ != settings.Label(selected)) {
        name_.fill(0);
        const auto label = settings.Label(selected);
        std::memcpy(name_.data(), label.data(), std::min(label.size(), name_.size() - 1));
        nameLayer_ = selected;
        displayedName_ = label;
    }
    ImGui::BeginDisabled(selected == 0);
    ImGui::InputText(text("physics.layer.name").c_str(), name_.data(), name_.size());
    if(ImGui::Button(text("physics.layer.rename").c_str()) && name_[0]) {
        auto renamed = settings;
        renamed.names[selected] = name_.data();
        editor_.Apply(renamed);
    }
    if(ImGui::Button(text("physics.layer.delete").c_str())) {
        destination_ = 0;
        ImGui::OpenPopup(context.text.Label("physics.layer.delete", "DeleteLayer").c_str());
    }
    ImGui::EndDisabled();
    if(ImGui::BeginPopupModal(context.text.Label("physics.layer.delete", "DeleteLayer").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", text("physics.layer.deleteHelp").c_str());
        ImGui::Text("%s: %s (%zu)", text("physics.layer.usage").c_str(), settings.Label(selected).c_str(), editor_.Usage(selected));
        ImGui::BeginChild("AffectedObjects", {420, 120}, ImGuiChildFlags_Borders);
        auto* level = context.session.GetLevel();
        if(level && level->GetScene()) {
            level->GetScene()->ForEachEntity([&](Entity entity) {
                const auto* collider = entity.GetComponent<ColliderComponent>();
                const auto* body = entity.GetComponent<RigidBodyComponent>();
                if(collider || body) {
                    const auto membership = collider ? collider->collisionLayer : body->collisionLayer;
                    const auto* name = entity.GetComponent<NameComponent>();
                    if(membership >= 0 && settings.Resolve(static_cast<uint32_t>(membership)) == selected && name) {
                        ImGui::TextUnformatted(name->name.c_str());
                    }
                }
            });
        }
        ImGui::EndChild();
        if(ImGui::BeginCombo(text("physics.layer.destination").c_str(), settings.Label(destination_).c_str())) {
            for(auto layer : layers) {
                if(layer != selected && !settings.names[layer].empty() && ImGui::Selectable(settings.Label(layer).c_str(), layer == destination_)) {
                    destination_ = layer;
                }
            }
            ImGui::EndCombo();
        }
        if(ImGui::Button(text("physics.layer.deleteConfirm").c_str()) && editor_.DeleteLayer(selected, destination_)) {
            nameLayer_ = 32;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if(ImGui::Button(text("cancel").c_str())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SeparatorText(text("physics.summary").c_str());
    for(auto target : layers) {
        auto edited = settings;
        bool enabled = (edited.matrix[selected] & (uint32_t{1} << target)) != 0;
        ImGui::PushID(static_cast<int>(target));
        if(ImGui::Checkbox(settings.Label(target).c_str(), &enabled)) {
            edited.matrix[selected] ^= uint32_t{1} << target;
            if(selected != target) {
                edited.matrix[target] ^= uint32_t{1} << selected;
            }
            editor_.Apply(edited);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::EndTable();
}
}
