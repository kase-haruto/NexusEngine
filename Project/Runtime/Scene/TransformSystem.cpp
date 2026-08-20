#include "TransformSystem.h"

// c++
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

// engine
#include "Components/HierarchyComponent.h"
#include "Components/TransformComponent.h"
#include "Scene.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidEntity = 1;
		constexpr int32_t kHierarchyCycle = 2;

		enum class VisitState : uint8_t {
			Unvisited,
			Visiting,
			Resolved
		};
	}

	Result<void> TransformSystem::SetParent(Scene& scene, Entity child, Entity parent) const {
		if(child.GetScene() != &scene || parent.GetScene() != &scene || !child.IsAlive() || !parent.IsAlive() ||
		   child.GetId() == parent.GetId() || child.GetComponent<TransformComponent>() == nullptr ||
		   parent.GetComponent<TransformComponent>() == nullptr) {
			return std::unexpected(Error(ErrorCategory::Scene, kInvalidEntity, "Transform parent entities are invalid."));
		}

		// 親からrootまで辿り、childへ戻る関係を設定前に拒否する。
		EntityId ancestor = parent.GetId();
		std::unordered_set<uint32_t> visitedAncestors;
		while(scene.IsAlive(ancestor)) {
			if(ancestor == child.GetId() || !visitedAncestors.insert(ancestor.index).second) {
				return std::unexpected(Error(ErrorCategory::Scene, kHierarchyCycle, "Transform hierarchy cycle was detected."));
			}
			const auto* hierarchy = scene.GetComponent<HierarchyComponent>(ancestor);
			if(hierarchy == nullptr || !hierarchy->parent.IsValid()) {
				break;
			}
			ancestor = hierarchy->parent;
		}

		auto* hierarchy = child.GetComponent<HierarchyComponent>();
		if(hierarchy == nullptr) {
			hierarchy = child.AddComponent<HierarchyComponent>();
			if(hierarchy == nullptr) {
				return std::unexpected(Error(ErrorCategory::Scene, kInvalidEntity, "Failed to add transform hierarchy component."));
			}
		}
		hierarchy->parent = parent.GetId();
		return {};
	}

	bool TransformSystem::ClearParent(Scene& scene, Entity child) const {
		if(child.GetScene() != &scene || !child.IsAlive()) {
			return false;
		}
		if(auto* hierarchy = child.GetComponent<HierarchyComponent>()) {
			hierarchy->parent = {};
		}
		return true;
	}

	Result<void> TransformSystem::Update(Scene& scene) const {
		uint32_t maximumEntityIndex = 0;
		bool hasTransforms = false;
		scene.ForEach<TransformComponent>([&](Entity entity, TransformComponent& transform) {
			hasTransforms = true;
			maximumEntityIndex = (std::max)(maximumEntityIndex, entity.GetId().index);
			transform.localMatrix = MakeAffineMatrix(
				transform.scale, transform.rotation, transform.translation);
		});
		if(!hasTransforms) {
			return {};
		}

		std::vector<VisitState> visitStates(static_cast<std::size_t>(maximumEntityIndex) + 1, VisitState::Unvisited);
		bool cycleDetected = false;

		auto resolveWorld = [&](auto&& self, Entity entity, TransformComponent& transform) -> void {
			auto& visitState = visitStates[entity.GetId().index];
			if(visitState == VisitState::Resolved || cycleDetected) {
				return;
		}
			if(visitState == VisitState::Visiting) {
				cycleDetected = true;
				return;
			}

			visitState = VisitState::Visiting;
			transform.worldMatrix = transform.localMatrix;
			if(auto* hierarchy = entity.GetComponent<HierarchyComponent>(); hierarchy != nullptr && hierarchy->parent.IsValid()) {
				if(!scene.IsAlive(hierarchy->parent)) {
					// 親破棄後の古い参照はrootへ戻し、毎フレーム同じ無効参照を評価しない。
					hierarchy->parent = {};
				} else if(auto* parentTransform = scene.GetComponent<TransformComponent>(hierarchy->parent)) {
					const Entity parent = scene.GetEntity(hierarchy->parent);
					self(self, parent, *parentTransform);
					if(!cycleDetected) {
						transform.worldMatrix = Multiply(transform.localMatrix, parentTransform->worldMatrix);
					}
				} else {
					// Transformを持たないEntityは階層計算上の親にできないため関係を解除する。
					hierarchy->parent = {};
				}
			}
			visitState = VisitState::Resolved;
		};

		scene.ForEach<TransformComponent>([&](Entity entity, TransformComponent& transform) {
			resolveWorld(resolveWorld, entity, transform);
		});
		if(cycleDetected) {
			return std::unexpected(Error(ErrorCategory::Scene, kHierarchyCycle, "Transform hierarchy cycle was detected during update."));
		}
		return {};
	}

} // namespace NexusEngine
