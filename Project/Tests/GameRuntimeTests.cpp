#include "Runtime/Application/GameApplication.h"
#include "Runtime/Application/ProjectSettings.h"
#include "Editor/Core/EditorSession.h"
#include "Runtime/Scene/Components/TransformComponent.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include "Game/SampleGame.h"
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

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
std::unique_ptr<Level> EmptyLevel() {
    auto level = std::make_unique<Level>(); assert(level->LoadEmpty());
    static_cast<void>(level->CreateEntity("Object")); return level;
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
    // 実行中の未準備Assetを誤ったNodeで描かず、エラーを伝える。
    game.Runtime().GetLevel()->GetScene()->ForEach<PrimitiveRenderComponent>([](Entity, PrimitiveRenderComponent& primitive) {
        primitive.shape = PrimitiveShape::Sphere;
    });
    assert(!game.PrepareRender(800, 600));
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
void RunGameRuntimeTests() {
    LifecycleAndIsolation(); FailedSetupRollsBack(); SampleParityAndGameAssets();
}
