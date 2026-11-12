#include "Runtime/Gameplay/GameApiBridge.h"
#include "Runtime/Gameplay/GameModuleLoader.h"
#include "Runtime/Application/GameRuntime.h"
#include "Editor/Core/EditorSession.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include "Runtime/Scene/Components/ScriptComponent.h"
#include "Foundation/Logging/Logger.h"
#include <cassert>
#include <limits>
#include <thread>
#include <fstream>
#include <string_view>
#include <cstring>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

using namespace NexusEngine;
namespace {
/** \brief 共通APIが既存ECSの世代とWorld寿命を検証することを確認する */
void ApiHandleIsolation() {
    Level level; assert(level.LoadEmpty());
    const auto api = GameApiBridge::Api();
    NexusEntityHandle expired{};
    {
        GameApiBridge bridge(level);
        assert(api.createEntity(bridge.World(), {"API Entity", 10}, &expired) == NEXUS_GAME_OK);
        assert(level.GetScene()->GetEntityCount() == 1);
        const auto entity = level.GetScene()->GetEntity({expired.index, expired.generation});
        assert(entity.HasComponent<PersistentIdComponent>());
        NexusLocalTransform value{{1, 2, 3}, {0, 0, 0, 2}, {1, 1, 1}};
        assert(api.setLocalTransform(expired, &value) == NEXUS_GAME_OK);
        assert(entity.GetComponent<TransformComponent>()->translation.x == 1);
        assert(entity.GetComponent<TransformComponent>()->rotation.w == 1);
        value.translation[0] = std::numeric_limits<float>::quiet_NaN();
        assert(api.setLocalTransform(expired, &value) == NEXUS_GAME_INVALID_ARGUMENT);
        assert(entity.GetComponent<TransformComponent>()->translation.x == 1);
        uint32_t count = 0;
        assert(api.entities(bridge.World(), nullptr, 0, &count) == NEXUS_GAME_BUFFER_TOO_SMALL && count == 1);
        NexusGameStatus threaded = NEXUS_GAME_OK;
        std::thread worker([&] { threaded = api.isEntityAlive(expired); }); worker.join();
        assert(threaded == NEXUS_GAME_WRONG_THREAD);
        assert(api.destroyEntity(expired) == NEXUS_GAME_OK);
        NexusEntityHandle replacement{};
        assert(api.createEntity(bridge.World(), {}, &replacement) == NEXUS_GAME_OK);
        assert(replacement.index == expired.index && replacement.generation != expired.generation);
        assert(api.isEntityAlive(expired) == NEXUS_GAME_INVALID_HANDLE);
        expired = replacement;
        // ComponentのないEntityも列挙し、特定Componentによる暗黙の除外を防ぐ。
        auto replaced = level.GetScene()->GetEntity({replacement.index, replacement.generation});
        assert(replaced.RemoveComponent<NameComponent>());
        NexusEntityHandle listed{};
        assert(api.entities(bridge.World(), &listed, 1, &count) == NEXUS_GAME_OK && count == 1);
        assert(listed.index == replacement.index);
        const auto scripts = GameApiBridge::Scripts();
        assert(scripts.setScript(replacement, {"Fixture.Probe", 13}, 1) == NEXUS_GAME_OK);
        NexusScriptStateV1 state{};
        assert(scripts.getState(replacement, &state) == NEXUS_GAME_OK && state.attachment != 0 && state.enabled == 1);
        const auto oldAttachment = state.attachment;
        assert(scripts.setEnabled(replacement, 0) == NEXUS_GAME_OK);
        assert(scripts.getState(replacement, &state) == NEXUS_GAME_OK && state.enabled == 0 && state.attachment == oldAttachment);
        assert(scripts.removeScript(replacement) == NEXUS_GAME_OK);
        assert(scripts.getState(replacement, &state) == NEXUS_GAME_INVALID_HANDLE);
        assert(scripts.setScript(replacement, {"Fixture.Probe", 13}, 1) == NEXUS_GAME_OK);
        assert(scripts.getState(replacement, &state) == NEXUS_GAME_OK && state.attachment != oldAttachment);
        assert(level.GetScene()->GetEntityCount() == 1); // Script APIも同じSceneだけを操作している。
    }
    assert(api.isEntityAlive(expired) == NEXUS_GAME_INVALID_HANDLE);
    GameApiBridge next(level);
    assert(next.World() != expired.world && api.isEntityAlive(expired) == NEXUS_GAME_INVALID_HANDLE);
}

NativeGameModuleSettings Fixture() {
    return {std::filesystem::absolute("Generated/Outputs/EditorTests/NativeModuleFixture.dll"),
        "test-engine", "1.0.0", "11111111-1111-4111-8111-111111111111", "Fixture"};
}
Entity ModuleEntity(Level& level) {
    Entity result;
    level.GetScene()->ForEach<NameComponent>([&](Entity entity, const NameComponent& name) { if(name.name == "DllEntity") result = entity; });
    return result;
}
/*-----------------------------------------------------------------------------------------
 * LoaderStopSink
 * - DLL callbackのログから停止・再入Updateを要求し、Loaderの寿命境界を観測する
 * - Loader / Levelは借用だけで、callback内からそれらの所有者を破棄しない
 *---------------------------------------------------------------------------------------*/
class LoaderStopSink final : public ILogSink {
public:
    GameModuleLoader* loader = nullptr; //< テストscopeで所有し、Sink解除後に破棄する
    std::filesystem::path file; //< DLLのロード状態だけを観測する絶対path
    std::string_view marker; //< static文字列だけを借用する
    bool armed = true, returnedWhileLoaded = false, rejectedUpdate = false, callbackFailed = false; //< 停止再入後もコードが有効で、再帰Updateを拒否したか
    uint32_t requests = 0, createReturns = 0, updateReturns = 0, destroys = 0, destroyReturns = 0; //< callbackの完了と破棄回数だけを観測する
    void Write(const LogRecord& record) noexcept override {
        if(!loader) return;
        if(record.message == "NativeFixture.CreateReturned") ++createReturns;
        if(record.message == "NativeFixture.UpdateReturned") ++updateReturns;
        if(record.message == "NativeFixture.Destroy") ++destroys;
        if(record.message == "NativeFixture.DestroyReturned") ++destroyReturns;
        const bool selected = marker == "Loaded Native Game Module: " ? record.message.starts_with(marker) : record.message == marker;
        if(!armed || !selected) return;
        try {
            ++requests;
            loader->Stop(); loader->Stop(); // 同じcallbackからの重複要求も一度だけ破棄する。
            returnedWhileLoaded = GetModuleHandleW(file.c_str()) != nullptr;
            rejectedUpdate = !loader->Update(0);
        } catch(...) { callbackFailed = true; }
    }
};
/** \brief 直接Loaderを利用する場合もcreate / Update / destroyのstack上でUnloadしない検証ソース */
void LoaderCallbackBoundaries() {
    const auto settings = Fixture();
    for(const auto marker : {"NativeFixture.Create", "NativeFixture.Update", "NativeFixture.Destroy", "Loaded Native Game Module: "}) {
        Level level; assert(level.LoadEmpty()); GameApiBridge world(level); GameModuleLoader loader;
        const NexusGameModuleContextV1 context{NEXUS_GAME_ABI_VERSION, sizeof(NexusGameModuleContextV1), GameApiBridge::Api(), world.World()};
        auto sink = std::make_unique<LoaderStopSink>(); auto* observer = sink.get(); observer->loader = &loader; observer->file = settings.file; observer->marker = marker;
        assert(Logger::Get().AddSink(std::move(sink)));
        struct RemoveSink { const ILogSink* sink; ~RemoveSink() { static_cast<void>(Logger::Get().RemoveSink(sink)); } } remove{observer};
        const auto started = loader.Start(settings, context, &level);
        if(std::string_view(marker) == "NativeFixture.Create" || std::string_view(marker) == "Loaded Native Game Module: ") assert(!started);
        else {
            assert(started);
            if(std::string_view(marker) == "NativeFixture.Update") assert(!loader.Update(.25f));
            else loader.Stop();
        }
        assert(observer->requests == 1 && observer->returnedWhileLoaded && observer->rejectedUpdate && !observer->callbackFailed);
        assert(observer->createReturns == 1 && observer->destroys == 1 && observer->destroyReturns == 1);
        if(std::string_view(marker) == "NativeFixture.Update") assert(observer->updateReturns == 1);
        assert(!loader.IsQuarantined() && level.GetScene()->GetEntityCount() == 0 && GetModuleHandleW(settings.file.c_str()) == nullptr);
        observer->armed = false;
        assert(loader.Start(settings, context, &level)); loader.Stop();
        assert(observer->destroys == 2 && observer->destroyReturns == 2 && !loader.IsQuarantined());
    }
    Level first, second; assert(first.LoadEmpty() && second.LoadEmpty()); GameApiBridge world(first); GameModuleLoader loader;
    const NexusGameModuleContextV1 context{NEXUS_GAME_ABI_VERSION, sizeof(NexusGameModuleContextV1), GameApiBridge::Api(), world.World()};
    // DLLが生成処理へ進む前に誤World / 必須Level省略を拒否し、両WorldのEntity数を維持する。
    assert(!loader.Start(settings, context, &second) && first.GetScene()->GetEntityCount() == 0 && second.GetScene()->GetEntityCount() == 0);
    assert(!loader.Start(settings, context) && first.GetScene()->GetEntityCount() == 0 && GetModuleHandleW(settings.file.c_str()) == nullptr);
    assert(loader.Start(settings, context, &first));
    std::thread wrongThread([&] { loader.Stop(); }); wrongThread.join();
    assert(GetModuleHandleW(settings.file.c_str()) != nullptr && loader.Update(0));
    loader.Stop(); assert(!loader.IsQuarantined() && GetModuleHandleW(settings.file.c_str()) == nullptr);
}
/** \brief Editor Playと単体RuntimeのDLL更新・停止・開始失敗時の編集World保持を確認する */
void DllLifecycleAndRollback() {
    const auto settings = Fixture();
    assert(std::filesystem::is_regular_file(settings.file));
    EditorSession editor; assert(editor.Create());
    const auto editingCount = editor.GetLevel()->GetScene()->GetEntityCount();
    auto mismatch = settings; mismatch.engineVersion = "incompatible";
    assert(editor.SetNativeGameModule(mismatch));
    assert(!editor.StartPlay() && editor.GetPlayError());
    assert(editor.GetLevel()->GetScene()->GetEntityCount() == editingCount);
    assert(editor.SetNativeGameModule(settings) && editor.StartPlay());
    assert(editor.GetLevel()->GetScene()->GetEntityCount() == editingCount);
    auto entity = ModuleEntity(*editor.GetPlayLevel()); assert(entity.IsAlive());
    assert(editor.UpdatePreview(0.25f));
    assert(entity.GetComponent<TransformComponent>()->translation.x == .25f);
    assert(entity.GetComponent<TransformComponent>()->worldMatrix.At(3, 0) == .25f);
    editor.TogglePause(); assert(editor.UpdatePreview(1));
    assert(entity.GetComponent<TransformComponent>()->translation.x == .25f);
    editor.StopPlay();
    assert(GetModuleHandleW(settings.file.c_str()) == nullptr);
    assert(editor.StartPlay()); editor.StopPlay();

    GameRuntime runtime; assert(runtime.SetNativeGameModule(settings));
    auto level = std::make_unique<Level>(); assert(level->LoadEmpty());
    const auto systemCount = level->GetSystemScheduler().GetSystems().size();
    assert(runtime.Start(std::move(level)));
    assert(runtime.GetLevel()->GetSystemScheduler().GetSystems().size() == systemCount + 1);
    entity = ModuleEntity(*runtime.GetLevel()); assert(entity.IsAlive());
    assert(runtime.Update(.5f)); assert(entity.GetComponent<TransformComponent>()->translation.x == .5f);
    // Moduleの更新失敗時は実行Worldを停止し、半分動作したModuleだけを残さない。
    assert(runtime.GetLevel()->DestroyEntity(entity));
    assert(!runtime.Update(.5f) && !runtime.IsRunning());
    assert(GetModuleHandleW(settings.file.c_str()) == nullptr);
}
/** \brief SDKを呼ばず、Backendの適用結果通知とRuntime停止・再登録契約だけを検証するソース */
void NativePatchProtocol() {
    auto settings = Fixture();
    const auto source = std::filesystem::absolute(std::filesystem::path("Generated/NativePatchProtocol") / std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(source);
    const auto header = source / "FixedState.h";
    { std::ofstream file(header); file << "// fixed fixture contract\n"; }
    settings.patchSourceDirectory = source; // このfixtureは実パッチをせず、Source監視を模擬する。
    GameRuntime runtime; assert(runtime.SetNativeGameModule(settings));
    auto level = std::make_unique<Level>(); assert(level->LoadEmpty()); assert(runtime.Start(std::move(level)));
    auto entity = ModuleEntity(*runtime.GetLevel()); const auto persistent = entity.GetComponent<PersistentIdComponent>()->value;
    using TransferProbe=void(NEXUS_GAME_CALL*)(uint32_t,uint32_t*,uint32_t*);
    const auto transferExport=GetProcAddress(GetModuleHandleW(settings.file.c_str()),"NexusFixtureStateTransfer");
    TransferProbe transfer=nullptr; static_assert(sizeof(transfer)==sizeof(transferExport)); std::memcpy(&transfer,&transferExport,sizeof(transfer)); assert(transfer);
    uint32_t captured=0,restored=0; transfer(0,&captured,&restored); const auto initialCaptures=captured,initialRestores=restored;
    auto* scene = runtime.GetLevel()->GetScene(); assert(runtime.Update(.25f));
    assert(runtime.PrepareNativePatch() && runtime.IsPaused() && runtime.NativePatchStatus() == NativePatchState::Prepared);
    assert(!runtime.PrepareNativePatch() && runtime.Update(1));
    assert(entity.GetComponent<TransformComponent>()->translation.x == .25f);
    assert(runtime.FinishNativePatch(false) && runtime.NativePatchStatus() == NativePatchState::NotApplied && !runtime.IsPaused());
    transfer(0,&captured,&restored); assert(captured==initialCaptures+1 && restored==initialRestores);
    assert(runtime.Update(.25f) && entity.GetComponent<TransformComponent>()->translation.x == .5f);
    assert(runtime.PrepareNativePatch());
    assert(runtime.FinishNativePatch(true) && runtime.NativePatchStatus() == NativePatchState::Succeeded);
    transfer(0,&captured,&restored); assert(captured==initialCaptures+2 && restored==initialRestores+1);
    assert(runtime.GetLevel()->GetScene() == scene && runtime.GetLevel()->FindEntity(persistent).IsAlive() && runtime.GetLevel()->GetScene()->GetEntityCount() == 1);
    assert(runtime.Update(.25f) && entity.GetComponent<TransformComponent>()->translation.x == .75f);
    { std::ofstream file(header); file << "// incompatible fixture header\n"; }
    assert(!runtime.PrepareNativePatch() && runtime.NativePatchStatus() == NativePatchState::CompatibilityError && runtime.IsRunning() && !runtime.IsPaused());
    { std::ofstream file(header); file << "// fixed fixture contract\n"; }
    transfer(1,nullptr,nullptr);
    assert(!runtime.PrepareNativePatch() && runtime.IsRunning() && !runtime.IsPaused());
    transfer(0,nullptr,nullptr);
    assert(runtime.PrepareNativePatch()); runtime.Stop();
    assert(runtime.IsRunning() && GetModuleHandleW(settings.file.c_str()) != nullptr);
    assert(runtime.FinishNativePatch(false) && !runtime.IsRunning());
    assert(GetModuleHandleW(settings.file.c_str()) == nullptr);
}
/** \brief SDK非依存の境界検証ソース。同一DLLを利用しうる未調整Worldの同時パッチを拒否する */
void ModulePatchWorldLease() {
    Level level, other; assert(level.LoadEmpty() && other.LoadEmpty());
    GameApiBridge world(level);
    assert(!GameApiBridge::BeginGameModulePatch(other));
    {
        GameApiBridge second(other); assert(!GameApiBridge::BeginGameModulePatch(level));
    }
    bool wrongThread = true;
    std::thread worker([&] { wrongThread = GameApiBridge::BeginGameModulePatch(level); }); worker.join(); assert(!wrongThread);
    assert(GameApiBridge::BeginGameModulePatch(level) && !GameApiBridge::BeginLegacyPatch());
    bool rejected = false;
    try { GameApiBridge blocked(other); } catch(...) { rejected = true; }
    assert(rejected);
    std::thread wrongRelease([] { GameApiBridge::EndLegacyPatch(); }); wrongRelease.join();
    assert(!GameApiBridge::BeginGameModulePatch(level));
    GameApiBridge::EndLegacyPatch();
    { GameApiBridge second(other); }
    assert(GameApiBridge::BeginGameModulePatch(level)); GameApiBridge::EndLegacyPatch();
}
}

/** \brief 共通APIと独立DLLの回帰ケース。実行判断はユーザーへ委ねる */
/** \brief Level変更でGame stateは再生成し、コードimageは一時leaseで保持することを確認する */
void NativeLevelTransition() {
    const auto root=std::filesystem::absolute("Generated/NativeLevelTransition"); std::filesystem::create_directories(root);
    Level next; assert(next.LoadEmpty()); const auto target=next.CreateEntity("Next World");
    const auto id=target.GetComponent<PersistentIdComponent>()->value; assert(next.Save(root/"Next.level"));
    auto settings=Fixture(); GameRuntime runtime; assert(runtime.SetNativeGameModule(settings));
    auto level=std::make_unique<Level>(); assert(level->LoadEmpty() && level->SetResourceRoot(root));
    assert(runtime.Start(std::move(level)));
    const auto image=GetModuleHandleW(settings.file.c_str()); assert(image);
    assert(runtime.GetLevel()->RequestLevel("Next.level") && runtime.Update(0.1f));
    assert(runtime.IsRunning() && runtime.GetLevel()->FindEntity(id).IsAlive() && ModuleEntity(*runtime.GetLevel()).IsAlive());
    assert(GetModuleHandleW(settings.file.c_str())==image);
    runtime.Stop(); assert(GetModuleHandleW(settings.file.c_str())==nullptr);
}
void RunNativeGameModuleTests() { ApiHandleIsolation(); LoaderCallbackBoundaries(); DllLifecycleAndRollback(); NativePatchProtocol(); ModulePatchWorldLease(); NativeLevelTransition(); }
