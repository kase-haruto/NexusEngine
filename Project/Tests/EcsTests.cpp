// c++
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdlib>
#include <crtdbg.h>

// engine
#include "Runtime/Level/Level.h"
#include "Runtime/Scene/Components/AnimationPlayerComponent.h"
#include "Graphics/Model/ModelAssetData.h"
#include "Graphics/Model/ModelAssetReference.h"
#include "Graphics/Model/ModelInstance.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/DirectionalLightComponent.h"
#include "Runtime/Scene/Components/SkyRendererComponent.h"
#include "Runtime/Scene/Components/FogComponent.h"
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
        {
            Level level("Animation players"); assert(level.LoadEmpty());
            auto first = level.CreateEntity("First");
            auto second = level.CreateEntity("Second");
            assert(first.AddComponent<PrimitiveRenderComponent>());
            assert(second.AddComponent<PrimitiveRenderComponent>());
            assert(first.AddComponent<AnimationPlayerComponent>());
            assert(second.AddComponent<AnimationPlayerComponent>());
            // Fetch after additions: packed component storage may have reallocated.
            auto* a = first.GetComponent<AnimationPlayerComponent>();
            auto* b = second.GetComponent<AnimationPlayerComponent>();
            b->playbackSpeed = 2;
            assert(level.Update(.25f));
            assert(a->time == .25f && b->time == .5f);
            a->Pause(); assert(level.Update(.25f));
            assert(a->time == .25f && b->time == 1);
            a->Resume(); assert(level.Update(.25f)); assert(a->time == .5f);
            b->Stop(); assert(b->time == 0 && !b->playing);
            b->Play(2); assert(b->time == 0 && b->animationIndex == 2 && b->playing);
            RenderScene frame; RenderSceneExtractor extractor;
            assert(extractor.ExtractInto(*level.GetScene(), 1, frame));
            assert(frame.primitives.size() == 2 && frame.primitives[0].animation);
            assert(frame.primitives[0].animation->time == .5f && !frame.primitives[0].staticLighting);
            const auto id = first.GetComponent<PersistentIdComponent>()->value;
            const auto path = std::filesystem::path("Generated/AnimationPlayer.level");
            std::filesystem::create_directories(path.parent_path());
            a->loop = false; assert(level.Save(path));
            Level restored; assert(restored.Load(path));
            auto* saved = restored.FindEntity(id).GetComponent<AnimationPlayerComponent>();
            assert(saved && saved->time == .5f && saved->playing && !saved->loop);
            auto clone = level.GetScene()->Clone(); assert(clone);
            assert(extractor.ExtractInto(*clone, 1, frame));
            assert(frame.primitives[0].animation->time == .5f);
            assert(level.Update(.25f));
            assert(extractor.ExtractInto(*clone, 1, frame));
            assert(frame.primitives[0].animation->time == .5f); // PIE world isolation.
            saved->playbackSpeed = -1; assert(!restored.Save(path));
            assert(first.RemoveComponent<AnimationPlayerComponent>());
            assert(extractor.ExtractInto(*level.GetScene(), 1, frame));
            assert(!frame.primitives[0].animation && frame.primitives[0].staticLighting);
        }
        {
            Level level("Fog scene"); assert(level.LoadEmpty());
            RenderScene frame; RenderSceneExtractor extractor;
            frame.fog.enabled = true;
            assert(extractor.ExtractInto(*level.GetScene(), 1, frame) && !frame.fog.enabled);
            auto entity = level.CreateEntity("Fog");
            auto id = entity.GetComponent<PersistentIdComponent>()->value;
            auto* fog = entity.AddComponent<FogComponent>();
            fog->startDistance = 3; fog->endDistance = 25; fog->color = {.1f, .3f, .5f};
            assert(extractor.ExtractInto(*level.GetScene(), 1, frame));
            assert(frame.fog.enabled && frame.fog.startDistance == 3 && frame.fog.endDistance == 25);
            const auto path = std::filesystem::path("Generated/FogComponent.level");
            std::filesystem::create_directories(path.parent_path()); assert(level.Save(path));
            Level restored; assert(restored.Load(path));
            auto* saved = restored.FindEntity(id).GetComponent<FogComponent>();
            assert(saved && saved->color == frame.fog.color && saved->startDistance == 3 && saved->endDistance == 25);
            saved->endDistance = saved->startDistance;
            assert(!extractor.ExtractInto(*restored.GetScene(), 1, frame));
            saved->endDistance = 25; saved->enabled = false;
            assert(extractor.ExtractInto(*restored.GetScene(), 1, frame) && !frame.fog.enabled);
            saved->enabled = true;
            auto second = restored.CreateEntity("Second Fog"); assert(second.AddComponent<FogComponent>());
            assert(!extractor.ExtractInto(*restored.GetScene(), 1, frame));
            assert(restored.DestroyEntity(second));
            assert(restored.FindEntity(id).RemoveComponent<FogComponent>());
            assert(extractor.ExtractInto(*restored.GetScene(), 1, frame) && !frame.fog.enabled);
        }
        {
            Level level("Sky scene"); assert(level.LoadEmpty());
            RenderScene frame; RenderSceneExtractor extractor;
            assert(extractor.ExtractInto(*level.GetScene(), 1, frame));
            assert(!frame.skyIntensity);
            auto entity = level.CreateEntity("Sky");
            auto id = entity.GetComponent<PersistentIdComponent>()->value;
            entity.AddComponent<SkyRendererComponent>()->intensity = 0.75f;
            assert(extractor.ExtractInto(*level.GetScene(), 1, frame));
            assert(frame.skyIntensity == 0.75f);
            const auto path = std::filesystem::path("Generated/SkyComponent.level");
            std::filesystem::create_directories(path.parent_path());
            assert(level.Save(path));
            Level restored; assert(restored.Load(path));
            auto* sky = restored.FindEntity(id).GetComponent<SkyRendererComponent>();
            assert(sky && sky->enabled && sky->intensity == 0.75f);
            auto clone = restored.GetScene()->Clone();
            assert(clone && extractor.ExtractInto(*clone, 1, frame));
            assert(frame.skyIntensity == 0.75f);
            sky->enabled = false;
            assert(extractor.ExtractInto(*restored.GetScene(), 1, frame) && !frame.skyIntensity);
            sky->enabled = true; sky->intensity = -1;
            assert(!extractor.ExtractInto(*restored.GetScene(), 1, frame));
            assert(!restored.Save(path));
            sky->intensity = 1;
            auto second = restored.CreateEntity("Second Sky");
            assert(second.AddComponent<SkyRendererComponent>());
            assert(!extractor.ExtractInto(*restored.GetScene(), 1, frame));
            assert(restored.DestroyEntity(second));
            assert(restored.FindEntity(id).RemoveComponent<SkyRendererComponent>());
            assert(extractor.ExtractInto(*restored.GetScene(), 1, frame) && !frame.skyIntensity);
            for(const auto* pathName : {"Project/Templates/Blank/Startup.level", "Project/Templates/Basic3D/Startup.level"}) {
                Level defaults; assert(defaults.Load(pathName)); assert(defaults.Update(0));
                assert(extractor.ExtractInto(*defaults.GetScene(), 1, frame));
                assert(frame.skyIntensity == 1 && frame.camera);
            }
        }
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
        assert(pose.Sample(0, 2.25f, true));
        assert(std::abs(pose.GetNodeWorldTransforms()[0].At(3, 0) - .5f) < .0001f);
        assert(pose.Sample(0, 20, false));
        assert(pose.GetNodeWorldTransforms()[0].At(3, 0) == 2);
        ModelInstance independentPose; assert(independentPose.Initialize(asset));
        assert(independentPose.Sample(0, .75f));
        assert(independentPose.GetNodeWorldTransforms()[0].At(3, 0) == 1.5f);
        assert(pose.GetNodeWorldTransforms()[0].At(3, 0) == 2);
        assert(!pose.Sample(1, 0)); assert(!pose.Sample(0, -1));
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
		const auto path = std::filesystem::temp_directory_path() /
			("nexus_ecs_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".level");
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

		// 独立したNodeを抽出し、一方の編集が他方のWorldへ伝播しない。
		child.GetComponent<PrimitiveRenderComponent>()->nodeIndex = 3;
		Entity independent = source.CreateEntity("independent node");
		auto* independentPrimitive = independent.AddComponent<PrimitiveRenderComponent>();
		assert(independentPrimitive != nullptr);
		independentPrimitive->nodeIndex = 7;
		independentPrimitive->staticLighting = false;
		child.GetComponent<TransformComponent>()->translation.x = 12.0f;
		assert(source.Update(0.0f).has_value());
		assert(extractor.ExtractInto(*source.GetScene(), 1.5f, snapshot).has_value());
		assert(snapshot.primitives.size() == 2);
		bool foundIndependent = false;
		bool foundEdited = false;
		for(const auto& item : snapshot.primitives) {
			if(item.nodeIndex == 7) {
				foundIndependent = true;
				assert(item.worldMatrix.At(3, 0) == 0.0f);
				assert(!item.staticLighting && item.lightingId != 0);
			}
			if(item.nodeIndex == 3) {
				foundEdited = true;
				assert(item.worldMatrix.At(3, 0) == 12.0f);
			}
		}
		assert(foundIndependent && foundEdited);
		assert(parent.RemoveComponent<TestComponent>());
		assert(source.Save(path).has_value());
		Level nodeRestored;
		assert(nodeRestored.Load(path).has_value());
		const auto independentId = independent.GetComponent<PersistentIdComponent>()->value;
		assert(nodeRestored.FindEntity(independentId).GetComponent<PrimitiveRenderComponent>()->nodeIndex == 7);
		assert(!nodeRestored.FindEntity(independentId).GetComponent<PrimitiveRenderComponent>()->staticLighting);
		RenderScene restoredSnapshot;
		assert(extractor.ExtractInto(*nodeRestored.GetScene(),1.5f,restoredSnapshot).has_value());
		for(const auto& original:snapshot.primitives) for(const auto& restoredItem:restoredSnapshot.primitives)
			if(original.nodeIndex==restoredItem.nodeIndex) assert(original.lightingId==restoredItem.lightingId);
		source.Unload();
		assert(source.GetState() == LevelState::Unloaded && source.GetScene() == nullptr);
		std::filesystem::remove(path);

		Level lighting;
		assert(lighting.LoadEmpty().has_value());
		auto sun = lighting.CreateEntity("sun");
		auto* light = sun.AddComponent<DirectionalLightComponent>();
		assert(light);
		light->light.direction = {2, 0, 0}; light->light.color = {1, 0.5f, 0.25f}; light->light.intensity = 4;
		sun.GetComponent<TransformComponent>()->rotation = Quaternion::FromEulerRadians({0, 0, 1.5707963268f});
		assert(lighting.Update(0).has_value());
		RenderScene litScene;
		assert(extractor.ExtractInto(*lighting.GetScene(), 1, litScene).has_value());
		assert(std::abs(litScene.directionalLight.direction[1] - 1) < 1e-5f && litScene.directionalLight.intensity == 4);
		assert(lighting.Save(path).has_value());
		Level restoredLighting;
		assert(restoredLighting.Load(path).has_value());
		auto restoredSun = restoredLighting.FindEntity(sun.GetComponent<PersistentIdComponent>()->value);
		assert(restoredSun.GetComponent<DirectionalLightComponent>()->light.color[1] == 0.5f);
		light->enabled = false;
		assert(extractor.ExtractInto(*lighting.GetScene(), 1, litScene).has_value());
		assert(litScene.directionalLight.intensity == 0);
		light->enabled = true; light->light.direction = {0, 0, 0};
		assert(!extractor.ExtractInto(*lighting.GetScene(), 1, litScene).has_value());
		assert(!lighting.Save(path).has_value());
		light->light.direction = {1, 0, 0};
		auto secondSun = lighting.CreateEntity("second sun");
		assert(secondSun.AddComponent<DirectionalLightComponent>());
		assert(!extractor.ExtractInto(*lighting.GetScene(), 1, litScene).has_value());
		std::filesystem::remove(path);

		// 制作中のDemoLevelの内容ではなく、テスト専用の起動Worldを保存・復元する。
		const auto buildFixture = [](Level& level, uint32_t first, uint32_t count) {
			assert(level.LoadEmpty());
			assert(level.CreateEntity("Camera").AddComponent<CameraComponent>());
			assert(level.CreateEntity("Light").AddComponent<DirectionalLightComponent>());
			for(uint32_t node = first; node < first + count; ++node) {
				auto* primitive = level.CreateEntity("Mesh").AddComponent<PrimitiveRenderComponent>();
				assert(primitive); primitive->nodeIndex = node; primitive->meshAsset = kStartupModelAssetReference;
			}
		};
		Level startup("DemoLevel"); buildFixture(startup, 0, 3);
		assert(startup.Save(path));
		Level demo;
		assert(demo.Load(path).has_value());
		assert(demo.GetName() == "DemoLevel");
		assert(demo.Update(0).has_value());
		RenderScene demoSnapshot;
		assert(extractor.ExtractInto(*demo.GetScene(), 16.0f / 9.0f, demoSnapshot).has_value());
		assert(demoSnapshot.camera.has_value() && demoSnapshot.primitives.size() == 3);
		assert(demoSnapshot.directionalLight.intensity > 0);
		bool demoNodes[3] {};
		for(const auto& primitive : demoSnapshot.primitives) {
			assert(primitive.nodeIndex < 3);
			demoNodes[primitive.nodeIndex] = true;
		}
		assert(demoNodes[0] && demoNodes[1] && demoNodes[2]);
		Level pbrFixture("PBRTestLevel"); buildFixture(pbrFixture, 3, 13);
		pbrFixture.Lighting().emplace();
		pbrFixture.Lighting()->environment.imagePath = "Resources/Assets/Environments/PBRStudio.hdr";
		assert(pbrFixture.Save(path));
		Level pbr;
		assert(pbr.Load(path).has_value());
		assert(pbr.Update(0).has_value());
		RenderScene pbrSnapshot;
		assert(extractor.ExtractInto(*pbr.GetScene(),16.f/9.f,pbrSnapshot).has_value());
		assert(pbrSnapshot.camera && pbrSnapshot.primitives.size()==13 && pbrSnapshot.directionalLight.intensity>0);
		for(const auto& primitive : pbrSnapshot.primitives) assert(primitive.nodeIndex>=3 && primitive.nodeIndex<16);
		assert(pbr.Save(path).has_value());
		Level pbrRestored;
		assert(pbrRestored.Load(path).has_value());
		assert(pbrRestored.GetName()=="PBRTestLevel");
		assert(pbr.Lighting() && pbr.Lighting()->environment.imagePath=="Resources/Assets/Environments/PBRStudio.hdr");
		assert(pbrRestored.Lighting()->environment.imagePath==pbr.Lighting()->environment.imagePath);
		Level sponzaFixture("SponzaLevel"); buildFixture(sponzaFixture, 0, 3);
		sponzaFixture.Lighting().emplace();
		sponzaFixture.Lighting()->skyIrradiance = {4, 4.8f, 6.4f};
		sponzaFixture.Lighting()->probes.counts = {16, 6, 8};
		sponzaFixture.Lighting()->probes.raysPerProbe = 64;
		assert(sponzaFixture.Save(path));
		Level sponzaLevel;
		assert(sponzaLevel.Load(path).has_value());
		assert(sponzaLevel.Lighting() && sponzaLevel.Lighting()->IsValid());
		assert(sponzaLevel.Lighting()->skyIrradiance[0]==4 && sponzaLevel.Lighting()->probes.counts[0]==16);
		assert(sponzaLevel.Lighting()->probes.raysPerProbe==64);
		assert(sponzaLevel.Save(path).has_value());
		Level restoredSponza;
		assert(restoredSponza.Load(path).has_value());
		assert(restoredSponza.Lighting()->probes.counts==sponzaLevel.Lighting()->probes.counts);
		assert(restoredSponza.Lighting()->skyIrradiance==sponzaLevel.Lighting()->skyIrradiance);
		assert(restoredSponza.Update(0).has_value());
		RenderScene sponzaSnapshot;
		assert(extractor.ExtractInto(*restoredSponza.GetScene(),16.f/9.f,sponzaSnapshot).has_value());
		assert(sponzaSnapshot.camera && sponzaSnapshot.primitives.size()==3);
		assert(sponzaSnapshot.directionalLight.intensity>0);
		restoredSponza.Lighting()->skyIrradiance[0]=-1;
		assert(!restoredSponza.Save(path).has_value());
		std::filesystem::remove(path);
	}

} // namespace NexusEngine::Tests

#if defined(NEXUS_ECS_TEST_MAIN)
int main() {
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	NexusEngine::Tests::RunEcsTests();
	return 0;
}
#endif
