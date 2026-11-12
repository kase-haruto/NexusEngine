#include "Runtime/Gameplay/NativeGameSystemScope.h"
#include "Runtime/Gameplay/GameApiBridge.h"
#include "Runtime/Level/Level.h"
#include <cassert>
#include <cstring>
#include <thread>
#include <limits>
#include <stdexcept>

using namespace NexusEngine;
namespace {
/* テスト専用の観測値。System所有権を別Registryへ移さず、callbackの順序だけ記録する。 */
struct Trace {
    NexusNativeSystemsRegistrationV1 context{};
    NexusNativeSystemDescriptorV1 attempt{};
    NexusEntityHandle entity{};
    std::vector<uint64_t> created, updated, destroyed;
    bool rejectedMutation = false, rejectedReentry = false;
    uint32_t nativeCalls = 0; //< 再送されてよい失効と実行後失敗の区別を観測する
};
Trace* creating = nullptr; //< createにuserdataがないABIをテストするための所有thread限定fixture
struct State { NexusGameApiV1 api; uint64_t self; Trace* trace; };
NexusGameStatus NEXUS_GAME_CALL VisitAlive(void* instance, void*) {
    auto& state = *static_cast<State*>(instance); return state.api.isEntityAlive(state.trace->entity);
}
NexusGameStatus NEXUS_GAME_CALL Create(const NexusNativeSystemContextV1* context, void** output) {
    *output = new State{context->game, context->system, creating}; creating->created.push_back(context->system); return NEXUS_GAME_OK;
}
NexusGameStatus NEXUS_GAME_CALL Update(void* instance, float deltaTime) {
    auto& state = *static_cast<State*>(instance); auto& trace = *state.trace; trace.updated.push_back(state.self);
    uint64_t rejected = 0;
    trace.rejectedMutation = trace.context.systems.add(trace.context.scope, &trace.attempt, &rejected) == NEXUS_GAME_FAILED;
    trace.rejectedReentry = trace.context.systems.access(trace.context.scope, state.self, VisitAlive, nullptr) == NEXUS_GAME_FAILED;
    NexusLocalTransform transform{}; auto status = state.api.getLocalTransform(trace.entity, &transform); if(status != NEXUS_GAME_OK) return status;
    transform.translation[0] += deltaTime; return state.api.setLocalTransform(trace.entity, &transform);
}
NexusGameStatus NEXUS_GAME_CALL Destroy(void* instance) {
    auto* state = static_cast<State*>(instance); state->trace->destroyed.push_back(state->self); delete state; return NEXUS_GAME_OK;
}
NexusGameStatus NEXUS_GAME_CALL PartialCreate(const NexusNativeSystemContextV1* context, void** output) { static_cast<void>(Create(context, output)); return NEXUS_GAME_FAILED; }
NexusGameStatus NEXUS_GAME_CALL FailingDestroy(void* instance) { static_cast<void>(Destroy(instance)); return NEXUS_GAME_FAILED; }
NexusNativeSystemDescriptorV1 Descriptor(const char* id) {
    static const NexusGameString transform[]{{"Transform", 9}};
    NexusNativeSystemDescriptorV1 result{}; result.structSize = sizeof(result); result.version = 1;
    result.id = {id, static_cast<uint32_t>(std::strlen(id))}; result.reads = transform; result.readCount = 1; result.writes = transform; result.writeCount = 1;
    result.create = Create; result.update = Update; result.destroy = Destroy; return result;
}
/** \brief テスト用Systemへ型付き引数を適用する。再入拒否は別の呼出経路で確認する */
NexusGameStatus NEXUS_GAME_CALL SetBoundTarget(void* instance, const NexusNativeValueV1* arguments, uint32_t, NexusNativeValueV1* result, uint8_t*, uint32_t) {
    auto& state = *static_cast<State*>(instance); ++state.trace->nativeCalls; NexusLocalTransform transform{}; *result = {};
    auto status = state.api.getLocalTransform(arguments[0].entity, &transform); if(status != NEXUS_GAME_OK) return status;
    std::memcpy(transform.translation, arguments[1].number, sizeof(transform.translation)); return state.api.setLocalTransform(arguments[0].entity, &transform);
}
NexusGameStatus NEXUS_GAME_CALL ReservedStatus(void* instance, const NexusNativeValueV1*, uint32_t, NexusNativeValueV1*, uint8_t*, uint32_t) {
    ++static_cast<State*>(instance)->trace->nativeCalls; return NEXUS_GAME_STALE_BINDING;
}
/** \brief SDKなしの契約検証ソース。非適用のHandle保持・一括公開・不完全再登録を区別する */
void PatchBindingContracts() {
    Level level; assert(level.LoadEmpty()); GameApiBridge world(level); Trace trace; creating = &trace;
    auto entity = level.CreateEntity("Patch target"); const auto id = entity.GetId(); trace.entity = {world.World(), id.index, id.generation};
    NativeGameSystemScope scope(level, world.World()); auto context = scope.BindingContext(); const auto& registration = context.registration;
    uint64_t system = 0; auto descriptor = Descriptor("Bound"); assert(registration.systems.add(registration.scope, &descriptor, &system) == NEXUS_GAME_OK);
    NexusGameApiParameterV1 parameters[]{ {{"entity", 6}, {NEXUS_VALUE_ENTITY, 0, {}}}, {{"target", 6}, {NEXUS_VALUE_VECTOR3, 0, {}}} };
    NexusNativeFunctionDescriptorV1 signature{sizeof(signature), 1, {"SetTarget", 9}, {NEXUS_VALUE_VOID, 0, {}}, parameters, 2, 1};
    auto reserved = signature; reserved.id = {"Reserved", 8}; reserved.parameters = nullptr; reserved.parameterCount = 0;
    assert(registration.publications.declareFunction(registration.scope, system, &signature) == NEXUS_GAME_OK);
    assert(registration.publications.declareFunction(registration.scope, system, &reserved) == NEXUS_GAME_OK);
    assert(context.bindings.bind(registration.scope, system, signature.id, 1, SetBoundTarget) == NEXUS_GAME_OK);
    assert(context.bindings.bind(registration.scope, system, reserved.id, 1, ReservedStatus) == NEXUS_GAME_OK);
    scope.Seal(); const auto api = NativeGameSystemScope::BindingApi(); NexusNativeBindingHandleV1 before{};
    assert(api.resolve(world.World(), {"Game", 4}, {"Bound", 5}, 1, &signature, &before) == NEXUS_GAME_OK);
    NexusNativeValueV1 arguments[2]{}; arguments[0].kind = NEXUS_VALUE_ENTITY; arguments[0].entity = trace.entity; arguments[1].kind = NEXUS_VALUE_VECTOR3;
    NexusNativeValueV1 output{};
    assert(scope.PreparePatch() && !scope.PreparePatch());
    assert(api.invoke(world.World(), before, arguments, 2, &output, nullptr, 0) == NEXUS_GAME_BINDING_DISABLED);
    assert(registration.systems.access(registration.scope, system, VisitAlive, nullptr) == NEXUS_GAME_BINDING_DISABLED);
    assert(!level.Update(.1f) && trace.updated.empty());
    assert(scope.FinishPatch(false));
    assert(api.invoke(world.World(), before, arguments, 2, &output, nullptr, 0) == NEXUS_GAME_OK && trace.nativeCalls == 1);
    assert(scope.PreparePatch());
    assert(context.bindings.bind(registration.scope, system, signature.id, 1, SetBoundTarget) == NEXUS_GAME_OK);
    assert(context.bindings.bind(registration.scope, system, reserved.id, 1, ReservedStatus) == NEXUS_GAME_OK);
    assert(scope.FinishPatch(true) && trace.created.size() == 1 && trace.destroyed.empty());
    assert(api.invoke(world.World(), before, arguments, 2, &output, nullptr, 0) == NEXUS_GAME_STALE_BINDING && trace.nativeCalls == 1);
    NexusNativeBindingHandleV1 after{};
    assert(api.resolve(world.World(), {"Game", 4}, {"Bound", 5}, 1, &signature, &after) == NEXUS_GAME_OK && after.function != before.function);
    assert(api.invoke(world.World(), after, arguments, 2, &output, nullptr, 0) == NEXUS_GAME_OK && trace.nativeCalls == 2);
    assert(api.resolve(world.World(), {"Game", 4}, {"Bound", 5}, 1, &reserved, &after) == NEXUS_GAME_OK);
    assert(api.invoke(world.World(), after, nullptr, 0, &output, nullptr, 0) == NEXUS_GAME_FAILED && trace.nativeCalls == 3);
    assert(scope.PreparePatch());
    assert(context.bindings.bind(registration.scope, system, signature.id, 1, SetBoundTarget) == NEXUS_GAME_OK);
    assert(!scope.FinishPatch(true) && !scope.FinishPatch(false)); // 適用後の未完了状態を未適用と偽って再開しない。
    assert(scope.Close() && trace.destroyed.size() == 1); creating = nullptr;
}
NexusGameStatus NEXUS_GAME_CALL EchoBoundValue(void*, const NexusNativeValueV1* arguments, uint32_t, NexusNativeValueV1* result, uint8_t* buffer, uint32_t capacity) {
    *result = arguments[0];
    if(result->kind == NEXUS_VALUE_UTF8 || result->kind == NEXUS_VALUE_FLOAT32_ARRAY) {
        const auto bytes = result->count * (result->kind == NEXUS_VALUE_UTF8 ? 1u : 4u);
        if(bytes > capacity) return NEXUS_GAME_BUFFER_TOO_SMALL;
        if(bytes) std::memcpy(buffer, result->data, bytes);
        result->data = bytes ? buffer : nullptr;
    }
    return NEXUS_GAME_OK;
}
NexusGameStatus NEXUS_GAME_CALL ThrowBoundValue(void*, const NexusNativeValueV1*, uint32_t, NexusNativeValueV1*, uint8_t*, uint32_t) { throw std::runtime_error("Expected Native failure"); }
NexusGameStatus NEXUS_GAME_CALL BadBoundResult(void* state, const NexusNativeValueV1*, uint32_t, NexusNativeValueV1* result, uint8_t*, uint32_t) {
    *result = {}; result->kind = NEXUS_VALUE_UTF8; result->count = 1; result->data = state; return NEXUS_GAME_OK;
}
/** \brief 同じSystemへの再入がcallbackを二重実行せず拒否されることを観測する */
NexusGameStatus NEXUS_GAME_CALL ReenterBoundSystem(void* state, const NexusNativeValueV1*, uint32_t, NexusNativeValueV1* result, uint8_t*, uint32_t) {
    *result = {}; const auto world = static_cast<State*>(state)->trace->entity.world;
    const auto api = NativeGameSystemScope::BindingApi(); NexusNativeBindingHandleV1 handle{};
    const NexusNativeFunctionDescriptorV1 signature{sizeof(signature), 1, {"Reenter", 7}, {NEXUS_VALUE_VOID, 0, {}}, nullptr, 0, 1};
    if(api.resolve(world, {"Game", 4}, {"Bound", 5}, 1, &signature, &handle) != NEXUS_GAME_OK) return NEXUS_GAME_FAILED;
    NexusNativeValueV1 output{}; return api.invoke(world, handle, nullptr, 0, &output, nullptr, 0) == NEXUS_GAME_FAILED ? NEXUS_GAME_OK : NEXUS_GAME_FAILED;
}
/** \brief 宣言・Binding・解決・値転送・停止・再登録をPublic ABI経由で確認する。Agentは実行しない */
void BindingContracts() {
    Level level; assert(level.LoadEmpty()); GameApiBridge world(level); Trace trace; creating = &trace;
    auto entity = level.CreateEntity("Bound target"); const auto id = entity.GetId(); trace.entity = {world.World(), id.index, id.generation};
    NativeGameSystemScope scope(level, world.World()); const auto registration = scope.BindingContext(); const auto& context = registration.registration;
    const auto api = NativeGameSystemScope::BindingApi(); uint64_t system = 0; auto descriptor = Descriptor("Bound");
    assert(context.systems.add(context.scope, &descriptor, &system) == NEXUS_GAME_OK);
    NexusGameApiParameterV1 parameters[]{ {{"entity", 6}, {NEXUS_VALUE_ENTITY, 0, {}}}, {{"target", 6}, {NEXUS_VALUE_VECTOR3, 0, {}}} };
    NexusNativeFunctionDescriptorV1 signature{sizeof(signature), 1, {"SetTarget", 9}, {NEXUS_VALUE_VOID, 0, {}}, parameters, 2, 1};
    NexusNativeBindingHandleV1 handle{};
    assert(api.resolve(world.World(), {"Game", 4}, {"Bound", 5}, 1, &signature, &handle) == NEXUS_GAME_FUNCTION_NOT_REGISTERED);
    assert(context.publications.declareFunction(context.scope, system, &signature) == NEXUS_GAME_OK);
    assert(api.resolve(world.World(), {"Game", 4}, {"Bound", 5}, 1, &signature, &handle) == NEXUS_GAME_FUNCTION_NOT_REGISTERED);
    assert(registration.bindings.bind(context.scope, system, signature.id, 1, SetBoundTarget) == NEXUS_GAME_OK);
    assert(registration.bindings.bind(context.scope, system, signature.id, 1, SetBoundTarget) == NEXUS_GAME_FAILED);
    // 同じFunction IDでも版ごとに署名が異なる契約を確認する。
    for(uint32_t kind = NEXUS_VALUE_BOOL; kind <= NEXUS_VALUE_FLOAT32_ARRAY; ++kind) {
        NexusGameApiParameterV1 parameter{{"value", 5}, {kind, 0, kind == NEXUS_VALUE_ENUM32 ? NexusGameString{"test.mode", 9} : NexusGameString{}}};
        NexusNativeFunctionDescriptorV1 echo{sizeof(echo), kind, {"Echo", 4}, parameter.type, &parameter, 1, 1};
        assert(context.publications.declareFunction(context.scope, system, &echo) == NEXUS_GAME_OK);
        assert(registration.bindings.bind(context.scope, system, echo.id, echo.version, EchoBoundValue) == NEXUS_GAME_OK);
    }
    auto throwing = signature; throwing.id = {"Throw", 5}; throwing.parameters = nullptr; throwing.parameterCount = 0;
    assert(context.publications.declareFunction(context.scope, system, &throwing) == NEXUS_GAME_OK);
    assert(registration.bindings.bind(context.scope, system, throwing.id, 1, ThrowBoundValue) == NEXUS_GAME_OK);
    auto reenter = throwing; reenter.id = {"Reenter", 7};
    assert(context.publications.declareFunction(context.scope, system, &reenter) == NEXUS_GAME_OK);
    assert(registration.bindings.bind(context.scope, system, reenter.id, 1, ReenterBoundSystem) == NEXUS_GAME_OK);
    auto bad = throwing; bad.id = {"BadResult", 9}; bad.result.kind = NEXUS_VALUE_UTF8;
    assert(context.publications.declareFunction(context.scope, system, &bad) == NEXUS_GAME_OK);
    assert(registration.bindings.bind(context.scope, system, bad.id, 1, BadBoundResult) == NEXUS_GAME_OK);
    scope.Seal();
    assert(api.resolve(world.World(), {"missing", 7}, {"Bound", 5}, 1, &signature, &handle) == NEXUS_GAME_SYSTEM_NOT_REGISTERED);
    assert(api.resolve(world.World(), {"Game", 4}, {"Bound", 5}, 2, &signature, &handle) == NEXUS_GAME_INCOMPATIBLE_ABI);
    auto wrong = signature; wrong.result.kind = NEXUS_VALUE_BOOL;
    assert(api.resolve(world.World(), {"Game", 4}, {"Bound", 5}, 1, &wrong, &handle) == NEXUS_GAME_INCOMPATIBLE_ABI);
    assert(api.resolve(world.World(), {"Game", 4}, {"Bound", 5}, 1, &signature, &handle) == NEXUS_GAME_OK);
    NexusNativeValueV1 arguments[2]{}; arguments[0].kind = NEXUS_VALUE_ENTITY; arguments[0].entity = trace.entity;
    arguments[1].kind = NEXUS_VALUE_VECTOR3; arguments[1].number[0] = 3; NexusNativeValueV1 result{};
    assert(api.invoke(world.World(), handle, arguments, 2, &result, nullptr, 0) == NEXUS_GAME_OK && entity.GetComponent<TransformComponent>()->translation.x == 3);
    arguments[1].number[1] = std::numeric_limits<float>::quiet_NaN();
    assert(api.invoke(world.World(), handle, arguments, 2, &result, nullptr, 0) == NEXUS_GAME_INVALID_ARGUMENT); arguments[1].number[1] = 0;
    ++arguments[0].entity.generation;
    assert(api.invoke(world.World(), handle, arguments, 2, &result, nullptr, 0) == NEXUS_GAME_INVALID_HANDLE); arguments[0].entity = trace.entity;
    NexusGameStatus threaded = NEXUS_GAME_OK; std::thread worker([&] { NexusNativeValueV1 output{}; threaded = api.invoke(world.World(), handle, arguments, 2, &output, nullptr, 0); }); worker.join(); assert(threaded == NEXUS_GAME_WRONG_THREAD);
    assert(world.FreezeManagedServices(true)); assert(api.invoke(world.World(), handle, arguments, 2, &result, nullptr, 0) == NEXUS_GAME_FAILED); assert(world.FreezeManagedServices(false));
    uint8_t buffer[4096]{}; const float samples[]{1, 2}; const char text[] = "hello";
    for(uint32_t kind = NEXUS_VALUE_BOOL; kind <= NEXUS_VALUE_FLOAT32_ARRAY; ++kind) {
        NexusGameApiParameterV1 parameter{{"value", 5}, {kind, 0, kind == NEXUS_VALUE_ENUM32 ? NexusGameString{"test.mode", 9} : NexusGameString{}}};
        NexusNativeFunctionDescriptorV1 echo{sizeof(echo), kind, {"Echo", 4}, parameter.type, &parameter, 1, 1}; NexusNativeBindingHandleV1 echoHandle{};
        assert(api.resolve(world.World(), {"Game", 4}, {"Bound", 5}, 1, &echo, &echoHandle) == NEXUS_GAME_OK);
        NexusNativeValueV1 value{}; value.kind = kind;
        if(kind == NEXUS_VALUE_BOOL || kind == NEXUS_VALUE_INT32 || kind == NEXUS_VALUE_UINT32 || kind == NEXUS_VALUE_ENUM32) value.integer = 1;
        if(kind >= NEXUS_VALUE_FLOAT32 && kind <= NEXUS_VALUE_VECTOR4) value.number[0] = 2;
        if(kind == NEXUS_VALUE_ENTITY) value.entity = trace.entity;
        if(kind == NEXUS_VALUE_UTF8) { value.data = text; value.count = 5; }
        if(kind == NEXUS_VALUE_FLOAT32_ARRAY) { value.data = samples; value.count = 2; }
        assert(api.invoke(world.World(), echoHandle, &value, 1, &result, buffer, sizeof(buffer)) == NEXUS_GAME_OK && result.kind == kind);
        if(value.count) assert(result.data == buffer && std::memcmp(buffer, value.data, value.count * (kind == NEXUS_VALUE_UTF8 ? 1 : 4)) == 0);
    }
    NexusNativeBindingHandleV1 failure{};
    assert(api.resolve(world.World(), {"Game", 4}, {"Bound", 5}, 1, &throwing, &failure) == NEXUS_GAME_OK);
    assert(api.invoke(world.World(), failure, nullptr, 0, &result, buffer, sizeof(buffer)) == NEXUS_GAME_FAILED);
    assert(api.resolve(world.World(), {"Game", 4}, {"Bound", 5}, 1, &reenter, &failure) == NEXUS_GAME_OK);
    assert(api.invoke(world.World(), failure, nullptr, 0, &result, buffer, sizeof(buffer)) == NEXUS_GAME_OK);
    assert(api.resolve(world.World(), {"Game", 4}, {"Bound", 5}, 1, &bad, &failure) == NEXUS_GAME_OK);
    assert(api.invoke(world.World(), failure, nullptr, 0, &result, buffer, sizeof(buffer)) == NEXUS_GAME_INVALID_ARGUMENT);
    assert(registration.bindings.setEnabled(context.scope, 0) == NEXUS_GAME_OK);
    assert(api.invoke(world.World(), handle, arguments, 2, &result, nullptr, 0) == NEXUS_GAME_BINDING_DISABLED);
    assert(registration.bindings.bind(context.scope, system, signature.id, 1, SetBoundTarget) == NEXUS_GAME_OK);
    assert(registration.bindings.setEnabled(context.scope, 1) == NEXUS_GAME_OK);
    assert(api.invoke(world.World(), handle, arguments, 2, &result, nullptr, 0) == NEXUS_GAME_STALE_BINDING);
    assert(api.resolve(world.World(), {"Game", 4}, {"Bound", 5}, 1, &signature, &handle) == NEXUS_GAME_OK);
    assert(scope.Close()); assert(api.invoke(world.World(), handle, arguments, 2, &result, nullptr, 0) == NEXUS_GAME_INVALID_HANDLE);
    creating = nullptr;
}
}
/** \brief Scheduler統合・数値取得・契約分離・Module寿命・エラーを検証するSource。Agentは実行しない */
void RunNativeGameSystemTests() {
    Level level, another; assert(level.LoadEmpty() && another.LoadEmpty());
    GameApiBridge world(level), otherWorld(another); Trace trace; creating = &trace;
    auto entity = level.CreateEntity("Native System target"); const auto id = entity.GetId(); trace.entity = {world.World(), id.index, id.generation};
    const auto original = level.GetSystemScheduler().GetSystems().size();
    NativeGameSystemScope scope(level, world.World()), foreign(another, otherWorld.World()); trace.context = scope.Context();
    auto a = Descriptor("A"), b = Descriptor("B"); const NexusGameString dependency[]{ {"A", 1} }; b.after = dependency; b.afterCount = 1;
    uint64_t first = 0, second = 0, found = 0;
    assert(trace.context.systems.add(trace.context.scope, &a, &first) == NEXUS_GAME_OK);
    assert(trace.context.systems.add(trace.context.scope, &a, &found) == NEXUS_GAME_FAILED);
    assert(trace.context.systems.add(trace.context.scope, &b, &second) == NEXUS_GAME_OK);
    assert(level.GetSystemScheduler().GetSystems().size() == original + 2);
    assert(scope.Publications().Functions().empty()); // Systemを登録しただけではAPIは公開されない。
    assert(trace.context.systems.find(trace.context.scope, {"A", 1}, 1, &found) == NEXUS_GAME_OK && found == first);
    assert(trace.context.systems.find(trace.context.scope, {"A", 1}, 2, &found) == NEXUS_GAME_INCOMPATIBLE_ABI);
    assert(trace.context.systems.find(trace.context.scope, {"missing", 7}, 1, &found) == NEXUS_GAME_INVALID_HANDLE);
    auto invalid = Descriptor("invalid"); const NexusGameString missing[]{ {"missing", 7} }; invalid.after = missing; invalid.afterCount = 1;
    assert(trace.context.systems.add(trace.context.scope, &invalid, &found) == NEXUS_GAME_INVALID_ARGUMENT);
    invalid.after = nullptr; invalid.afterCount = 0; invalid.reads = missing;
    assert(trace.context.systems.add(trace.context.scope, &invalid, &found) == NEXUS_GAME_INVALID_ARGUMENT && trace.created.size() == 2);
    auto other = foreign.Context();
    assert(other.systems.access(other.scope, first, VisitAlive, nullptr) == NEXUS_GAME_INVALID_HANDLE);
    NexusGameStatus threaded = NEXUS_GAME_OK;
    std::thread worker([&] { uint64_t handle = 0; threaded = trace.context.systems.find(trace.context.scope, {"A", 1}, 1, &handle); }); worker.join();
    assert(threaded == NEXUS_GAME_WRONG_THREAD);
    char parameterName[] = "target";
    NexusGameApiParameterV1 parameters[]{ {{"entity", 6}, {NEXUS_VALUE_ENTITY, 0, {}}}, {{parameterName, 6}, {NEXUS_VALUE_VECTOR3, 0, {}}} };
    NexusNativeFunctionDescriptorV1 function{sizeof(function), 1, {"SetTarget", 9}, {NEXUS_VALUE_VOID, 0, {}}, parameters, 2, 1};
    assert(trace.context.publications.declareFunction(trace.context.scope, first, &function) == NEXUS_GAME_OK);
    parameterName[0] = 'x'; assert(scope.Publications().Functions()[0].parameters[1].id == "target");
    assert(trace.context.publications.declareFunction(trace.context.scope, first, &function) == NEXUS_GAME_FAILED);
    NexusNativeFunctionInfoV1 info{};
    assert(trace.context.publications.findFunction(trace.context.scope, first, {"SetTarget", 9}, 1, &info) == NEXUS_GAME_OK && info.parameterCount == 2 && info.system == first && info.function);
    assert(trace.context.publications.findFunction(trace.context.scope, first, {"SetTarget", 9}, 2, &info) == NEXUS_GAME_INCOMPATIBLE_ABI);
    trace.attempt = Descriptor("during-update");
    assert(level.Update(.25f)); // 未sealedでもScheduler実行中の登録を拒否する。
    assert(trace.rejectedMutation && trace.rejectedReentry && trace.updated == (std::vector<uint64_t>{first, second}));
    assert(entity.GetComponent<TransformComponent>()->worldMatrix.At(3, 0) == .5f);
    scope.Seal(); assert(trace.context.systems.add(trace.context.scope, &trace.attempt, &found) == NEXUS_GAME_FAILED);
    assert(trace.context.publications.declareFunction(trace.context.scope, second, &function) == NEXUS_GAME_FAILED);
    assert(trace.context.systems.access(trace.context.scope, first, VisitAlive, nullptr) == NEXUS_GAME_OK);
    // 同じLevelのScene交換でも、adapterはScene pointerを保持せず現在Worldを参照する。
    auto clone = level.GetScene()->Clone(); assert(clone); level.ExchangeScene(std::move(clone)); assert(world.RebindScene());
    assert(level.Update(.25f));
    auto current = level.GetScene()->GetEntity(id); assert(level.DestroyEntity(current));
    assert(trace.context.systems.access(trace.context.scope, first, VisitAlive, nullptr) == NEXUS_GAME_INVALID_HANDLE);
    assert(scope.Close() && scope.Close());
    assert(trace.destroyed == (std::vector<uint64_t>{second, first}) && scope.Publications().Functions().empty());
    assert(level.GetSystemScheduler().GetSystems().size() == original);
    assert(trace.context.systems.find(trace.context.scope, {"A", 1}, 1, &found) == NEXUS_GAME_INVALID_HANDLE);
    assert(trace.context.publications.findFunction(trace.context.scope, first, {"SetTarget", 9}, 1, &info) == NEXUS_GAME_INVALID_HANDLE);
    {
        Trace partial; creating = &partial; NativeGameSystemScope failed(level, world.World()); partial.context = failed.Context();
        auto descriptor = Descriptor("partial"); descriptor.create = PartialCreate;
        assert(partial.context.systems.add(partial.context.scope, &descriptor, &found) == NEXUS_GAME_FAILED);
        assert(partial.created.size() == 1 && partial.destroyed == partial.created && level.GetSystemScheduler().GetSystems().size() == original);
        assert(failed.Close());
    }
    {
        Trace destruction; creating = &destruction; NativeGameSystemScope failed(level, world.World()); destruction.context = failed.Context();
        auto descriptor = Descriptor("destroy-failure"); descriptor.destroy = FailingDestroy;
        assert(destruction.context.systems.add(destruction.context.scope, &descriptor, &found) == NEXUS_GAME_OK);
        assert(!failed.Close() && level.GetSystemScheduler().GetSystems().size() == original);
    }
    creating = nullptr;
    BindingContracts();
    PatchBindingContracts();
}
