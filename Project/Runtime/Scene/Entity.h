#pragma once

// c++
#include <utility>

// engine
#include "EntityId.h"

namespace NexusEngine {
	class Scene;

	/*-----------------------------------------------------------------------------------------
	 * Entity
	 * - Scene内のEntityIdを操作する軽量Handle
	 * - Entity slotやComponentは所有せず、生成元Sceneより長く保持してはならない
	 *---------------------------------------------------------------------------------------*/
	class Entity final {
	public:
		Entity() noexcept = default;

		[[nodiscard]] bool IsAlive() const noexcept;
		[[nodiscard]] EntityId GetId() const noexcept { return id_; }
		[[nodiscard]] Scene* GetScene() const noexcept { return scene_; }

		template<typename Component, typename... Arguments>
		[[nodiscard]] Component* AddComponent(Arguments&&... arguments);

		template<typename Component>
		[[nodiscard]] Component* GetComponent() noexcept;

		template<typename Component>
		[[nodiscard]] const Component* GetComponent() const noexcept;

		template<typename Component>
		[[nodiscard]] bool HasComponent() const noexcept;

		template<typename Component>
		bool RemoveComponent();

		[[nodiscard]] friend bool operator==(const Entity&, const Entity&) noexcept = default;

	private:
		friend class Scene;
		Entity(Scene* scene, EntityId id) noexcept : scene_(scene), id_(id) {}

		Scene* scene_ = nullptr; //< EntityとComponentを所有する非所有Scene参照
		EntityId id_;            //< Scene内の世代付き識別子
	};

} // namespace NexusEngine
