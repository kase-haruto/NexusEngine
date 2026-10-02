#pragma once

// c++
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// engine
#include "Components/NameComponent.h"
#include "Components/TransformComponent.h"
#include "Entity.h"
#include "ComponentStorage.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * Scene
	 * - Entity slotと型別Component Storageを所有する
	 * - Scene遷移、Serialization、描画、Editor UIは担当しない
	 *---------------------------------------------------------------------------------------*/
	class Scene final {
	public:
		Scene() = default;
		~Scene() = default;
		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;
		Scene(Scene&&) = delete;
		Scene& operator=(Scene&&) = delete;

		/**
		 * \brief NameとTransformを持つEntityを生成する
		 * \param name Entityの初期表示名
		 * \return このSceneを参照する軽量Entity Handle
		 */
		[[nodiscard]] Entity CreateEntity(std::string name = "Entity");

		/**
		 * \brief Entityと所有Componentを破棄する
		 * \param entity このSceneから生成されたEntity
		 * \return 生存Entityを破棄した場合true
		 */
		bool DestroyEntity(Entity entity);

		[[nodiscard]] bool IsAlive(EntityId id) const noexcept;
		[[nodiscard]] Entity GetEntity(EntityId id) noexcept {
			return IsAlive(id) ? Entity(this, id) : Entity {};
		}
		[[nodiscard]] size_t GetEntityCount() const noexcept { return livingEntityCount_; }

		/** \brief 型と要素数のmetadataを列挙する。Storageへの変更権限は公開しない */
		template<typename Function>
		void ForEachStorageInfo(Function&& function) const {
			for(const auto& [type, storage] : componentStorages_) function(type, storage->GetSize());
		}

		template<typename Component, typename... Arguments>
		[[nodiscard]] Component* AddComponent(EntityId id, Arguments&&... arguments);

		template<typename Component>
		[[nodiscard]] Component* GetComponent(EntityId id) noexcept;

		template<typename Component>
		[[nodiscard]] const Component* GetComponent(EntityId id) const noexcept;

		template<typename Component>
		[[nodiscard]] bool HasComponent(EntityId id) const noexcept;

		template<typename Component>
		bool RemoveComponent(EntityId id);

		/**
		 * \brief 指定した全Componentを持つ生存Entityを密なStorageから反復する
		 * \param function Entityと各Component参照を受け取る処理
		 * \note callback実行中はEntityとComponentの構造変更を拒否する
		 */
		template<typename... Components, typename Function>
		void ForEach(Function&& function);

	private:
		struct EntitySlot {
			uint32_t generation = 1; //< 再利用時に古いEntity Handleを無効化する世代番号
			bool alive = false;       //< 現在Entityとして公開されているか
		};

		class IterationGuard final {
		public:
			explicit IterationGuard(size_t& iterationDepth) noexcept : iterationDepth_(iterationDepth) { ++iterationDepth_; }
			~IterationGuard() noexcept { --iterationDepth_; }
			IterationGuard(const IterationGuard&) = delete;
			IterationGuard& operator=(const IterationGuard&) = delete;

		private:
			size_t& iterationDepth_;
		};

		template<typename Component>
		[[nodiscard]] ComponentStorage<Component>* FindStorage() noexcept;

		template<typename Component>
		[[nodiscard]] const ComponentStorage<Component>* FindStorage() const noexcept;

		template<typename Component>
		[[nodiscard]] ComponentStorage<Component>& GetOrCreateStorage();

		std::vector<EntitySlot> entitySlots_; //< indexと世代を保持するEntity slot
		std::vector<uint32_t> freeEntityIndices_; //< 再利用可能な破棄済みslot index
		std::unordered_map<std::type_index, std::unique_ptr<IComponentStorage>> componentStorages_; //< Component型別Storage
		size_t livingEntityCount_ = 0; //< 現在生存しているEntity数
		size_t iterationDepth_ = 0; //< Component反復中の構造変更を拒否するネスト数
	};

	template<typename Component, typename... Arguments>
	Component* Scene::AddComponent(const EntityId id, Arguments&&... arguments) {
		static_assert(!std::is_reference_v<Component> && !std::is_const_v<Component>);
		if(iterationDepth_ != 0 || !IsAlive(id)) {
			return nullptr;
		}
		return GetOrCreateStorage<Component>().Add(id.index, std::forward<Arguments>(arguments)...);
	}

	template<typename Component>
	Component* Scene::GetComponent(const EntityId id) noexcept {
		if(!IsAlive(id)) {
			return nullptr;
		}
		auto* storage = FindStorage<Component>();
		return storage != nullptr ? storage->Get(id.index) : nullptr;
	}

	template<typename Component>
	const Component* Scene::GetComponent(const EntityId id) const noexcept {
		if(!IsAlive(id)) {
			return nullptr;
		}
		const auto* storage = FindStorage<Component>();
		return storage != nullptr ? storage->Get(id.index) : nullptr;
	}

	template<typename Component>
	bool Scene::HasComponent(const EntityId id) const noexcept {
		return GetComponent<Component>(id) != nullptr;
	}

	template<typename Component>
	bool Scene::RemoveComponent(const EntityId id) {
		if(iterationDepth_ != 0 || !IsAlive(id)) {
			return false;
		}
		auto* storage = FindStorage<Component>();
		if(storage == nullptr || storage->Get(id.index) == nullptr) {
			return false;
		}
		storage->Remove(id.index);
		return true;
	}

	template<typename... Components, typename Function>
	void Scene::ForEach(Function&& function) {
		static_assert(sizeof...(Components) > 0);
		static_assert(((!std::is_const_v<Components> && !std::is_reference_v<Components>) && ...));

		std::tuple<ComponentStorage<Components>*...> typedStorages { FindStorage<Components>()... };
		std::array<IComponentStorage*, sizeof...(Components)> storages { FindStorage<Components>()... };
		for(const auto* storage : storages) {
			if(storage == nullptr) {
				return;
			}
		}

		IComponentStorage* drivingStorage = storages.front();
		for(auto* storage : storages) {
			if(storage->GetSize() < drivingStorage->GetSize()) {
				drivingStorage = storage;
			}
		}

		IterationGuard guard(iterationDepth_);
		// 型消去の仮想呼出はQuery開始時の1回に限定し、Entity反復内では配列だけを走査する。
		for(const uint32_t entityIndex : drivingStorage->GetEntityIndices()) {
			const auto& slot = entitySlots_[entityIndex];
			if(!slot.alive) {
				continue;
			}
			const EntityId id { entityIndex, slot.generation };
			// Storage型解決は反復開始時に一度だけ行い、Entityごとのtype_index探索を避ける。
			std::tuple<Components*...> componentPointers = std::apply(
				[entityIndex](auto*... storage) {
					return std::tuple<Components*...> { storage->Get(entityIndex)... };
				},
				typedStorages);
			const bool hasAllComponents = std::apply(
				[](auto*... components) { return ((components != nullptr) && ...); }, componentPointers);
			if(hasAllComponents) {
				std::apply(
					[&](auto*... components) { function(Entity(this, id), *components...); }, componentPointers);
			}
		}
	}

	template<typename Component>
	ComponentStorage<Component>* Scene::FindStorage() noexcept {
		const auto iterator = componentStorages_.find(std::type_index(typeid(Component)));
		return iterator != componentStorages_.end()
			? static_cast<ComponentStorage<Component>*>(iterator->second.get())
			: nullptr;
	}

	template<typename Component>
	const ComponentStorage<Component>* Scene::FindStorage() const noexcept {
		const auto iterator = componentStorages_.find(std::type_index(typeid(Component)));
		return iterator != componentStorages_.end()
			? static_cast<const ComponentStorage<Component>*>(iterator->second.get())
			: nullptr;
	}

	template<typename Component>
	ComponentStorage<Component>& Scene::GetOrCreateStorage() {
		const std::type_index componentType = typeid(Component);
		auto iterator = componentStorages_.find(componentType);
		if(iterator == componentStorages_.end()) {
			auto storage = std::make_unique<ComponentStorage<Component>>();
			iterator = componentStorages_.emplace(componentType, std::move(storage)).first;
		}
		return *static_cast<ComponentStorage<Component>*>(iterator->second.get());
	}

	inline bool Entity::IsAlive() const noexcept {
		return scene_ != nullptr && scene_->IsAlive(id_);
	}

	template<typename Component, typename... Arguments>
	Component* Entity::AddComponent(Arguments&&... arguments) {
		return scene_ != nullptr
			? scene_->AddComponent<Component>(id_, std::forward<Arguments>(arguments)...)
			: nullptr;
	}

	template<typename Component>
	Component* Entity::GetComponent() noexcept {
		return scene_ != nullptr ? scene_->GetComponent<Component>(id_) : nullptr;
	}

	template<typename Component>
	const Component* Entity::GetComponent() const noexcept {
		return scene_ != nullptr ? scene_->GetComponent<Component>(id_) : nullptr;
	}

	template<typename Component>
	bool Entity::HasComponent() const noexcept {
		return scene_ != nullptr && scene_->HasComponent<Component>(id_);
	}

	template<typename Component>
	bool Entity::RemoveComponent() {
		return scene_ != nullptr && scene_->RemoveComponent<Component>(id_);
	}

} // namespace NexusEngine
