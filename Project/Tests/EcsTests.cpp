// c++
#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>

// engine
#include "Runtime/Level/Level.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "Runtime/Rendering/RenderSceneExtractor.h"

namespace NexusEngine::Tests {
	namespace {
		struct TestComponent {
			int value = 0;
		};
		class TestMovementSystem final : public ISceneSystem {
		public:
			std::span<const std::type_index> GetReadComponents() const noexcept override { return {}; }
			std::span<const std::type_index> GetWriteComponents() const noexcept override {
				static const std::type_index type = typeid(TransformComponent);
				return { &type, 1 };
			}
			Result<void> Update(Scene& world, float) override {
				world.ForEach<TransformComponent>([](Entity, TransformComponent& value) { value.translation.x += 1.0f; });
				return {};
			}
		};
	}

	/**
	 * \brief ECSの主要な不変条件をassertで検証する開発用Test
	 * \note 専用Test runner導入後に自動実行へ接続する。Runtime本体からは呼び出さない
	 */
	void RunEcsTests() {
		Scene scene;
		Entity first = scene.CreateEntity("first");
		const EntityId staleId = first.GetId();
		assert(first.AddComponent<TestComponent>(TestComponent { 10 }) != nullptr);
		assert(scene.DestroyEntity(first));
		assert(!scene.IsAlive(staleId));

		Entity reused = scene.CreateEntity("reused");
		assert(reused.GetId().index == staleId.index);
		assert(reused.GetId().generation != staleId.generation);
		assert(scene.GetComponent<TestComponent>(staleId) == nullptr);

		Entity second = scene.CreateEntity("second");
		assert(reused.AddComponent<TestComponent>(TestComponent { 20 }) != nullptr);
		assert(second.AddComponent<TestComponent>(TestComponent { 30 }) != nullptr);
		assert(reused.RemoveComponent<TestComponent>());
		int sum = 0;
		scene.ForEach<TestComponent>([&](Entity, TestComponent& component) { sum += component.value; });
		assert(sum == 30); // swap-and-pop後も末尾ComponentとSparse indexが一致する。
		bool structureRejected = false;
		scene.ForEach<TestComponent>([&](Entity entity, TestComponent&) {
			structureRejected = !scene.DestroyEntity(entity) && !entity.RemoveComponent<TestComponent>();
			try { static_cast<void>(scene.CreateEntity()); } catch(const std::logic_error&) { return; }
			structureRejected = false;
		});
		assert(structureRejected);
		assert(second.GetComponent<TestComponent>()->value == 30);

		Level source("serialization-test");
		assert(source.LoadEmpty().has_value());
		Entity parent = source.CreateEntity("parent");
		Entity child = source.CreateEntity("child");
		assert(parent.IsAlive() && child.IsAlive());
		assert(child.AddComponent<HierarchyComponent>(HierarchyComponent { parent.GetId() }) != nullptr);
		const PersistentId savedParentId = parent.GetComponent<PersistentIdComponent>()->value;
		const std::filesystem::path path = std::filesystem::temp_directory_path() / "nexus_ecs_test.level";
		assert(source.Save(path).has_value());

		Level restored;
		assert(restored.Load(path).has_value());
		assert(restored.GetScene() != nullptr && restored.GetScene()->GetEntityCount() == 2);
		assert(restored.FindEntity(savedParentId).IsAlive());
		bool hierarchyWasRemapped = false;
		restored.GetScene()->ForEach<HierarchyComponent, PersistentIdComponent>(
			[&](Entity, HierarchyComponent& hierarchy, PersistentIdComponent&) {
				const auto* restoredParent = restored.GetScene()->GetComponent<PersistentIdComponent>(hierarchy.parent);
				hierarchyWasRemapped = restoredParent != nullptr && restoredParent->value == savedParentId;
			});
		assert(hierarchyWasRemapped);
		restored.GetSystemScheduler().AddSystem<TestMovementSystem>();
		assert(restored.Update(0.016f).has_value());
		restored.GetScene()->ForEach<TransformComponent>([](Entity entity, TransformComponent& value) {
			if(!entity.HasComponent<HierarchyComponent>()) assert(value.worldMatrix.At(3, 0) == 1.0f);
		});
		assert(!restored.Update(-1.0f).has_value());

		// 破損ファイルのロード失敗で、現在のWorldが置換されないことを保証する。
		Scene* previousScene = restored.GetScene();
		{
			std::ofstream invalid(path, std::ios::trunc);
			invalid << "NEXUS_LEVEL 1\nLEVEL \"broken\"\nENTITY";
		}
		assert(!restored.Load(path).has_value());
		assert(restored.GetScene() == previousScene && previousScene->GetEntityCount() == 2);
		assert(parent.AddComponent<HierarchyComponent>(HierarchyComponent { child.GetId() }) != nullptr);
		assert(!source.Save(path).has_value()); // 保存開始前に循環を拒否する。
		assert(parent.RemoveComponent<HierarchyComponent>());

		// 未対応Componentを追加した保存を黙って成功させない。
		assert(parent.AddComponent<TestComponent>() != nullptr);
		assert(!source.Save(path).has_value());

		Entity camera = source.CreateEntity("camera");
		assert(camera.AddComponent<CameraComponent>() != nullptr);
		assert(child.AddComponent<PrimitiveRenderComponent>() != nullptr);
		assert(source.Update(0.0f).has_value());
		RenderScene snapshot;
		RenderSceneExtractor extractor;
		assert(extractor.ExtractInto(*source.GetScene(), 1.5f, snapshot).has_value());
		assert(snapshot.camera.has_value() && snapshot.primitives.size() == 1);
		const size_t capacity = snapshot.primitives.capacity();
		assert(extractor.ExtractInto(*source.GetScene(), 1.5f, snapshot).has_value());
		assert(snapshot.primitives.capacity() == capacity);
		source.Unload();
		assert(source.GetState() == LevelState::Unloaded && source.GetScene() == nullptr);
		std::filesystem::remove(path);
	}

} // namespace NexusEngine::Tests

#if defined(NEXUS_ECS_TEST_MAIN)
int main() {
	NexusEngine::Tests::RunEcsTests();
	return 0;
}
#endif
