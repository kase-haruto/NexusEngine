#include "Scene.h"

// c++
#include <utility>
#include <stdexcept>

namespace NexusEngine {

	/////////////////////////////////////////////////////////////////////////////////////////
	// NameとTransformを持つEntityを生成する
	/////////////////////////////////////////////////////////////////////////////////////////
	Entity Scene::CreateEntity(std::string name) {
		if(iterationDepth_ != 0) {
			throw std::logic_error("Entity creation is not allowed during component iteration.");
		}
		uint32_t entityIndex = 0;
		if(!freeEntityIndices_.empty()) {
			entityIndex = freeEntityIndices_.back();
			freeEntityIndices_.pop_back();
		} else {
			if(entitySlots_.size() >= EntityId::kInvalidIndex) {
				throw std::overflow_error("Scene entity capacity is exhausted.");
			}
			entityIndex = static_cast<uint32_t>(entitySlots_.size());
			entitySlots_.push_back({});
		}

		auto& slot = entitySlots_[entityIndex];
		slot.alive = true;
		++livingEntityCount_;
		const EntityId id { entityIndex, slot.generation };

		try {
			static_cast<void>(AddComponent<NameComponent>(id, NameComponent { std::move(name) }));
			static_cast<void>(AddComponent<TransformComponent>(id));
		} catch(...) {
			// Component構築失敗時にEntityだけを公開状態へ残さない。
			static_cast<void>(DestroyEntity(Entity(this, id)));
			throw;
		}
		return Entity(this, id);
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// Entityを無効化し、全型Storageから所有Componentを除去する
	/////////////////////////////////////////////////////////////////////////////////////////
	bool Scene::DestroyEntity(const Entity entity) {
		if(iterationDepth_ != 0 || entity.scene_ != this || !IsAlive(entity.id_)) {
			return false;
		}

		// free list拡張に失敗した場合はEntityとComponentを変更せず、呼び出し側へ例外を伝える。
		// 世代をwrapさせると最古のHandleが再び有効になる。最大世代slotは永久退役させる。
		if(entitySlots_[entity.id_.index].generation != (std::numeric_limits<uint32_t>::max)()) {
			freeEntityIndices_.push_back(entity.id_.index);
		}
		for(auto& [componentType, storage] : componentStorages_) {
			static_cast<void>(componentType);
			storage->Remove(entity.id_.index);
		}

		auto& slot = entitySlots_[entity.id_.index];
		slot.alive = false;
		if(slot.generation != (std::numeric_limits<uint32_t>::max)()) {
			++slot.generation;
		}
		--livingEntityCount_;
		return true;
	}

	bool Scene::IsAlive(const EntityId id) const noexcept {
		return id.IsValid() && id.index < entitySlots_.size() &&
		       entitySlots_[id.index].alive && entitySlots_[id.index].generation == id.generation;
	}

} // namespace NexusEngine
