#include "SceneEditService.h"
#include "Runtime/Scene/TransformSystem.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/DirectionalLightComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include <algorithm>

namespace NexusEngine {
bool SceneEditService::AddComponent(const TypeDescriptor& type) {
    if(!type.add || session_.Selection().All().empty()) return false;
    return Execute("edit.addComponent", [this, &type](Level& level) {
        bool changed = false;
        for(auto id : session_.Selection().All()) {
            auto entity = level.FindEntity(id);
            if(!entity.IsAlive()) return false;
            if(type.contains(entity)) continue;
            if(!type.add(entity)) return false;
            changed = true;
        }
        return changed;
    });
}
bool SceneEditService::Execute(std::string label, const std::function<bool(Level&)>& apply) {
    auto* level = session_.GetLevel();
    if(!level || !level->GetScene() || session_.IsPlaying()) return false;
    properties_.Commit();
    auto candidate = level->GetScene()->Clone();
    if(!candidate) return false;
    const std::vector<PersistentId> selected(session_.Selection().All().begin(), session_.Selection().All().end());
    auto original = level->ExchangeScene(std::move(candidate));
    try {
        if(!apply(*level)) {
            level->ExchangeScene(std::move(original));
            session_.Selection().Replace(selected); return false;
        }
        auto result = level->GetScene()->Clone();
        if(!result) {
            level->ExchangeScene(std::move(original));
            session_.Selection().Replace(selected); return false;
        }
        // immutable snapshotはUndo/Redo closure間だけで共有。GPU資源は含まない。
        std::shared_ptr<const Scene> before(original->Clone()), after(std::move(result));
        const std::vector<PersistentId> selectionAfter(session_.Selection().All().begin(), session_.Selection().All().end());
        const auto restore = [this, level](const std::shared_ptr<const Scene>& snapshot, const std::vector<PersistentId>& selection) {
            if(session_.GetLevel() != level) return false;
            auto copy = snapshot->Clone();
            if(!copy) return false;
            level->ExchangeScene(std::move(copy)); session_.Selection().Replace(selection); return true;
        };
        properties_.History().Record({std::move(label), [restore, before, selected] { return restore(before, selected); },
            [restore, after, selectionAfter] { return restore(after, selectionAfter); }});
        return true;
    } catch(...) {
        level->ExchangeScene(std::move(original)); session_.Selection().Replace(selected); throw;
    }
}
bool SceneEditService::Create(std::string_view kind, std::string_view name) {
    if(kind != "Entity" && kind != "Camera" && kind != "DirectionalLight" && kind != "Model") return false;
    if(name.size() > 1024 || name.find('\0') != name.npos) return false;
    return Execute("edit.create", [this, kind, name](Level& level) {
        auto entity = level.CreateEntity(std::string(name.empty() ? kind : name));
        if(!entity.IsAlive()) return false;
        if(kind == "Camera") {
            auto* camera = entity.AddComponent<CameraComponent>();
            if(!camera) return false;
            // 複数primaryで描画抽出が失敗しないよう、追加Cameraは非primaryにする。
            camera->primary = true;
            level.GetScene()->ForEach<CameraComponent>([&](Entity other, const auto& existing) {
                if(other.GetId() != entity.GetId() && existing.primary && existing.enabled) camera->primary = false;
            });
            entity.GetComponent<TransformComponent>()->translation = {0, 2, -10};
        }
        if(kind == "Model" && !entity.AddComponent<PrimitiveRenderComponent>()) return false;
        if(kind == "DirectionalLight") {
            auto* light = entity.AddComponent<DirectionalLightComponent>();
            if(!light) return false;
            level.GetScene()->ForEach<DirectionalLightComponent>([&](Entity other, const auto& existing) {
                if(other.GetId() != entity.GetId() && existing.enabled) light->enabled = false;
            });
        }
        session_.Select(entity.GetComponent<PersistentIdComponent>()->value); return true;
    });
}
bool SceneEditService::DeleteSelection() {
    const std::vector<PersistentId> ids(session_.Selection().All().begin(), session_.Selection().All().end());
    if(ids.empty()) return false;
    return Execute("edit.delete", [this, ids](Level& level) {
        std::vector<EntityId> local;
        for(auto id : ids) {
            auto entity = level.FindEntity(id);
            if(!entity.IsAlive()) return false;
            local.push_back(entity.GetId());
        }
        level.GetScene()->ForEach<HierarchyComponent>([&](Entity, auto& hierarchy) {
            if(std::ranges::find(local, hierarchy.parent) != local.end()) hierarchy.parent = {};
        });
        for(auto id : ids) if(!level.DestroyEntity(level.FindEntity(id))) return false;
        session_.Selection().Clear(); return true;
    });
}
bool SceneEditService::Reparent(PersistentId child, PersistentId parent) {
    return Execute("edit.reparent", [child, parent](Level& level) {
        auto entity = level.FindEntity(child);
        if(!entity.IsAlive()) return false;
        if(parent.IsValid()) return static_cast<bool>(TransformSystem{}.SetParent(*level.GetScene(), entity, level.FindEntity(parent)));
        return TransformSystem{}.ClearParent(*level.GetScene(), entity);
    });
}
}
