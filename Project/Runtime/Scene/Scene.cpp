#include "Scene.h"

// c++
#include <utility>
#include <stdexcept>

namespace NexusEngine {
	Scene::EntityLayout Scene::CaptureEntityLayout() const {
		EntityLayout result;
		for(const auto& slot : entitySlots_) {
			result.generations.push_back(slot.generation); result.alive.push_back(slot.alive ? uint8_t{1} : uint8_t{0});
		}
		result.freeIndices = freeEntityIndices_;
		return result;
	}
	void Scene::RestoreEntityLayout(const EntityLayout& layout) {
		if(iterationDepth_ || !componentStorages_.empty()) throw std::logic_error("Entity layout requires empty component storages");
		if(layout.generations.size() != layout.alive.size() || layout.generations.size() >= EntityId::kInvalidIndex)
			throw std::runtime_error("Invalid reinstanced entity layout");
		std::vector<EntitySlot> slots; slots.reserve(layout.generations.size());
		std::vector<bool> free(layout.generations.size(), false);
		for(auto index : layout.freeIndices) {
			if(index >= free.size() || free[index] || layout.alive[index])
				throw std::runtime_error("Invalid reinstanced entity free list");
			free[index] = true;
		}
		size_t living = 0;
		for(size_t i = 0; i < layout.generations.size(); ++i) {
			if(layout.generations[i] == 0 || layout.alive[i] > 1 ||
				(!layout.alive[i] && layout.generations[i] != UINT32_MAX && !free[i]))
				throw std::runtime_error("Invalid reinstanced entity slot");
			slots.push_back({layout.generations[i], layout.alive[i] != 0}); living += layout.alive[i];
		}
		auto freeIndices = layout.freeIndices;
		entitySlots_ = std::move(slots); freeEntityIndices_ = std::move(freeIndices); livingEntityCount_ = living;
	}
	std::unique_ptr<Scene> Scene::Clone() const {
		if(iterationDepth_ != 0) return {};
		auto copy = std::make_unique<Scene>();
		copy->entitySlots_ = entitySlots_;
		copy->freeEntityIndices_ = freeEntityIndices_;
		copy->livingEntityCount_ = livingEntityCount_;
		for(const auto& [type, storage] : componentStorages_) {
			auto values = storage->Clone();
			if(!values) return {};
			copy->componentStorages_.emplace(type, std::move(values));
		}
		return copy;
	}

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
			auto* nameComponent = AddComponent<NameComponent>(id);
			nameComponent->name = std::move(name);
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
