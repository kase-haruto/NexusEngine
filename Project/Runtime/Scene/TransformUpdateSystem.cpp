#include "TransformUpdateSystem.h"

// engine
#include "Components/HierarchyComponent.h"
#include "Components/TransformComponent.h"

namespace NexusEngine {

	std::span<const std::type_index> TransformUpdateSystem::GetReadComponents() const noexcept {
		static const std::array<std::type_index, 0> types {};
		return types;
	}

	std::span<const std::type_index> TransformUpdateSystem::GetWriteComponents() const noexcept {
		// 無効な親参照の解除も行うためHierarchyはread-onlyではなくwriteとして宣言する。
		static const std::array types {
			std::type_index(typeid(TransformComponent)),
			std::type_index(typeid(HierarchyComponent))
		};
		return types;
	}

	Result<void> TransformUpdateSystem::Update(Scene& scene, const float deltaTime) {
		static_cast<void>(deltaTime);
		return transformSystem_.Update(scene);
	}

} // namespace NexusEngine
