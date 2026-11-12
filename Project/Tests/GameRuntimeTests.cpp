#include "Runtime/Application/GameApplication.h"
#include "Runtime/Application/ProjectSettings.h"
#include "Editor/Core/EditorSession.h"
#include "Runtime/Scene/Components/TransformComponent.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include "Game/SampleGame.h"
#include "Runtime/Gameplay/GameApiBridge.h"
#include "Public/Nexus/GameWorldControl.hpp"
#include <fstream>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace NexusEngine;
namespace {
struct Counts { int setups = 0, updates = 0, alive = 0; };
class CountingSystem final : public ISceneSystem {
public:
    explicit CountingSystem(Counts& counts) : counts_(counts) { ++counts_.alive; }
    ~CountingSystem() override { --counts_.alive; }
    std::span<const std::type_index> GetReadComponents() const noexcept override { return {}; }
    std::span<const std::type_index> GetWriteComponents() const noexcept override {
        static const std::array types{std::type_index(typeid(TransformComponent))}; return types;
    }
    Result<void> Update(Scene& scene, float dt) override {
        ++counts_.updates;
        scene.ForEach<TransformComponent>([&](Entity, TransformComponent& transform) { transform.translation.x += dt; });
        return {};
    }
private:
    Counts& counts_;
};
/* RuntimeがSystemの失敗・例外を通知し、実行stackを抜けた後に所有Systemを解放する検証用。 */
class FailingSystem final : public ISceneSystem {
public:
    FailingSystem(Counts& counts, bool throws) : counts_(counts), throws_(throws) { ++counts_.alive; }
    ~FailingSystem() override { --counts_.alive; }
    std::span<const std::type_index> GetReadComponents() const noexcept override { return {}; }
    std::span<const std::type_index> GetWriteComponents() const noexcept override { return {}; }
    Result<void> Update(Scene&, float) override {
        ++counts_.updates;
        if(throws_) throw std::runtime_error("Expected System update failure");
        return std::unexpected(Error(ErrorCategory::Scene, 77, "Expected System error"));
    }
private:
    Counts& counts_; //< Runtime停止まで生存する検証用カウンタ
    bool throws_; //< Result失敗とC++例外の両経路を確認する
};
std::unique_ptr<Level> EmptyLevel() {
    auto level = std::make_unique<Level>(); assert(level->LoadEmpty());
    static_cast<void>(level->CreateEntity("Object")); return level;
}
/** \brief InteropなしのCpp-onlyも、誤Threadの要求からWorldを更新・破棄せず所有Threadで再開する検証ソース */
void RuntimeThreadOwnership() {
    Counts counts; GameRuntime runtime; bool configured = false;
    // World開始前の値設定は逐次handoffできる。構築Threadへの固定で既存の開始準備を狭めない。
    std::thread configure([&] {
        configured = runtime.SetLevelSetup([&](Level& level) -> Result<void> {
            ++counts.setups; level.GetSystemScheduler().AddSystem<CountingSystem>(counts); return {};
        });
    }); configure.join(); assert(configured && runtime.Start(EmptyLevel()));
    auto* world = runtime.GetLevel(); bool updateDenied = false, reloadDenied = false, prepareDenied = false, finishDenied = false;
    std::thread foreign([&] {
        const auto updated = runtime.Update(.25f); updateDenied = !updated && updated.error().GetCode() == 45;
        runtime.Stop(); runtime.TogglePause(); runtime.AbandonNativePatch();
        const auto reload = runtime.ReloadCSharp(std::filesystem::absolute("Generated/UnusedThreadProbe.dll")); reloadDenied = !reload && reload.error().GetCode() == 45;
        const auto prepare = runtime.PrepareNativePatch(); prepareDenied = !prepare && prepare.error().GetCode() == 45;
        const auto finish = runtime.FinishNativePatch(false); finishDenied = !finish && finish.error().GetCode() == 45;
        assert(!runtime.ValidateNativePatch());
    }); foreign.join();
    assert(updateDenied && reloadDenied && prepareDenied && finishDenied);
    assert(runtime.IsRunning() && runtime.GetLevel() == world && !runtime.IsPaused() && runtime.NativePatchStatus() == NativePatchState::Idle);
    assert(counts.alive == 1 && counts.updates == 0 && runtime.Update(.5f) && counts.updates == 1);
    runtime.TogglePause(); std::thread pausedForeign([&] { runtime.TogglePause(); runtime.Stop(); }); pausedForeign.join();
    assert(runtime.IsRunning() && runtime.IsPaused() && counts.alive == 1);
    runtime.TogglePause(); runtime.Stop(); assert(!runtime.IsRunning() && counts.alive == 0);
    bool setupDenied = false, nativeDenied = false, modulesDenied = false, managedDenied = false, restartDenied = false;
    std::thread stoppedForeign([&] {
        setupDenied = !runtime.SetLevelSetup({}); nativeDenied = !runtime.SetNativeGameModule({});
        modulesDenied = !runtime.SetNativeGameModules({}); managedDenied = !runtime.SetCSharpRuntime({});
        const auto restarted = runtime.Start(EmptyLevel()); restartDenied = !restarted && restarted.error().GetCode() == 45;
    }); stoppedForeign.join();
    assert(setupDenied && nativeDenied && modulesDenied && managedDenied && restartDenied && !runtime.IsRunning() && counts.setups == 1);
    assert(runtime.Start(EmptyLevel()) && counts.setups == 2); runtime.Stop(); assert(counts.alive == 0);
}
void LifecycleAndIsolation() {
    Counts counts;
    const GameLevelSetup setup = [&](Level& level) -> Result<void> {
        ++counts.setups;
        level.GetSystemScheduler().AddSystem<CountingSystem>(counts);
        return {};
    };
    EditorSession editor; assert(editor.Create());
    auto entity = editor.GetLevel()->CreateEntity("Object");
    const auto id = entity.GetComponent<PersistentIdComponent>()->value;
    assert(editor.SetGameLevelSetup(setup));
    assert(editor.UpdatePreview(1)); assert(counts.setups == 0 && counts.updates == 0);
    assert(editor.StartPlay()); assert(counts.setups == 1 && counts.alive == 1 && counts.updates == 0);
    assert(!editor.SetGameLevelSetup({})); assert(!editor.StartPlay());
    assert(editor.UpdatePreview(0.5f));
    auto* played = editor.GetPlayLevel()->FindEntity(id).GetComponent<TransformComponent>();
    assert(played->translation.x == 0.5f);
    assert(played->worldMatrix.At(3, 0) == 0.5f);
    assert(entity.GetComponent<TransformComponent>()->translation.x == 0);
    editor.TogglePause(); assert(editor.UpdatePreview(1)); assert(counts.updates == 1);
    assert(!editor.UpdatePreview(std::numeric_limits<float>::quiet_NaN()));
    editor.TogglePause(); assert(editor.UpdatePreview(0.5f)); assert(played->translation.x == 1);
    editor.StopPlay(); assert(counts.alive == 0 && !editor.IsPaused());
    assert(editor.StartPlay()); assert(counts.setups == 2);
    assert(editor.GetPlayLevel()->FindEntity(id).GetComponent<TransformComponent>()->translation.x == 0);
    editor.Close(); assert(counts.alive == 0);

    GameRuntime standalone; assert(standalone.SetLevelSetup(setup));
    assert(!standalone.Start(nullptr));
    assert(standalone.Start(EmptyLevel())); assert(counts.setups == 3);
    assert(!standalone.Start(EmptyLevel()));
    assert(!standalone.Update(-1));
    standalone.TogglePause(); assert(standalone.Update(1)); assert(counts.updates == 2);
    standalone.TogglePause(); assert(standalone.Update(1)); assert(counts.updates == 3);
    standalone.Stop(); standalone.Stop(); assert(counts.alive == 0);
}
void FailedSetupRollsBack() {
    Counts counts;
    EditorSession editor; assert(editor.Create());
    auto* original = editor.GetLevel()->GetScene();
    assert(editor.SetGameLevelSetup([&](Level& level) -> Result<void> {
        level.GetSystemScheduler().AddSystem<CountingSystem>(counts);
        return std::unexpected(Error(ErrorCategory::General, 99, "Expected setup failure"));
    }));
    assert(!editor.StartPlay()); assert(editor.GetPlayError());
    assert(editor.GetPlayError()->GetCode() == 99 && counts.alive == 0);
    assert(editor.GetLevel()->GetScene() == original && !editor.IsPlaying());
    assert(editor.SetGameLevelSetup([](Level&) -> Result<void> { throw std::runtime_error("Setup exception"); }));
    assert(!editor.StartPlay()); assert(editor.GetPlayError());
    assert(editor.SetGameLevelSetup({})); assert(editor.StartPlay()); assert(!editor.GetPlayError());
    editor.StopPlay();
    GameRuntime runtime;
    assert(runtime.SetLevelSetup([](Level& level) -> Result<void> { level.Unload(); return {}; }));
    assert(!runtime.Start(EmptyLevel()) && !runtime.IsRunning());
}
/** \brief Setup取消でNativeを開始しないことと、System失敗後に次frameを実行しないことを確認するソース */
void CancellationAndUpdateFailure() {
    Counts cancelledCounts;
    GameRuntime cancelled;
    assert(cancelled.SetNativeGameModule({})); // ここへ到達すればNative開始は配置エラーになるsentinel。
    assert(cancelled.SetLevelSetup([&](Level& level) -> Result<void> {
        ++cancelledCounts.setups;
        level.GetSystemScheduler().AddSystem<CountingSystem>(cancelledCounts);
        cancelled.Stop(); return {};
    }));
    const auto start = cancelled.Start(EmptyLevel());
    assert(!start && start.error().GetCode() == 234 && !cancelled.IsRunning());
    assert(cancelledCounts.setups == 1 && cancelledCounts.alive == 0); // Setupを実行し、取消WorldのSystemも解放する。
    assert(cancelled.SetNativeGameModules({}) && cancelled.SetLevelSetup({}));
    assert(cancelled.Start(EmptyLevel())); cancelled.Stop();
    for(const bool throws : {false, true}) {
        Counts counts; GameRuntime runtime;
        assert(runtime.SetLevelSetup([&](Level& level) -> Result<void> { level.GetSystemScheduler().AddSystem<FailingSystem>(counts, throws); return {}; }));
        assert(runtime.Start(EmptyLevel())); assert(counts.alive == 1);
        const auto update = runtime.Update(.1f);
        assert(!update && update.error().GetCode() == (throws ? 44 : 77));
        assert(!runtime.IsRunning() && counts.alive == 0 && counts.updates == 1);
        assert(runtime.Update(.1f) && counts.updates == 1);
        assert(runtime.SetLevelSetup({}) && runtime.Start(EmptyLevel())); runtime.Stop();
    }
}
/** \brief .NET未導入でも開始前の配置検証・編集World保持・Hybrid設定を検証できる回帰ケース */
void ManagedConfigurationFailureRollsBack() {
    const auto absent = std::filesystem::absolute("Generated/NoManagedHostingFiles");
    const CSharpRuntimeSettings settings{absent / "nethost.dll", absent / "Bootstrap.runtimeconfig.json", absent / "Bootstrap.dll", {}};
    EditorSession editor; assert(editor.Create());
    const auto count = editor.GetLevel()->GetScene()->GetEntityCount();
    assert(editor.SetCSharpRuntime(settings));
    assert(!editor.StartPlay() && editor.GetPlayError());
    assert(!editor.IsPlaying() && editor.GetLevel()->GetScene()->GetEntityCount() == count);
    assert(!editor.StartPlay()); // 失敗したManaged HostやWorldを保持せず、再試行の検証も可能である。
    GameRuntime managed; assert(managed.SetCSharpRuntime(settings));
    assert(managed.SetNativeGameModule({}));
    assert(!managed.Start(EmptyLevel()) && !managed.IsRunning());
    GameRuntime native; assert(native.SetNativeGameModule({}));
    assert(native.SetCSharpRuntime(settings));
}
void SampleParityAndGameAssets() {
    const auto file = std::filesystem::path{"Project/Resources/Assets/Levels/RuntimeDemoLevel.json"};
    const auto root = std::filesystem::path{"Project/Resources/Assets"};
    EditorSession editor; assert(editor.Open(file));
    assert(editor.SetGameLevelSetup(NexusGame::SetupSampleGame)); assert(editor.StartPlay());
    GameApplication game; assert(game.SetGameLevelSetup(NexusGame::SetupSampleGame));
    int legacyLoads = 0;
    assert(game.Load(file, root, [&]() -> Result<ModelAssetData> { ++legacyLoads; return ModelAssetData{}; }));
    assert(legacyLoads == 0); // 組み込み形状のゲームにSponzaは不要。
    assert(!game.Load(file, root));
    assert(game.Update(0.25f)); assert(editor.UpdatePreview(0.25f));
    assert(game.PrepareRender(1280, 720));
    // The standalone host must keep the same weak, unoccluded sky as editor Play.
    // Full-strength fallback sky washes out direct-light shadows after tone mapping.
    assert(game.GetRenderScene().fallbackEnvironmentIntensity == .05f);
    assert(!game.GetRenderScene().ddgi.enabled && !game.GetRenderScene().globalIllumination.enabled &&
           !game.GetRenderScene().bakedLighting.enabled);
    RenderScene snapshot;
    assert(RenderSceneExtractor{}.ExtractInto(*editor.GetPlayLevel()->GetScene(), 1280.f/720.f, snapshot));
    assert(snapshot.primitives.size() == 1 && game.GetRenderScene().primitives.size() == 1);
    const auto& expected = snapshot.primitives[0].worldMatrix;
    const auto& actual = game.GetRenderScene().primitives[0].worldMatrix;
    for(int row = 0; row < 4; ++row) for(int column = 0; column < 4; ++column)
        assert(std::abs(expected.At(row, column) - actual.At(row, column)) < 0.00001f);
    assert(game.GetRenderScene().primitives[0].nodeEnd > 0);
    game.Runtime().TogglePause(); assert(game.Update(1)); assert(game.PrepareRender(800, 600));
    assert(game.GetRenderScene().camera);
    // 実行中の新しい形状も既存asset準備経路で公開し、正しいNode範囲で描く。
    game.Runtime().GetLevel()->GetScene()->ForEach<PrimitiveRenderComponent>([](Entity, PrimitiveRenderComponent& primitive) {
        primitive.shape = PrimitiveShape::Sphere;
    });
    assert(game.PrepareRender(800, 600));
    assert(!game.GetRenderScene().primitives.empty() && game.GetRenderScene().primitives.front().nodeEnd>game.GetRenderScene().primitives.front().nodeBegin);
    game.Runtime().Stop(); assert(game.PrepareRender(800, 600));
    assert(!game.GetRenderScene().camera && game.GetRenderScene().primitives.empty());
    GameApplication missing;
    assert(!missing.Load("does-not-exist.level", root)); assert(!missing.Runtime().IsRunning());
    assert(missing.Load(file, root));
    GameApplication noCamera;
    assert(noCamera.SetGameLevelSetup([](Level& level) -> Result<void> {
        level.GetScene()->ForEach<CameraComponent>([](Entity, CameraComponent& camera) { camera.enabled = false; }); return {};
    }));
    assert(!noCamera.Load(file, root)); assert(!noCamera.Runtime().IsRunning());
    assert(noCamera.SetGameLevelSetup({})); assert(noCamera.Load(file, root));
    const auto project = LoadProjectSettings("Project"); assert(project && !project->startupLevel.empty());
    assert(!LoadProjectSettings("Generated/NoSuchProject"));
}
}
void RunNativeGameModuleTests();
void RunManagedReflectionTests();
void RunGameServicesTests();
void RunNativeGameSystemTests();
void RunNativeComponentTests();
/** \brief callback終了後のLevel交換、受理/読込失敗、共通asset照会を確認する */
void WorldTransitionRequests() {
    const auto root=std::filesystem::absolute("Generated/WorldTransitionTests"); std::filesystem::create_directories(root);
    Level destination("Destination"); assert(destination.LoadEmpty());
    auto object=destination.CreateEntity("Destination object"); const auto saved=object.GetComponent<PersistentIdComponent>()->value;
    assert(destination.Save(root/"Next.level"));
    { std::ofstream invalid(root/"Broken.level"); invalid << "invalid level"; }
    Counts counts; GameRuntime runtime;
    assert(runtime.SetLevelSetup([&](Level& level)->Result<void> { ++counts.setups; level.GetSystemScheduler().AddSystem<CountingSystem>(counts); return {}; }));
    auto first=EmptyLevel(); assert(first->SetResourceRoot(root)); assert(runtime.Start(std::move(first)));
    auto* previous=runtime.GetLevel()->GetScene();
    {
        GameApiBridge bridge(*runtime.GetLevel()); uint64_t client=0;
        assert(GameApiBridge::OpenNativeServices(bridge.World(),&client)==NEXUS_GAME_OK);
        NexusGame::WorldControl world({1,sizeof(NexusGameServicesContextV1),bridge.World(),client,GameApiBridge::Services()});
        bool exists=false; assert(world.AssetExists({"Next.level",10},exists)==NEXUS_GAME_OK && exists);
        assert(world.AssetExists({"missing.bin",11},exists)==NEXUS_GAME_OK && !exists);
        assert(world.AssetExists({"../outside",10},exists)==NEXUS_GAME_INVALID_ARGUMENT);
        assert(bridge.FreezeManagedServices(true));
        assert(world.RequestLevel({"Next.level",10})==NEXUS_GAME_FAILED);
        assert(world.AssetExists({"Next.level",10},exists)==NEXUS_GAME_OK && exists);
        assert(bridge.FreezeManagedServices(false));
        assert(world.RequestLevel({"Next.level",10})==NEXUS_GAME_OK && runtime.GetLevel()->GetScene()==previous);
        assert(world.RequestLevel({"Broken.level",12})==NEXUS_GAME_INVALID_ARGUMENT);
    }
    assert(runtime.Update(0.1f));
    assert(runtime.GetLevel()->GetScene()!=previous && runtime.GetLevel()->FindEntity(saved).IsAlive());
    assert(counts.setups==2 && counts.alive==1);
    previous=runtime.GetLevel()->GetScene(); assert(runtime.GetLevel()->RequestLevel("Broken.level"));
    assert(runtime.Update(0.1f) && runtime.GetLevel()->GetScene()==previous && counts.setups==2);
    runtime.Stop(); assert(counts.alive==0);
}

void RunGameRuntimeTests() {
    WorldTransitionRequests();
    LifecycleAndIsolation(); RuntimeThreadOwnership(); FailedSetupRollsBack(); CancellationAndUpdateFailure(); SampleParityAndGameAssets();
    ManagedConfigurationFailureRollsBack();
    RunNativeGameModuleTests();
    RunManagedReflectionTests();
    RunGameServicesTests();
    RunNativeGameSystemTests();
    RunNativeComponentTests();
}
