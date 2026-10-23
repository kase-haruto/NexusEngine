#include "OutlinerPanel.h"
#include "Editor/Gui/Widgets/SearchBar.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/PropertyEditService.h"
#include "Editor/Services/SceneEditService.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Services/EditorCommandRegistry.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include "Foundation/Logging/Logger.h"
#include "ThirdParty/imgui/imgui.h"
#include <cstring>
#include <map>
#include <set>
#include <algorithm>

namespace NexusEngine {
void OutlinerPanel::Rebuild(Level& level) {
    ++builds_;
    std::set<std::string> expanded;
    for(const auto& row : rows_) if(row.open) expanded.insert(row.key);
    rows_.clear(); roots_.clear();
    level.GetScene()->ForEach<NameComponent, PersistentIdComponent>([&](Entity entity, const auto& name, const auto& persistent) {
        auto* hierarchy = entity.GetComponent<HierarchyComponent>();
        auto key = std::to_string(persistent.value.high) + ':' + std::to_string(persistent.value.low);
        const bool open = expanded.contains(key);
        rows_.push_back({persistent.value,entity.GetId(),hierarchy ? hierarchy->parent : EntityId{},name.name,std::move(key),{},open});
    });
    using Key = std::pair<uint32_t,uint32_t>;
    const auto key = [](EntityId id) { return Key{id.index,id.generation}; };
    std::map<Key,size_t> indices;
    for(size_t i = 0; i < rows_.size(); ++i) indices[key(rows_[i].local)] = i;
    for(size_t i = 0; i < rows_.size(); ++i) {
        auto parent = indices.find(key(rows_[i].parent));
        if(parent != indices.end()) rows_[parent->second].children.push_back(i); else roots_.push_back(i);
    }
    visibleDirty_ = true;
}
void OutlinerPanel::RebuildVisible() {
    visible_.clear(); visibleDirty_ = false;
    if(!searchText_.empty()) {
        for(size_t i = 0; i < rows_.size(); ++i)
            if(rows_[i].name.find(searchText_) != std::string::npos) visible_.push_back({i,0});
        return;
    }
    std::vector<Visible> stack;
    for(auto it = roots_.rbegin(); it != roots_.rend(); ++it) stack.push_back({*it,0});
    std::vector<bool> visited(rows_.size());
    while(!stack.empty()) {
        auto item = stack.back(); stack.pop_back();
        if(visited[item.row]) continue;
        visited[item.row] = true; visible_.push_back(item);
        if(rows_[item.row].open)
            for(auto it = rows_[item.row].children.rbegin(); it != rows_[item.row].children.rend(); ++it) stack.push_back({*it,item.depth+1});
    }
}
void OutlinerPanel::Draw(const EditorContext& context) {
    auto* level = context.session.GetLevel();
    if(!level || !level->GetScene()) return;
    if(UI::SearchBar("OutlinerSearch",searchText_,context.text.Text("search").c_str())) visibleDirty_ = true;
    const auto count = level->GetScene()->GetEntityCount();
    if(scene_ != level->GetScene() || revision_ != context.properties.Revision() || generation_ != context.session.Generation() || count_ != count) {
        if(generation_ != context.session.Generation()) { rows_.clear(); anchor_ = {}; }
        Rebuild(*level); scene_ = level->GetScene(); revision_ = context.properties.Revision();
        generation_ = context.session.Generation(); count_ = count;
    }
    // Nameの入力中にも検索結果を更新する。確定後はRevisionで通常の無効化へ戻る。
    if(context.properties.HasPendingChanges()) Rebuild(*level);
    if(visibleDirty_) RebuildVisible();
    std::optional<std::pair<PersistentId,PersistentId>> reparent;
    bool remove = false;
    const auto target = [&](PersistentId parent) {
        if(ImGui::BeginDragDropTarget()) {
            if(const auto* payload = ImGui::AcceptDragDropPayload("NEXUS_ENTITY"))
                if(payload->DataSize == static_cast<int>(sizeof(PersistentId))) {
                    PersistentId child; std::memcpy(&child,payload->Data,sizeof(child)); reparent = std::make_pair(child,parent);
                }
            ImGui::EndDragDropTarget();
        }
    };
    ImGui::BeginChild("rows",{0,-ImGui::GetFrameHeightWithSpacing()});
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible_.size()),ImGui::GetFrameHeightWithSpacing());
    while(clipper.Step()) for(int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
        auto& row = rows_[visible_[index].row];
        const float indent = static_cast<float>(visible_[index].depth)*ImGui::GetStyle().IndentSpacing;
        if(indent > 0) ImGui::Indent(indent);
        ImGui::PushID(row.key.c_str());
        auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_FramePadding;
        if(!searchText_.empty() || row.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
        if(context.selection.Contains(row.id)) flags |= ImGuiTreeNodeFlags_Selected;
        ImGui::SetNextItemOpen(row.open,ImGuiCond_Always);
        const bool expanded = ImGui::TreeNodeEx("entity",flags,"%s",row.name.c_str());
        if(expanded != row.open) { row.open = expanded; visibleDirty_ = true; }
        if(ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            context.properties.Commit();
            if(ImGui::GetIO().KeyShift && anchor_.IsValid()) {
                auto anchor = std::find_if(visible_.begin(),visible_.end(),[&](const auto& visible) { return rows_[visible.row].id == anchor_; });
                if(anchor != visible_.end()) {
                    const auto first = std::min(static_cast<size_t>(index),static_cast<size_t>(anchor-visible_.begin()));
                    const auto last = std::max(static_cast<size_t>(index),static_cast<size_t>(anchor-visible_.begin()));
                    std::vector<PersistentId> ids;
                    if(ImGui::GetIO().KeyCtrl) ids.assign(context.selection.All().begin(),context.selection.All().end());
                    for(size_t i = first; i <= last; ++i) ids.push_back(rows_[visible_[i].row].id);
                    context.selection.Replace(ids);
                } else context.selection.Select(row.id);
            } else { context.selection.Select(row.id,ImGui::GetIO().KeyCtrl); anchor_ = row.id; }
        }
        if(ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("NEXUS_ENTITY",&row.id,sizeof(row.id));
            ImGui::TextUnformatted(row.name.c_str()); ImGui::EndDragDropSource();
        }
        target(row.id);
        if(ImGui::BeginPopupContextItem()) {
            if(!context.selection.Contains(row.id)) { context.properties.Commit(); context.selection.Select(row.id); }
            if(ImGui::MenuItem(context.text.Text("edit.rename").c_str())) context.commands.Execute("focus.details");
            PropertyHandle handle{row.id,"PrimitiveRender","enabled"};
            auto value = context.properties.Read(handle);
            const bool supported = value && std::holds_alternative<bool>(*value);
            if(ImGui::MenuItem(context.text.Text("property.visible").c_str(),nullptr,supported && std::get<bool>(*value),supported)) {
                context.properties.Preview(handle,!std::get<bool>(*value)); context.properties.Commit();
            }
            if(ImGui::MenuItem(context.text.Text("edit.delete").c_str())) remove = true;
            ImGui::EndPopup();
        }
        ImGui::PopID(); if(indent > 0) ImGui::Unindent(indent);
    }
    ImGui::EndChild();
    ImGui::Selectable(context.text.Label("outliner.root","root-drop").c_str(),false); target({});
    if(reparent && !context.sceneEdits.Reparent(reparent->first,reparent->second)) NEXUS_LOG_WARNING("Outliner",context.text.Text("edit.rejected"));
    if(remove) context.commands.Execute("edit.delete");
}
}
