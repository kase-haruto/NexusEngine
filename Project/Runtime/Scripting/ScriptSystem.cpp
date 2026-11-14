#include "ScriptSystem.h"
#include "NativeManagedBridge.h"
#include "Runtime/Gameplay/GameApiBridge.h"
#include "Runtime/Level/Level.h"
#include "Runtime/Scene/Components/ScriptComponent.h"
#include "Runtime/Scene/Components/TransformComponent.h"
#include "Runtime/Scene/Scene.h"
#include <algorithm>
#include <cmath>
#include <exception>

namespace NexusEngine {
	ScriptSystem::ScriptSystem(Level& level, NativeManagedBridge& bridge, uint64_t world) : scene_(level.GetScene()), level_(&level), bridge_(&bridge), world_(world), thread_(std::this_thread::get_id()) {
		// 実行WorldのCloneに旧sessionの数値IDを引き継がせない。永続データは変更しない。
		scene_->ForEach<ScriptComponent>([](Entity, ScriptComponent& component) { component.ForEachScript([](auto& slot) { slot.instance = {}; slot.attachment = 0; }); });
	}
	std::span<const std::type_index> ScriptSystem::GetReadComponents() const noexcept {
		static const std::type_index type = typeid(ScriptComponent);
		return {&type, 1};
	}
	std::span<const std::type_index> ScriptSystem::GetWriteComponents() const noexcept {
		static const std::type_index types[]{typeid(ScriptComponent), typeid(TransformComponent)};
		return types;
	}
	Result<void> ScriptSystem::Initialize() { return Synchronize(0, true); }
	Result<void> ScriptSystem::Update(Scene& scene, float deltaTime) {
		if(!bridge_) return {};
		if(thread_ != std::this_thread::get_id()) return std::unexpected(Error(ErrorCategory::Scene, 234, "ScriptSystem requires its owner thread."));
		if(&scene != scene_ || !std::isfinite(deltaTime) || deltaTime < 0)
			return std::unexpected(Error(ErrorCategory::Scene, 234, "ScriptSystem received another World or invalid deltaTime."));
		return Synchronize(deltaTime, false);
	}
	Result<void> ScriptSystem::FixedUpdate(Scene& scene, float deltaTime) {
        if(&scene != scene_) {
            return std::unexpected(Error(ErrorCategory::Scene, 234, "FixedUpdate received another World."));
        }
        return Synchronize(deltaTime, false, true);
    }
    Result<void> ScriptSystem::Synchronize(float deltaTime, bool initializeOnly, bool fixedOnly) {
		if(!bridge_ || !scene_) return {};
		if(thread_ != std::this_thread::get_id()) return std::unexpected(Error(ErrorCategory::Scene, 234, "Script synchronization requires its owner thread."));
		if(!level_ || level_->GetScene() != scene_) return std::unexpected(Error(ErrorCategory::Scene, 234, "Script World was replaced without stopping its Runtime."));
		try {
			snapshots_.clear();
			bindings_.clear();
			const auto scripts = GameApiBridge::Scripts();
			bool	   valid   = true;
			scene_->ForEach<ScriptComponent>([&](Entity entity, const ScriptComponent& component) {
				if(!component.additional.empty() && !bridge_->SupportsScriptAttachments()) { valid = false; return; }
				const auto		   id = entity.GetId();
				NexusScriptStateV1 state{};
				if(!component.IsValid() || component.scriptType.empty() || scripts.getState({world_, id.index, id.generation}, &state) != NEXUS_GAME_OK) {
					valid = false;
					return;
				}
				component.ForEachScript([&](const auto& slot) {
                    if(slot.scriptType.empty() || !slot.attachment) { valid = false; return; }
                    snapshots_.push_back({id, slot.scriptType, slot.instance.value, slot.attachment, slot.enabled ? 1u : 0u});
                });
			});
			if(!valid) return std::unexpected(Error(ErrorCategory::Scene, 234, "ScriptComponent requires valid types/attachments and an updated Bound Gameplay Bootstrap for multiple Scripts."));
			// 同じEntityでは保存された主Script・追加Scriptの順序を維持する。
            std::ranges::stable_sort(snapshots_, [](const auto& left, const auto& right) { return left.entity.index < right.entity.index; });
			for(const auto& item : snapshots_) bindings_.push_back({{world_, item.entity.index, item.entity.generation},
																	{item.type.data(), static_cast<uint32_t>(item.type.size())},
																	item.attachment,
																	item.instance,
																	item.enabled,
																	0});
			// ここではSceneのIterationGuardが終了済み。callbackからEntity / Componentの構造変更が可能。
			auto result = bridge_->Tick(bindings_, deltaTime, initializeOnly, fixedOnly);
			if(!result) return result;
			// 例外的にcallback中にDetachされた場合も旧Levelを参照せず、書戻しを拒否する。
			if(!level_ || level_->GetScene() != scene_) return std::unexpected(Error(ErrorCategory::Scene, 234, "Script callback detached or replaced its World. Stop the Runtime at a frame boundary."));
			for(size_t index = 0; index < bindings_.size(); ++index) {
				const auto& item	  = snapshots_[index];
				auto* owner = scene_->GetEntity(item.entity).GetComponent<ScriptComponent>();
                auto* component = owner ? owner->FindAttachment(item.attachment) : nullptr;
				// 同じslotの再利用、Component再追加、型変更の後に旧結果を書き戻さない。
				if(component && component->attachment == item.attachment && component->scriptType == item.type)
					component->instance.value = bindings_[index].instance;
			}
			return {};
		} catch(const std::exception& exception) {
			return std::unexpected(Error(ErrorCategory::Scene, 234, std::string("Script synchronization failed: ") + exception.what()));
		} catch(...) {
			return std::unexpected(Error(ErrorCategory::Scene, 234, "Script synchronization failed."));
		}
	}
	void ScriptSystem::Detach() noexcept {
		// OnDestroyはこの後Bridge.Stopが担当する。System destructorからManagedを呼ばない。
		if(thread_ == std::this_thread::get_id() && scene_ && level_ && level_->GetScene() == scene_) scene_->ForEach<ScriptComponent>([](Entity, ScriptComponent& component) { component.ForEachScript([](auto& slot) { slot.instance = {}; slot.attachment = 0; }); });
		bridge_ = nullptr;
		scene_	= nullptr;
		level_	= nullptr;
	}
	bool ScriptSystem::RebindScene(bool resetInstances) noexcept {
		if(!bridge_ || !level_ || !level_->GetScene() || thread_ != std::this_thread::get_id()) return false;
		scene_ = level_->GetScene();
		snapshots_.clear();
		bindings_.clear();
		// 旧Sceneへ戻るだけの取消では数値IDも維持し、次Tickで不要なDestroy / Createを発生させない。
		if(resetInstances) scene_->ForEach<ScriptComponent>([](Entity, ScriptComponent& component) { component.ForEachScript([](auto& slot) { slot.instance = {}; }); });
		return true;
	}
} // namespace NexusEngine
