#include "OutlinerPanel.h"
#include "Editor/Gui/Widgets/SearchBar.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/PropertyEditService.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Services/EditorCommandRegistry.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include "Runtime/Scene/TransformSystem.h"
#include "Foundation/Logging/Logger.h"
#include "ThirdParty/imgui/imgui.h"
#include <cstring>
#include <map>
#include <set>

namespace NexusEngine {
void OutlinerPanel::Draw(const EditorContext& context) {
    auto* level = context.session.GetLevel();
    if(!level || !level->GetScene()) return;
    UI::SearchBar("OutlinerSearch", searchText_, context.text.Text("search").c_str());
    struct Row { PersistentId id; EntityId local, parent; std::string name; };
    std::vector<Row> rows;
    level->GetScene()->ForEach<NameComponent, PersistentIdComponent>([&](Entity e, auto& name, auto& persistent) {
        auto* hierarchy = e.GetComponent<HierarchyComponent>();
        rows.push_back({persistent.value, e.GetId(), hierarchy ? hierarchy->parent : EntityId{}, name.name});
    });
    using Key = std::pair<uint32_t, uint32_t>;
    auto key = [](EntityId id) { return Key{id.index, id.generation}; };
    std::map<Key, size_t> indices;
    for(size_t i = 0; i < rows.size(); ++i) indices[key(rows[i].local)] = i;
    std::vector<std::vector<size_t>> children(rows.size());
    std::vector<size_t> roots;
    for(size_t i = 0; i < rows.size(); ++i) {
        auto parent = indices.find(key(rows[i].parent));
        if(parent != indices.end()) children[parent->second].push_back(i); else roots.push_back(i);
    }
    std::optional<std::pair<PersistentId, PersistentId>> reparent;
    bool remove = false;
    std::set<size_t> visited;
    const auto target = [&](PersistentId parent) {
        if(ImGui::BeginDragDropTarget()) {
            if(const auto* payload = ImGui::AcceptDragDropPayload("NEXUS_ENTITY")) {
                if(payload->DataSize == static_cast<int>(sizeof(PersistentId))) {
                    PersistentId child; std::memcpy(&child, payload->Data, sizeof(child));
                    reparent = std::make_pair(child, parent);
                }
            }
            ImGui::EndDragDropTarget();
        }
    };
    const bool searching = !searchText_.empty();
    std::function<void(size_t, size_t)> draw = [&](size_t index, size_t depth) {
        if(depth > 128 || !visited.insert(index).second) return;
        const auto& row = rows[index];
        if(searching && row.name.find(searchText_) == row.name.npos) return;
        auto stableId = std::to_string(row.id.high) + ":" + std::to_string(row.id.low);
        ImGui::PushID(stableId.c_str());
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if(searching || children[index].empty()) flags |= ImGuiTreeNodeFlags_Leaf;
        if(context.selection.Contains(row.id)) flags |= ImGuiTreeNodeFlags_Selected;
        bool expanded = ImGui::TreeNodeEx("entity", flags, "%s", row.name.c_str());
        // SelectionをPanel内で複製せず、Details/Viewportと同じサービスへ反映する。
        if(ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) context.selection.Select(row.id, ImGui::GetIO().KeyCtrl);
        if(ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("NEXUS_ENTITY", &row.id, sizeof(row.id));
            ImGui::TextUnformatted(row.name.c_str()); ImGui::EndDragDropSource();
        }
        target(row.id);
        if(ImGui::BeginPopupContextItem()) {
            if(ImGui::MenuItem(context.text.Text("edit.rename").c_str())) {
                context.selection.Select(row.id); context.commands.Execute("focus.details");
            }
            PropertyHandle visible{row.id, "PrimitiveRender", "enabled"};
            auto current = context.properties.Read(visible);
            const bool supported = current && std::holds_alternative<bool>(*current);
            bool enabled = supported && std::get<bool>(*current);
            if(ImGui::MenuItem(context.text.Text("property.visible").c_str(), nullptr, enabled, supported)) {
                context.properties.Begin(visible);
                context.properties.Preview(visible, !enabled); context.properties.Commit();
            }
            if(ImGui::MenuItem(context.text.Text("edit.delete").c_str())) { context.selection.Select(row.id); remove = true; }
            ImGui::EndPopup();
        }
        if(expanded) {
            if(!searching) for(size_t child : children[index]) draw(child, depth + 1);
            ImGui::TreePop();
        }
        ImGui::PopID();
    };
    if(searching) for(size_t i = 0; i < rows.size(); ++i) draw(i, 0);
    else for(size_t root : roots) draw(root, 0);
    ImGui::Selectable(context.text.Label("outliner.root", "root-drop").c_str(), false);
    target({});
    // Entity反復とTree構築の後に適用し、Storage反復中の構造変更を避ける。
    if(reparent) {
        auto child = level->FindEntity(reparent->first);
        if(reparent->second.IsValid()) {
            auto result = TransformSystem{}.SetParent(*level->GetScene(), child, level->FindEntity(reparent->second));
            if(!result) NEXUS_LOG_WARNING("Outliner", result.error().GetMessageText());
            else context.session.MarkDirty();
        } else if(TransformSystem{}.ClearParent(*level->GetScene(), child)) context.session.MarkDirty();
    }
    if(remove) context.commands.Execute("edit.delete");
}
}
