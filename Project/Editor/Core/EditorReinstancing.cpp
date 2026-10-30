#include "EditorApplication.h"
#include "Runtime/Reflection/ComponentReflection.h"
#include "Foundation/Logging/Logger.h"
#include "Runtime/Scene/TransformSystem.h"

namespace NexusEngine {
ReflectionValue EditorApplication::CaptureReinstancing() {
    // 操作中の編集を確定してから World を保存し、未保存フラグとの不整合を避ける。
    ReflectionValue::Object result;
    if(workspace_) result.emplace("workspace", workspace_->CaptureForReinstancing());
    ComponentReflection registry; RegisterGeneratedComponents(registry);
    if(auto* level = session_.GetLevel(); level && level->GetScene()) result.emplace("edit", registry.Capture(*level->GetScene()));
    if(auto* level = session_.GetPlayLevel(); level && level->GetScene()) result.emplace("play", registry.Capture(*level->GetScene()));
    return ReflectionValue(std::move(result));
}
void EditorApplication::ReleaseReinstancing() noexcept {
    FinishAssetPreparation();
    if(workspace_) { workspace_->ReleaseForReinstancing(); workspace_.reset(); }
    if(auto* level = session_.GetLevel(); level && level->GetScene()) level->GetScene()->ReleaseComponentStorages();
    if(auto* level = session_.GetPlayLevel(); level && level->GetScene()) level->GetScene()->ReleaseComponentStorages();
}
void EditorApplication::RestoreReinstancing(const ReflectionValue& snapshot) {
    // 旧 Registry のメンバ位置や生成関数は持ち越さず、必ず新コードから構築する。
    ComponentReflection registry; RegisterGeneratedComponents(registry);
    const auto& worlds = std::get<ReflectionValue::Object>(snapshot.data);
    for(auto [id, level] : {std::pair{"edit", session_.GetLevel()}, std::pair{"play", session_.GetPlayLevel()}}) {
        if(auto found = worlds.find(id); found != worlds.end()) {
            if(!level || !level->GetScene()) throw std::runtime_error("Reinstancing world is no longer available");
            registry.Restore(*level->GetScene(), found->second);
            // 再生成では派生Matrixだけを復元し、Pause中のゲームSystemを実行しない。
            const auto update = TransformSystem{}.Update(*level->GetScene());
            if(!update) throw std::runtime_error(update.error().GetMessageText());
        }
    }
    session_.NotifyReinstanced();
    if(auto found = worlds.find("workspace"); found != worlds.end()) {
        auto result = CreateWorkspace(1.0f);
        if(!result) throw std::runtime_error(result.error().GetMessageText());
        workspace_->RestoreAfterReinstancing(found->second);
    }
    NEXUS_LOG_INFO("Live++", "Editor workspace and reflected component storages reinstanced; entity IDs, selection and unsaved edits preserved.");
}
}
