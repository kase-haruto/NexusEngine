#include <Nexus/GameModule.h>
#include <Nexus/GameServices.h>
#include <Nexus/NativeGameSystemApi.h>
#include <Nexus/NativeGameBindings.h>
#include <Nexus/NativeGamePatch.h>
#include <Nexus/NativeGameState.h>
#include <cstring>
#include <memory>

namespace {
uint32_t stateCaptured=0,stateRestored=0,stateTransferFailure=0; //< 値移行の呼出しと失敗経路だけを観測する
/*-----------------------------------------------------------------------------------------
 * FixtureState
 * - テストDLLのheapとWorld Handleを保持し、Engine内部へのリンクなしで境界を検証する
 *---------------------------------------------------------------------------------------*/
struct FixtureState {
    NexusGameApiV1 api{}; //< create時に値コピーするEngine API
    NexusEntityHandle entity{}; //< このDLLが生成した既存ECSのEntity
    NexusGameServicesContextV1 services{}; //< 明示API登録とEventを同じWorldへ接続する
    NexusNativeSystemsRegistrationV1 systems{}; //< 保存/復元の間もpointerではなく既存の同期借用APIを使う
    uint32_t calls = 0; //< C# Reloadで再生成されていないことを検証する内部状態
    bool systemRegistered = false, systemDestroyed = false; //< Moduleより先にSystemを破棄する契約の観測値
};
/*-----------------------------------------------------------------------------------------
 * FixtureSystem
 * - 既存DLL fixture内でScheduler登録とModule終了前の破棄だけを観測する
 * - ECSを複製せず、Module stateの借用は同じDLLの寿命内に限定する
 *---------------------------------------------------------------------------------------*/
struct FixtureSystem { FixtureState* owner = nullptr; };
/** \brief Systemの確保をDLLのallocator内で完結する */
NexusGameStatus NEXUS_GAME_CALL CreateSystem(const NexusNativeSystemContextV1*, void** output) noexcept {
    try { *output = new FixtureSystem; return NEXUS_GAME_OK; } catch(...) { return NEXUS_GAME_FAILED; }
}
/** \brief Module内部でだけ借用して所有者との関連を設定する */
NexusGameStatus NEXUS_GAME_CALL AttachSystem(void* system, void* owner) noexcept {
    static_cast<FixtureSystem*>(system)->owner = static_cast<FixtureState*>(owner); return NEXUS_GAME_OK;
}
/** \brief 既存fixtureのTransform結果を変更せずScheduler呼出しを受ける */
NexusGameStatus NEXUS_GAME_CALL UpdateSystem(void*, float) noexcept { return NEXUS_GAME_OK; }
/** \brief Module stateがまだ有効な順序で破棄されたことを記録する */
NexusGameStatus NEXUS_GAME_CALL DestroySystem(void* system) noexcept {
    auto* value = static_cast<FixtureSystem*>(system);
    if(value->owner) value->owner->systemDestroyed = true;
    delete value; return NEXUS_GAME_OK;
}
/** \brief fixtureでもSystem実体を通り、共通Entityへ値を適用する。クラスの自動公開はしない */
NexusGameStatus NEXUS_GAME_CALL SetSystemTarget(void* system, const NexusNativeValueV1* arguments, uint32_t count, NexusNativeValueV1* result, uint8_t*, uint32_t) noexcept {
    if(!system || !arguments || count != 2 || !result) return NEXUS_GAME_INVALID_ARGUMENT;
    *result = {}; auto* owner = static_cast<FixtureSystem*>(system)->owner;
    if(!owner) return NEXUS_GAME_FAILED;
    NexusLocalTransform transform{}; auto status = owner->api.getLocalTransform(arguments[0].entity, &transform); if(status != NEXUS_GAME_OK) return status;
    std::memcpy(transform.translation, arguments[1].number, sizeof(transform.translation)); return owner->api.setLocalTransform(arguments[0].entity, &transform);
}
/** \brief DLL側だけでstateを生成する */
NexusGameStatus NEXUS_GAME_CALL Create(const NexusGameModuleContextV1* context, void** output) noexcept {
    if(!context || !output) return NEXUS_GAME_INVALID_ARGUMENT;
    *output = nullptr;
    try {
        auto state = std::make_unique<FixtureState>(); state->api = context->api;
        const auto result = state->api.createEntity(context->world, {"DllEntity", 9}, &state->entity);
        if(result != NEXUS_GAME_OK) return result;
        *output = state.release();
        // ログ経由の停止要求後もDLL callbackが戻るまでstateを保持することを観測する。
        auto& created = *static_cast<FixtureState*>(*output);
        static constexpr char entered[] = "NativeFixture.Create", returned[] = "NativeFixture.CreateReturned";
        auto logged = created.api.log(created.entity.world, {entered, sizeof(entered) - 1}); if(logged != NEXUS_GAME_OK) return logged;
        if(created.api.isEntityAlive(created.entity) != NEXUS_GAME_OK) return NEXUS_GAME_FAILED;
        return created.api.log(created.entity.world, {returned, sizeof(returned) - 1});
    } catch(...) { return NEXUS_GAME_FAILED; }
}
/** \brief 既存ECSのTransform変更と、削除済みEntityによる更新失敗を検証する */
NexusGameStatus NEXUS_GAME_CALL Update(void* instance, float deltaTime) noexcept {
    auto& state = *static_cast<FixtureState*>(instance);
    if(state.services.client) {
        NexusGameEventV1 header{}; uint8_t bytes[4224]{}; uint32_t size = 0;
        for(uint32_t index = 0; index < 256; ++index) {
            auto result = state.services.api.poll(state.services.client, &header, bytes, 4224, &size);
            if(result != NEXUS_GAME_OK) return result; if(!size) break;
        }
    }
    NexusLocalTransform transform{};
    const auto read = state.api.getLocalTransform(state.entity, &transform);
    if(read != NEXUS_GAME_OK) return read;
    static constexpr char entered[] = "NativeFixture.Update", returned[] = "NativeFixture.UpdateReturned";
    auto logged = state.api.log(state.entity.world, {entered, sizeof(entered) - 1}); if(logged != NEXUS_GAME_OK) return logged;
    transform.translation[0] += deltaTime;
    const auto written = state.api.setLocalTransform(state.entity, &transform); if(written != NEXUS_GAME_OK) return written;
    return state.api.log(state.entity.world, {returned, sizeof(returned) - 1});
}
/** \brief Entityを両言語で共有し、Native内部counterを値応答でだけ公開する */
NexusGameStatus NEXUS_GAME_CALL Bump(void* instance, NexusEntityHandle target, NexusGameBytes request, uint8_t* response, uint32_t capacity, uint32_t* length) noexcept {
    if(!instance || !length || request.length || capacity < 4 || !response) return NEXUS_GAME_INVALID_ARGUMENT;
    auto& state = *static_cast<FixtureState*>(instance); *length = 0;
    NexusLocalTransform transform{}; auto result = state.api.getLocalTransform(target, &transform); if(result != NEXUS_GAME_OK) return result;
    transform.translation[1] += 1; result = state.api.setLocalTransform(target, &transform); if(result != NEXUS_GAME_OK) return result;
    ++state.calls; std::memcpy(response, &state.calls, 4); *length = 4;
    return state.services.api.publish(state.services.client, {"fixture.bumped", 14}, 1, target, {response, 4});
}
/** \brief stateを生成元DLLで破棄する。Worldを使える破棄順であることも検証する */
NexusGameStatus NEXUS_GAME_CALL Destroy(void* instance) noexcept {
    auto* state = static_cast<FixtureState*>(instance);
    if(state->systemRegistered && !state->systemDestroyed) return NEXUS_GAME_FAILED;
    static constexpr char entered[] = "NativeFixture.Destroy", returned[] = "NativeFixture.DestroyReturned";
    auto logged = state->api.log(state->entity.world, {entered, sizeof(entered) - 1}); if(logged != NEXUS_GAME_OK) return logged;
    const auto status = state->api.destroyEntity(state->entity);
    if(status != NEXUS_GAME_OK && status != NEXUS_GAME_INVALID_HANDLE) return status;
    logged = state->api.log(state->entity.world, {returned, sizeof(returned) - 1});
    delete state; return logged;
}
}
/** \brief 任意exportから既存Schedulerへ登録し、Systemの所有とModule破棄順序を検証する */
extern "C" __declspec(dllexport) NexusGameStatus NEXUS_GAME_CALL NexusRegisterGameSystems(void* instance, const NexusNativeSystemsRegistrationV1* context) noexcept {
    if(!instance || !context || context->abiVersion != 1 || context->structSize != sizeof(*context)) return NEXUS_GAME_INCOMPATIBLE_ABI;
    NexusNativeSystemDescriptorV1 descriptor{}; descriptor.structSize = sizeof(descriptor); descriptor.version = 1;
    descriptor.id = {"FixtureSystem", 13}; descriptor.create = CreateSystem; descriptor.update = UpdateSystem; descriptor.destroy = DestroySystem;
    uint64_t handle = 0; const auto status = context->systems.add(context->scope, &descriptor, &handle);
    if(status != NEXUS_GAME_OK) return status;
    auto& state = *static_cast<FixtureState*>(instance); state.systemRegistered = true;
    state.systems=*context;
    return context->systems.access(context->scope, handle, AttachSystem, instance);
}
/** \brief V1のfixtureを維持し、V2だけでMetadataと呼出先を結び付ける */
extern "C" __declspec(dllexport) NexusGameStatus NEXUS_GAME_CALL NexusRegisterGameSystemsV2(void* instance, const NexusNativeSystemsRegistrationV2* context) noexcept {
    if(!context || context->registration.abiVersion != 2 || context->registration.structSize != sizeof(*context) || context->bindings.abiVersion != 1 || context->bindings.structSize != sizeof(context->bindings)) return NEXUS_GAME_INCOMPATIBLE_ABI;
    auto registration = context->registration; registration.abiVersion = 1; registration.structSize = sizeof(registration);
    auto status = NexusRegisterGameSystems(instance, &registration); if(status != NEXUS_GAME_OK) return status;
    uint64_t system = 0; status = registration.systems.find(registration.scope, {"FixtureSystem", 13}, 1, &system); if(status != NEXUS_GAME_OK) return status;
    const NexusGameApiParameterV1 parameters[]{ {{"entity", 6}, {NEXUS_VALUE_ENTITY, 0, {}}}, {{"target", 6}, {NEXUS_VALUE_VECTOR3, 0, {}}} };
    const NexusNativeFunctionDescriptorV1 function{sizeof(NexusNativeFunctionDescriptorV1), 1, {"SetTarget", 9}, {NEXUS_VALUE_VOID, 0, {}}, parameters, 2, 1};
    status = registration.publications.declareFunction(registration.scope, system, &function); if(status != NEXUS_GAME_OK) return status;
    return context->bindings.bind(registration.scope, system, function.id, function.version, SetSystemTarget);
}
/** \brief fixtureはSDKパッチをせず、この固定契約の受渡しだけをユーザーテストで確認する */
extern "C" __declspec(dllexport) NexusGameStatus NEXUS_GAME_CALL NexusGetNativePatchContract(uint32_t abi, NexusNativePatchContractV1* output) noexcept {
    if(!output) return NEXUS_GAME_INVALID_ARGUMENT; *output = {};
    if(abi != 1) return NEXUS_GAME_INCOMPATIBLE_ABI;
    *output = {1, sizeof(*output), 1, sizeof(FixtureState), alignof(FixtureState), (static_cast<uint64_t>(sizeof(FixtureSystem)) << 32) | alignof(FixtureSystem)};
    return NEXUS_GAME_OK;
}
/** \brief Systemは再生成せず既存のSetTargetだけを再登録する */
extern "C" __declspec(dllexport) NexusGameStatus NEXUS_GAME_CALL NexusRebindGameSystems(void* instance, const NexusNativeSystemsRegistrationV2* context) noexcept {
    if(!instance || !context || context->registration.abiVersion != 2 || context->registration.structSize != sizeof(*context)) return NEXUS_GAME_INVALID_ARGUMENT;
    const auto& registration = context->registration; uint64_t system = 0;
    const auto found = registration.systems.find(registration.scope, {"FixtureSystem", 13}, 1, &system); if(found != NEXUS_GAME_OK) return found;
    return context->bindings.bind(registration.scope, system, {"SetTarget", 9}, 1, SetSystemTarget);
}
/** \brief 独立DLLの固定metadataを返す。Worldの生成はcreateまで行わない */
extern "C" __declspec(dllexport) NexusGameStatus NEXUS_GAME_CALL NexusGetGameModule(uint32_t requestedAbi, NexusGameModuleV1* output) noexcept {
    if(!output) return NEXUS_GAME_INVALID_ARGUMENT;
    *output = {};
    if(requestedAbi != NEXUS_GAME_ABI_VERSION) return NEXUS_GAME_INCOMPATIBLE_ABI;
    *output = {NEXUS_GAME_ABI_VERSION, sizeof(NexusGameModuleV1), {"test-engine", 11}, {"1.0.0", 5},
        {"11111111-1111-4111-8111-111111111111", 36}, {"Fixture", 7}, Create, Update, Destroy};
    return NEXUS_GAME_OK;
}
/** \brief 任意exportの登録寿命も既存V1 DLLと同じloaderで検証する */
extern "C" __declspec(dllexport) NexusGameStatus NEXUS_GAME_CALL NexusConnectGameServices(void* instance, const NexusGameServicesContextV1* context) noexcept {
    if(!instance || !context || context->abiVersion != 1 || context->structSize != sizeof(*context)) return NEXUS_GAME_INCOMPATIBLE_ABI;
    auto& state = *static_cast<FixtureState*>(instance); state.services = *context;
    return context->api.registerCommand(context->client, {"fixture.bump", 12}, 1, Bump, instance);
}
/** \brief テストで選択したゲーム値だけを保存し、EngineにFixtureStateを解釈させない */
extern "C" __declspec(dllexport) NexusGameStatus NEXUS_GAME_CALL NexusSaveNativeGameState(void* state,uint32_t abi,
    uint64_t* schema,uint8_t* data,uint32_t capacity,uint32_t* length) noexcept {
    if(!state || abi!=1 || !schema || !length || stateTransferFailure==1) return NEXUS_GAME_FAILED;
    *schema=1; *length=4;
    if(capacity<4) return NEXUS_GAME_BUFFER_TOO_SMALL;
    if(!data) return NEXUS_GAME_INVALID_ARGUMENT;
    std::memcpy(data,&static_cast<FixtureState*>(state)->calls,4); ++stateCaptured; return NEXUS_GAME_OK;
}
/** \brief 復元失敗時も元stateを破壊せず、Runtimeが停止可能な状態を保つ */
extern "C" __declspec(dllexport) NexusGameStatus NEXUS_GAME_CALL NexusRestoreNativeGameState(void* state,uint32_t abi,
    uint64_t schema,const uint8_t* data,uint32_t length) noexcept {
    if(!state || abi!=1 || schema!=1 || !data || length!=4 || stateTransferFailure==2) return NEXUS_GAME_FAILED;
    uint32_t value=0; std::memcpy(&value,data,4);
    auto& owner=*static_cast<FixtureState*>(state); uint64_t system=0;
    const auto found=owner.systems.systems.find(owner.systems.scope,{"FixtureSystem",13},1,&system);
    if(found!=NEXUS_GAME_OK) return found;
    const auto restored=owner.systems.systems.access(owner.systems.scope,system,
        [](void* instance,void* input) noexcept -> NexusGameStatus {
            auto* component=static_cast<FixtureSystem*>(instance);
            if(!component || !component->owner || !input) return NEXUS_GAME_FAILED;
            component->owner->calls=*static_cast<const uint32_t*>(input); return NEXUS_GAME_OK;
        },&value);
    if(restored!=NEXUS_GAME_OK) return restored;
    ++stateRestored; return NEXUS_GAME_OK;
}
/** \brief ABI検証用の観測値。ゲームAPIへの登録は行わない */
extern "C" __declspec(dllexport) void NEXUS_GAME_CALL NexusFixtureStateTransfer(uint32_t failure,uint32_t* captured,uint32_t* restored) noexcept {
    stateTransferFailure=failure;
    if(captured) *captured=stateCaptured;
    if(restored) *restored=stateRestored;
}
