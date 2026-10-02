// c++
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

// engine
#include "Runtime/Level/Level.h"
#include "Graphics/Model/ModelAssetData.h"
#include "Graphics/Model/ModelInstance.h"
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
		// SceneのCPU姿勢更新にGPU Resourceが不要なことを検証する。
		ModelAssetData asset;
		asset.nodes.emplace_back();
		AnimationClip clip;
		clip.duration = 1.0f;
		NodeAnimationChannel channel;
		channel.translations = { { 0.0f, { 0.0f, 0.0f, 0.0f }, {}, {} },
			{ 1.0f, { 2.0f, 0.0f, 0.0f }, {}, {} } };
		clip.channels.push_back(channel);
		asset.animations.push_back(clip);
		ModelInstance pose;
		assert(pose.Initialize(asset).has_value());
		assert(pose.Play(0).has_value());
		pose.Update(0.25f);
		assert(std::abs(pose.GetNodeWorldTransforms()[0].At(3, 0) - 0.5f) < 0.0001f);
		pose.Update(1.0f);
		assert(std::abs(pose.GetNodeWorldTransforms()[0].At(3, 0) - 0.5f) < 0.0001f);
		assert(pose.Play(0, false).has_value());
		pose.Update(2.0f);
		assert(pose.GetNodeWorldTransforms()[0].At(3, 0) == 2.0f);
		pose.Shutdown();
		asset.nodes[0].parentIndex = 0;
		assert(!pose.Initialize(asset).has_value()); // 不正な階層をGPU生成前に拒否する。
		asset.nodes[0].parentIndex = ModelNodeAssetData::kNoParent;
		asset.skins.push_back(ModelSkin { "invalid", { 1 }, { Matrix4x4::Identity() } });
		assert(!pose.Initialize(asset).has_value());

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
