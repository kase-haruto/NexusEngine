#include "Runtime/Gameplay/GameApiBridge.h"
#include "Public/Nexus/GameComponents.hpp"
#include "Public/Nexus/GameTransforms.hpp"
#include "Public/Nexus/GamePhysics.hpp"
#include "Public/Nexus/GameInput.hpp"
#include "Platform/Input/XInputGamepads.h"
#include "Public/Nexus/GameAudio.hpp"
#include "Tests/Fixtures/SilentWave.h"
#include "Graphics/Model/PrimitiveMeshBuilder.h"
#include "Public/Nexus/ScriptAttachments.h"
#include "Runtime/Scene/Components/ScriptComponent.h"
#include "Runtime/Level/Level.h"
#include "Runtime/Scene/Components/FogComponent.h"
#include "Runtime/Scene/Components/RigidBodyComponent.h"
#include "Runtime/Scene/Components/ColliderComponent.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include "Runtime/Scene/Components/PersistentIdComponent.h"
#include "Platform/Development/LiveCodingAgent.h"
#include <cassert>
#include <cstring>
#include <thread>
#include <stdexcept>
#include <limits>
#include <array>
#include <filesystem>
#include <cmath>
#include <fstream>
#include <iostream>
using namespace NexusEngine;
namespace {
/* Native stateを値応答でだけ公開するテスト。Managed objectや別ECSを持たない。 */
struct CommandState { uint32_t calls = 0; NexusGameApiV1 game; uint64_t client; };
NexusGameStatus NEXUS_GAME_CALL Count(void* instance, NexusEntityHandle entity, NexusGameBytes, uint8_t* output, uint32_t capacity, uint32_t* length) {
    auto& state = *static_cast<CommandState*>(instance);
    if(capacity < 4 || !output || !length) return NEXUS_GAME_INVALID_ARGUMENT;
    // callbackをWorld lock外で実行し、同じWorld APIへ再入できることを確認する。
    if(entity.world && state.game.isEntityAlive(entity) != NEXUS_GAME_OK) return NEXUS_GAME_INVALID_HANDLE;
    assert(GameApiBridge::Services().close(state.client) == NEXUS_GAME_FAILED);
    ++state.calls; std::memcpy(output, &state.calls, 4); *length = 4; return NEXUS_GAME_OK;
}
/** \brief 共通Reflectionへの値操作とバッチの失敗時原子性をCLRなしで検証する */
void ComponentOperations() {
    Level level; assert(level.LoadEmpty()); GameApiBridge bridge(level);
    const auto services = GameApiBridge::Services(); const auto game = GameApiBridge::Api();
    uint64_t client = 0; assert(GameApiBridge::OpenNativeServices(bridge.World(), &client) == NEXUS_GAME_OK);
    const NexusGameServicesContextV1 context{1, sizeof(NexusGameServicesContextV1), bridge.World(), client, services};
    NexusGame::Components components(context); NexusGame::Transforms transforms(context);
    NexusEntityHandle entity{}; assert(game.createEntity(bridge.World(), {"Component", 9}, &entity) == NEXUS_GAME_OK);
    const auto persistent = level.GetScene()->GetEntity({entity.index, entity.generation}).GetComponent<PersistentIdComponent>()->value;
    bool present = true;
    assert(components.Has(entity, {"Fog", 3}, present) == NEXUS_GAME_OK && !present);
    assert(components.Add(entity, {"Fog", 3}) == NEXUS_GAME_OK);
    NexusComponentValueV1 value{}; value.kind = NEXUS_COMPONENT_FLOAT; value.number[0] = 3;
    assert(components.Write(entity, {"Fog", 3}, {"startDistance", 13}, value) == NEXUS_GAME_OK);
    NexusComponentValueV1 actual{};
    assert(components.Read(entity, {"Fog", 3}, {"startDistance", 13}, actual) == NEXUS_GAME_OK && actual.kind == NEXUS_COMPONENT_FLOAT && actual.number[0] == 3);
    value.number[0] = std::numeric_limits<float>::quiet_NaN();
    assert(components.Write(entity, {"Fog", 3}, {"startDistance", 13}, value) == NEXUS_GAME_INVALID_ARGUMENT);
    value.number[0] = 100;
    assert(components.Write(entity, {"Fog", 3}, {"startDistance", 13}, value) == NEXUS_GAME_INVALID_ARGUMENT);
    assert(components.Read(entity, {"Fog", 3}, {"startDistance", 13}, actual) == NEXUS_GAME_OK && actual.number[0] == 3);
    assert(components.Remove(entity, {"Transform", 9}) == NEXUS_GAME_INVALID_ARGUMENT);
    assert(components.Add(entity, {"Script", 6}) == NEXUS_GAME_INVALID_ARGUMENT);
    value = {}; value.kind = NEXUS_COMPONENT_STRING;
    assert(components.Write(entity, {"Name", 4}, {"name", 4}, value, {"名前", 6}) == NEXUS_GAME_OK);
    assert(components.Read(entity, {"Name", 4}, {"name", 4}, actual) == NEXUS_GAME_BUFFER_TOO_SMALL && actual.stringLength == 6);
    char name[6]{};
    assert(components.Read(entity, {"Name", 4}, {"name", 4}, actual, name, sizeof(name)) == NEXUS_GAME_OK && std::memcmp(name, "名前", 6) == 0);
    CommandState state{};
    assert(services.registerCommand(client, {NEXUS_COMPONENT_COMMAND, sizeof(NEXUS_COMPONENT_COMMAND) - 1}, 1, Count, &state) == NEXUS_GAME_INVALID_ARGUMENT);
    // 純粋な読取だけが必要容量を返す。失敗したgetterでWorldを書き換えない。
    NexusComponentRequestV1 header{}; header.operation = NEXUS_COMPONENT_READ; header.typeLength = 3; header.propertyLength = 13;
    std::array<uint8_t, sizeof(header) + 16> request{};
    std::memcpy(request.data(), &header, sizeof(header)); std::memcpy(request.data() + sizeof(header), "FogstartDistance", 16);
    uint32_t length = 0;
    assert(services.invoke(client, {NEXUS_COMPONENT_COMMAND, sizeof(NEXUS_COMPONENT_COMMAND) - 1}, 1, entity,
        {request.data(), static_cast<uint32_t>(request.size())}, nullptr, 0, &length) == NEXUS_GAME_BUFFER_TOO_SMALL && length == sizeof(NexusComponentValueV1));
    assert(services.invoke(client, {NEXUS_COMPONENT_COMMAND, sizeof(NEXUS_COMPONENT_COMMAND) - 1}, 2, entity,
        {request.data(), static_cast<uint32_t>(request.size())}, nullptr, 0, &length) == NEXUS_GAME_INCOMPATIBLE_ABI);
    uint64_t managed = 0; assert(services.open(bridge.World(), &managed) == NEXUS_GAME_OK);
    const NexusGameServicesContextV1 managedContext{1, sizeof(NexusGameServicesContextV1), bridge.World(), managed, services};
    NexusGame::Components managedComponents(managedContext);
    assert(bridge.FreezeManagedServices(true));
    assert(managedComponents.Has(entity, {"Fog", 3}, present) == NEXUS_GAME_OK && present);
    assert(bridge.FreezeManagedServices(false)); assert(services.close(managed) == NEXUS_GAME_OK);
    NexusGameStatus wrongThread = NEXUS_GAME_OK;
    std::thread worker([&] { bool found = false; wrongThread = components.Has(entity, {"Fog", 3}, found); }); worker.join();
    assert(wrongThread == NEXUS_GAME_WRONG_THREAD);
    // 既存Level serializationへ保存され、新たな言語別保存形式を必要としない。
    const auto path = std::filesystem::path("Generated/GameServicesTests/Components.level");
    std::filesystem::create_directories(path.parent_path()); assert(level.Save(path));
    Level restored; assert(restored.Load(path));
    assert(restored.FindEntity(persistent).GetComponent<FogComponent>()->startDistance == 3);
    NexusEntityHandle next{}; assert(game.createEntity(bridge.World(), {}, &next) == NEXUS_GAME_OK);
    const NexusEntityHandle pair[]{entity, next}; NexusLocalTransform values[2]{};
    assert(transforms.Read(pair, 2, values) == NEXUS_GAME_OK);
    NexusTransformUpdateV1 updates[2]{{entity, values[0]}, {next, values[1]}};
    updates[0].transform.translation[0] = 20; updates[1].entity.generation += 1;
    assert(transforms.Write(updates, 2) == NEXUS_GAME_INVALID_HANDLE);
    assert(transforms.Read(pair, 2, values) == NEXUS_GAME_OK && values[0].translation[0] == 0);
    updates[1].entity = next; updates[1].transform.rotation[3] = 0;
    assert(transforms.Write(updates, 2) == NEXUS_GAME_INVALID_ARGUMENT);
    assert(transforms.Read(pair, 2, values) == NEXUS_GAME_OK && values[0].translation[0] == 0);
    updates[1].transform.rotation[3] = 2;
    assert(transforms.Write(updates, 2) == NEXUS_GAME_OK);
    assert(transforms.Read(pair, 2, values) == NEXUS_GAME_OK && values[0].translation[0] == 20 && values[1].rotation[3] == 1);
    // Queryの複数ページでも既存EntityのHandleだけを返し、終端と件数が一致する。
    for(int index = 0; index < 260; ++index) { NexusEntityHandle added{}; assert(game.createEntity(bridge.World(), {}, &added) == NEXUS_GAME_OK); }
    std::array<NexusEntityHandle, 255> pageEntities{}; NexusComponentQueryV1 page{};
    assert(components.Query({"Transform", 9}, 0, pageEntities.data(), 255, page) == NEXUS_GAME_OK && page.count == 255 && page.nextOffset == 255);
    assert(components.Query({"Transform", 9}, page.nextOffset, pageEntities.data(), 255, page) == NEXUS_GAME_OK && page.count == 7 && page.nextOffset == UINT32_MAX);
    assert(components.Remove(entity, {"Fog", 3}) == NEXUS_GAME_OK);
    assert(components.Has(entity, {"Fog", 3}, present) == NEXUS_GAME_OK && !present);
    assert(game.destroyEntity(entity) == NEXUS_GAME_OK);
    assert(components.Has(entity, {"Fog", 3}, present) == NEXUS_GAME_INVALID_HANDLE);
    assert(services.close(client) == NEXUS_GAME_OK);
    assert(components.Has(next, {"Transform", 9}, present) == NEXUS_GAME_INVALID_HANDLE);
}
/** \brief 同じEntityの個別関連付け、主Script昇格、旧token拒否と旧API互換を検証するソース */
void ScriptAttachments() {
    Level level; assert(level.LoadEmpty()); GameApiBridge bridge(level);
    const auto game = GameApiBridge::Api(); const auto services = GameApiBridge::Services();
    NexusEntityHandle entity{}; assert(game.createEntity(bridge.World(), {}, &entity) == NEXUS_GAME_OK);
    uint64_t client = 0; assert(GameApiBridge::OpenNativeServices(bridge.World(), &client) == NEXUS_GAME_OK);
    auto call = [&](NexusScriptAttachmentRequestV1 header, std::string_view type, NexusScriptStateV1* state = nullptr) {
        header.typeLength = static_cast<uint32_t>(type.size());
        std::array<uint8_t, 1024> request{}, response{}; uint32_t length = 0;
        std::memcpy(request.data(), &header, sizeof(header)); if(!type.empty()) std::memcpy(request.data() + sizeof(header), type.data(), type.size());
        const auto status = services.invoke(client, {NEXUS_SCRIPT_ATTACHMENTS_COMMAND, sizeof(NEXUS_SCRIPT_ATTACHMENTS_COMMAND) - 1}, 1, entity,
            {request.data(), static_cast<uint32_t>(sizeof(header) + type.size())}, response.data(), static_cast<uint32_t>(response.size()), &length);
        if(state && status == NEXUS_GAME_OK) { assert(length >= sizeof(*state)); std::memcpy(state, response.data(), sizeof(*state)); }
        return status;
    };
    NexusScriptStateV1 first{}, second{}, again{};
    assert(call({1, 1}, "First", &first) == NEXUS_GAME_OK && first.attachment);
    assert(call({1, 1}, "Second", &second) == NEXUS_GAME_OK && second.attachment != first.attachment);
    assert(call({1, 0}, "Second", &again) == NEXUS_GAME_OK && again.attachment == second.attachment && again.enabled == 1);
    assert(call({3, 0, 0, 0, second.attachment}, {}) == NEXUS_GAME_OK);
    assert(call({2, 0, 0, 0, first.attachment}, {}, &again) == NEXUS_GAME_OK && again.enabled == 1);
    assert(call({2, 0, 0, 0, second.attachment}, {}, &again) == NEXUS_GAME_OK && again.enabled == 0);
    const auto text = std::string(R"({"schemaVersion":1,"types":{}})"); const auto properties = GameApiBridge::Properties();
    assert(properties.write(entity, second.attachment, {text.data(), static_cast<uint32_t>(text.size())}) == NEXUS_GAME_OK);
    // 主Scriptだけを外しても二番目のtoken/値/有効状態は維持される。
    assert(call({4, 0, 0, 0, first.attachment}, {}) == NEXUS_GAME_OK);
    assert(GameApiBridge::Scripts().getState(entity, &again) == NEXUS_GAME_OK && again.attachment == second.attachment && again.enabled == 0);
    assert(call({2, 0, 0, 0, first.attachment}, {}, &again) == NEXUS_GAME_OK && !again.attachment);
    assert(properties.write(entity, first.attachment, {text.data(), static_cast<uint32_t>(text.size())}) == NEXUS_GAME_INVALID_HANDLE);
    assert(call({1, 1}, "First", &again) == NEXUS_GAME_OK && again.attachment != first.attachment);
    auto* owner = level.GetScene()->GetEntity({entity.index, entity.generation}).GetComponent<ScriptComponent>();
    assert(owner->scriptType == "Second" && owner->propertyState == text && owner->additional.size() == 1);
    assert(GameApiBridge::Scripts().setScript(entity, {"First", 5}, 1) == NEXUS_GAME_INVALID_ARGUMENT);
    const auto firstToken=again.attachment;
    NexusScriptStateV1 independent{};
    assert(call({6,1},"First",&independent)==NEXUS_GAME_OK && independent.attachment && independent.attachment!=firstToken);
    assert(owner->additional.size()==2 && owner->IsValid());
    assert(call({3,0,0,0,independent.attachment},{})==NEXUS_GAME_OK);
    assert(call({2,0,0,0,firstToken},{},&again)==NEXUS_GAME_OK && again.enabled==1);
    assert(GameApiBridge::Scripts().removeScript(entity) == NEXUS_GAME_OK);
    assert(!level.GetScene()->GetEntity({entity.index, entity.generation}).HasComponent<ScriptComponent>());
    assert(services.close(client) == NEXUS_GAME_OK);
}
}
/** \brief CLR不要でWorld分離・登録寿命・Event容量・thread・Reload隔離を検証する。実行はユーザーが行う */
void PhysicsOperations() {
    Level level; assert(level.LoadEmpty());
    auto floor=level.CreateEntity("Physics Floor");
    auto* floorBody=floor.AddComponent<ColliderComponent>(); assert(floorBody);
    floorBody->halfExtent={5,0.5f,5};
    floor.GetComponent<TransformComponent>()->translation.y=-0.5f;
    auto ball=level.CreateEntity("Physics Ball");
    auto* body=ball.AddComponent<RigidBodyComponent>(); assert(body);
    body->shape=1; body->radius=0.5f;
    ball.GetComponent<TransformComponent>()->translation.y=3;
    const auto persistent=ball.GetComponent<PersistentIdComponent>()->value;
    GameApiBridge bridge(level);
    uint64_t client=0; assert(GameApiBridge::OpenNativeServices(bridge.World(),&client)==NEXUS_GAME_OK);
    const NexusGameServicesContextV1 context{1,sizeof(NexusGameServicesContextV1),bridge.World(),client,GameApiBridge::Services()};
    NexusGame::Physics physics(context);
    const NexusEntityHandle handle{bridge.World(),ball.GetId().index,ball.GetId().generation};
    // queryは初回frame前でもbodyを同期する。Edit用Update(0)で落下させない。
    assert(level.Update(0)); assert(ball.GetComponent<TransformComponent>()->translation.y==3);
    NexusPhysicsHitV1 hit{};
    assert(physics.Raycast({0,5,0},{0,-2,0},10,hit)==NEXUS_GAME_OK);
    assert(hit.entity.world==bridge.World() && hit.entity.index==handle.index && std::abs(hit.distance-1.5f)<0.05f);
    assert(physics.Raycast({20,5,0},{0,-1,0},10,hit)==NEXUS_GAME_OK && hit.entity.world==0);
    assert(physics.Raycast({0,5,0},{0,0,0},10,hit)==NEXUS_GAME_INVALID_ARGUMENT);
    // 値境界の全件検証。無効Handle・非有限値・誤threadで力を混入させない。
    auto stale=handle; ++stale.generation;
    assert(physics.AddForce(stale,{1,0,0})==NEXUS_GAME_INVALID_HANDLE);
    assert(physics.AddForce(handle,{(std::numeric_limits<float>::quiet_NaN)(),0,0})==NEXUS_GAME_INVALID_ARGUMENT);
    const std::array<float,3> zero{};
    assert(body->force==zero);
    NexusGameStatus status=NEXUS_GAME_OK;
    std::thread worker([&] { status=physics.AddForce(handle,{1,0,0}); }); worker.join();
    assert(status==NEXUS_GAME_WRONG_THREAD);
    assert(physics.AddImpulse(handle,{1,0,0})==NEXUS_GAME_OK);
    assert(level.Update(1.0f/60));
    assert(body->velocity[0]>0.9f && body->impulse==zero);
    // 同じLevelの既存Schedulerで落下と床の衝突を解決する。別ECS/Sceneは生成しない。
    for(int frame=0;frame<180;++frame) assert(level.Update(1.0f/60));
    const float height=ball.GetComponent<TransformComponent>()->translation.y;
    assert(height>0.4f && height<0.65f);
    assert(std::abs(body->velocity[1])<0.1f);
    const auto path=std::filesystem::path("Generated/GameServicesTests/Physics.level");
    std::filesystem::create_directories(path.parent_path()); assert(level.Save(path));
    Level restored; assert(restored.Load(path));
    auto restoredBall=restored.FindEntity(persistent);
    assert(restoredBall.GetComponent<RigidBodyComponent>()->shape==1);
    assert(restoredBall.GetComponent<RigidBodyComponent>()->radius==0.5f);
    // Scene交換では同じEntityIdでも旧solverを破棄する。旧BridgeのHandleも失効する。
    auto candidate=level.GetScene()->Clone(); assert(candidate);
    candidate->GetComponent<TransformComponent>(ball.GetId())->translation={0,8,0};
    auto old=level.ExchangeScene(std::move(candidate)); assert(old);
    assert(physics.AddForce(handle,{1,0,0})==NEXUS_GAME_INVALID_HANDLE);
    auto rebound=level.FindEntity(persistent);
    assert(level.Update(1.0f/60));
    assert(rebound.GetComponent<TransformComponent>()->translation.y>7.9f);
    // 子RigidBodyは未対応として明示的に拒否する。local/worldの誤った混在を許さない。
    auto* hierarchy=rebound.AddComponent<HierarchyComponent>(); assert(hierarchy);
    hierarchy->parent=floor.GetId();
    const float before=rebound.GetComponent<TransformComponent>()->translation.y;
    assert(!level.Update(1.0f/60));
    assert(rebound.GetComponent<TransformComponent>()->translation.y==before);
    hierarchy->parent={};
    rebound.GetComponent<RigidBodyComponent>()->enabled=false;
    auto ray=level.Raycast({0,10,0},{0,-1,0},20); assert(ray && ray->entity==floor.GetId());
    level.Unload(); assert(!level.Raycast({0,10,0},{0,-1,0},20));
}

/** \brief OS frameのrepeat/喪失edgeと、言語共通の非消費snapshotを確認する */
void InputOperations() {
    InputFrame frame; frame.focused=true; frame.Key('W',true); frame.Key('W',true);
    assert(frame.down['W'] && frame.pressed['W']);
    frame.ClearTransitions(); frame.Key('W',true); assert(!frame.pressed['W']);
    frame.Key('A',true); frame.LoseFocus();
    assert(!frame.focused && !frame.down['W'] && frame.released['W'] && !frame.pressed['A']);
    frame.ClearTransitions(); assert(!frame.released['W']);
    frame.focused=true; frame.Key('W',true); frame.mouseX=23; frame.deltaX=2; frame.wheel=1;
    Level level; assert(level.LoadEmpty()); level.SetFrameInput(frame);
    GameApiBridge bridge(level); uint64_t client=0;
    assert(GameApiBridge::OpenNativeServices(bridge.World(),&client)==NEXUS_GAME_OK);
    const NexusGameServicesContextV1 context{1,sizeof(NexusGameServicesContextV1),bridge.World(),client,GameApiBridge::Services()};
    NexusGame::Input input(context); NexusInputFrameV1 first{},second{};
    assert(input.Read(first)==NEXUS_GAME_OK && input.Read(second)==NEXUS_GAME_OK);
    assert(std::memcmp(&first,&second,sizeof(first))==0 && first.focused && first.pressed['W'] && first.mouseX==23);
    assert(bridge.FreezeManagedServices(true)); assert(input.Read(second)==NEXUS_GAME_OK); assert(bridge.FreezeManagedServices(false));
    NexusGameStatus status=NEXUS_GAME_OK;
    std::thread worker([&] { status=input.Read(second); }); worker.join(); assert(status==NEXUS_GAME_WRONG_THREAD);
    assert(GameApiBridge::Services().close(client)==NEXUS_GAME_OK);
    assert(input.Read(second)==NEXUS_GAME_INVALID_HANDLE);
    std::cout << "PASS: shared Input frame, key repeat/edges, focus release, thread and client lifetime\n";
}

/** \brief 実機に依存せずhot-plug/正規化/focus/ABIを確認し、OS pollも接続する */
void GamepadOperations() {
    const std::array<float,2> neutral{};
    InputFrame frame; frame.focused=true;
    GamepadSample sample{true,NEXUS_PAD_A|NEXUS_PAD_DPAD_UP,32767,0,-32768,32767,30,255};
    frame.gamepads[0].Update(sample,true);
    auto& pad=frame.gamepads[0];
    assert(pad.connected && pad.connectedThisFrame && !pad.disconnectedThisFrame);
    assert(pad.down==sample.buttons && pad.pressed==sample.buttons && !pad.released);
    assert(pad.leftStick[0]==1 && pad.leftStick[1]==0 && pad.leftTrigger==0 && pad.rightTrigger==1);
    const float magnitude=pad.rightStick[0]*pad.rightStick[0]+pad.rightStick[1]*pad.rightStick[1];
    assert(pad.rightStick[0]<0 && pad.rightStick[1]>0 && std::abs(magnitude-1)<0.00001f);
    frame.ClearTransitions(); pad.Update(sample,true);
    assert(!pad.pressed && !pad.released && !pad.connectedThisFrame);
    // 一部button解放、独立した別slot、radial dead zoneとtrigger threshold。
    sample.buttons=NEXUS_PAD_A; sample.leftX=100; sample.leftY=-100; sample.leftTrigger=31;
    pad.Update(sample,true); assert(pad.released==NEXUS_PAD_DPAD_UP && pad.leftStick==neutral);
    assert(pad.leftTrigger>0 && pad.leftTrigger<0.01f);
    frame.gamepads[3].Update({true,NEXUS_PAD_B},true); assert(!frame.gamepads[1].connected && frame.gamepads[3].pressed==NEXUS_PAD_B);
    frame.ClearTransitions(); frame.LoseFocus(); pad.Update(sample,false);
    assert(pad.connected && !pad.down && !pad.pressed && pad.released==NEXUS_PAD_A && pad.rightTrigger==0);
    assert(!frame.gamepads[3].down && frame.gamepads[3].released==NEXUS_PAD_B);
    frame.ClearTransitions(); pad.Update(sample,false); assert(!pad.released);
    // Game Viewの遮断と復帰。非focus中にも接続/切断を追跡し、解除edgeは一度だけ返す。
    auto previous=pad; auto allowed=pad; allowed.Update(sample,true); allowed.Gate(previous,true);
    assert(allowed.pressed==NEXUS_PAD_A);
    auto blocked=allowed; blocked.Gate(allowed,false); assert(blocked.released==NEXUS_PAD_A && !blocked.down);
    previous=blocked; blocked.Gate(previous,false); assert(!blocked.released);
    allowed.ClearTransitions(); allowed.Update({},true);
    assert(!allowed.connected && allowed.disconnectedThisFrame && allowed.released==NEXUS_PAD_A && allowed.leftStick==neutral);
    allowed.ClearTransitions(); allowed.Update(sample,true);
    assert(allowed.connected && allowed.connectedThisFrame && !allowed.disconnectedThisFrame && allowed.pressed==NEXUS_PAD_A);
    GamepadFrame boundary; boundary.Update({true,0xffff,7849,0,8689,0,30,30},true);
    assert(boundary.down==GamepadFrame::ButtonMask && boundary.leftStick==neutral && boundary.rightStick==neutral && boundary.rightTrigger==0);
    frame.focused=true; frame.gamepads[0].Update(sample,true);
    Level level; assert(level.LoadEmpty()); level.SetFrameInput(frame); GameApiBridge bridge(level);
    uint64_t client=0; const auto& services=GameApiBridge::Services();
    assert(GameApiBridge::OpenNativeServices(bridge.World(),&client)==NEXUS_GAME_OK);
    NexusGame::Input input({1,sizeof(NexusGameServicesContextV1),bridge.World(),client,services});
    NexusGamepadsFrameV1 first{},second{};
    assert(input.ReadGamepads(first)==NEXUS_GAME_OK && input.ReadGamepads(second)==NEXUS_GAME_OK);
    assert(std::memcmp(&first,&second,sizeof(first))==0 && first.count==4 && first.focused==1 && first.pads[0].down==NEXUS_PAD_A);
    NexusInputFrameV1 keyboard{}; assert(input.Read(keyboard)==NEXUS_GAME_OK && sizeof(keyboard)==800);
    NexusGamepadRequestV1 request{1}; uint32_t length=0; uint8_t sentinel=97;
    const NexusGameString command{NEXUS_INPUT_COMMAND,sizeof(NEXUS_INPUT_COMMAND)-1};
    assert(services.invoke(client,command,1,{}, {reinterpret_cast<const uint8_t*>(&request),sizeof(request)},&sentinel,1,&length)==NEXUS_GAME_BUFFER_TOO_SMALL && length==272 && sentinel==97);
    request.operation=99;
    assert(services.invoke(client,command,1,{}, {reinterpret_cast<const uint8_t*>(&request),sizeof(request)},nullptr,0,&length)==NEXUS_GAME_INVALID_ARGUMENT);
    NexusGameStatus status=NEXUS_GAME_OK;
    std::thread worker([&] { status=input.ReadGamepads(second); }); worker.join(); assert(status==NEXUS_GAME_WRONG_THREAD);
    assert(bridge.FreezeManagedServices(true)); assert(input.ReadGamepads(second)==NEXUS_GAME_OK); assert(bridge.FreezeManagedServices(false));
    assert(services.close(client)==NEXUS_GAME_OK && input.ReadGamepads(second)==NEXUS_GAME_INVALID_HANDLE);
    // 接続台数を仮定しない実XInput smoke。非focus時は実機の操作値をゲームへ流さない。
    InputFrame hardware; PollXInputGamepads(hardware); unsigned connected=0;
    for(const auto& device:hardware.gamepads) { connected+=device.connected ? 1u : 0u; assert(!device.down && device.leftStick==neutral && device.rightTrigger==0); }
    std::cout << "PASS: 4-slot gamepad hot-plug, button edges, radial dead zones, focus, shared ABI/thread/lifetime and XInput poll (connected=" << connected << ")\n";
}

/** \brief 実XAudio2へ無音WAVを渡し、voice寿命とReload時の副作用拒否を確認する */
void AudioOperations() {
    const auto root=std::filesystem::absolute("Generated/GameServicesTests/Audio");
    std::filesystem::create_directories(root);
    assert(NexusTests::WriteSilentWave(root/"silence.wav"));
    { std::ofstream malformed(root/"broken.wav",std::ios::binary); malformed << "RIFF\0\0\0\0WAVE"; }
    Level level; assert(level.LoadEmpty()); assert(level.SetResourceRoot(root));
    GameApiBridge bridge(level); uint64_t client=0,other=0;
    const auto services=GameApiBridge::Services();
    assert(GameApiBridge::OpenNativeServices(bridge.World(),&client)==NEXUS_GAME_OK);
    assert(GameApiBridge::OpenNativeServices(bridge.World(),&other)==NEXUS_GAME_OK);
    NexusGame::Audio audio({1,sizeof(NexusGameServicesContextV1),bridge.World(),client,services});
    NexusGame::Audio foreign({1,sizeof(NexusGameServicesContextV1),bridge.World(),other,services});
    uint64_t voice=0;
    assert(audio.Play({"broken.wav",10},voice,0)==NEXUS_GAME_FAILED && voice==0);
    assert(audio.Play({"../escape.wav",13},voice,0)==NEXUS_GAME_FAILED && voice==0);
    assert(audio.Play({"silence.wav",11},voice,0,true)==NEXUS_GAME_OK && voice);
    bool playing=false; assert(audio.IsPlaying(voice,playing)==NEXUS_GAME_OK && playing);
    assert(foreign.Stop(voice)==NEXUS_GAME_INVALID_HANDLE);
    assert(audio.SetVolume(voice,0.25f)==NEXUS_GAME_OK);
    assert(level.PauseAudio(true) && level.PauseAudio(false));
    assert(bridge.FreezeManagedServices(true));
    uint64_t rejected=0; assert(audio.Play({"silence.wav",11},rejected,0)==NEXUS_GAME_FAILED && rejected==0);
    assert(bridge.FreezeManagedServices(false));
    assert(audio.Stop(voice)==NEXUS_GAME_OK);
    assert(audio.IsPlaying(voice,playing)==NEXUS_GAME_OK && !playing);
    assert(audio.Play({"silence.wav",11},voice,0,true)==NEXUS_GAME_OK);
    assert(services.close(client)==NEXUS_GAME_OK);
    assert(audio.IsPlaying(voice,playing)==NEXUS_GAME_INVALID_HANDLE);
    assert(!*level.Audio().IsPlaying(client,voice));
    assert(services.close(other)==NEXUS_GAME_OK);
    std::cout << "PASS: XAudio2 WAV playback, pause/volume, invalid asset, Reload freeze and owner cleanup\n";
}

/** \brief mesh cooking・接触寿命・購読時だけのEvent配送を同じSceneで検証する */
void PhysicsMeshAndContacts() {
    auto model=PrimitiveMeshBuilder::Build(PrimitiveShape::Cube); assert(model);
    Level level; assert(level.LoadEmpty());
    assert(level.SetPhysicsModelSource([&](std::string_view ref)->Result<const ModelAssetData*> {
        if(ref!="box") return std::unexpected(Error(ErrorCategory::Scene,260,"Unknown test mesh."));
        return &*model;
    }));
    auto floor=level.CreateEntity("Mesh floor"); auto* collider=floor.AddComponent<ColliderComponent>();
    collider->shape=3; collider->meshAsset="box";
    floor.GetComponent<TransformComponent>()->scale={10,1,10};
    floor.GetComponent<TransformComponent>()->translation.y=-0.5f;
    auto ball=level.CreateEntity("Convex body"); auto* body=ball.AddComponent<RigidBodyComponent>();
    body->continuous=true;
    auto* convex=ball.AddComponent<ColliderComponent>(); convex->shape=4; convex->meshAsset="box";
    ball.GetComponent<TransformComponent>()->translation.y=2;
    auto ray=level.Raycast({0,5,0},{0,-1,0},10); assert(ray && ray->entity==ball.GetId());
    GameApiBridge bridge(level); uint64_t client=0,quiet=0;
    const auto& api=GameApiBridge::Services();
    assert(GameApiBridge::OpenNativeServices(bridge.World(),&client)==NEXUS_GAME_OK);
    assert(GameApiBridge::OpenNativeServices(bridge.World(),&quiet)==NEXUS_GAME_OK);
    NexusGame::Physics physics({1,sizeof(NexusGameServicesContextV1),bridge.World(),client,api});
    assert(physics.EnableContactEvents()==NEXUS_GAME_OK);
    bool entered=false,stayed=false;
    for(int i=0;i<240;++i) {
        assert(level.Update(1.0f/60));
        for(const auto& contact:level.PhysicsContacts()) {
            assert(!contact.sensor); entered|=contact.kind==1; stayed|=contact.kind==2;
        }
    }
    assert(entered && stayed && ball.GetComponent<TransformComponent>()->translation.y>0.45f);
    // sleeping bodyでもStayを維持し、値snapshotと既存Eventを同じ形式で公開する。
    bridge.PublishPhysicsContacts();
    NexusPhysicsContactV1 contacts[63]{}; NexusPhysicsContactsHeaderV1 header{};
    assert(physics.ReadContacts(0,contacts,63,header)==NEXUS_GAME_OK && header.count==1 && contacts[0].kind==2);
    NexusGameEventV1 event{}; uint8_t buffer[4224]{}; uint32_t length=0;
    assert(api.poll(client,&event,buffer,sizeof(buffer),&length)==NEXUS_GAME_OK && length>0);
    assert(api.poll(quiet,&event,buffer,sizeof(buffer),&length)==NEXUS_GAME_OK && length==0);
    assert(level.DestroyEntity(ball));
    assert(level.Update(1.0f/60));
    assert(level.PhysicsContacts().size()==1 && level.PhysicsContacts()[0].kind==3);
    assert(level.Update(1.0f/60)); assert(level.PhysicsContacts().empty());
    // Triangle MeshはDynamicとして受理しない。誤った形状をsolverへ登録しない。
    auto invalid=level.CreateEntity("Invalid dynamic mesh"); assert(invalid.AddComponent<RigidBodyComponent>());
    auto* invalidCollider=invalid.AddComponent<ColliderComponent>(); invalidCollider->shape=3; invalidCollider->meshAsset="box";
    assert(!level.Update(1.0f/60));
    assert(level.DestroyEntity(invalid));
    // sensorは応答を行わず、同じEnter/Stay/Exit契約で通知する。
    collider=floor.GetComponent<ColliderComponent>(); collider->shape=0; collider->sensor=true;
    auto trigger=level.CreateEntity("Trigger body"); auto* triggerBody=trigger.AddComponent<RigidBodyComponent>();
    triggerBody->gravityScale=0; trigger.GetComponent<TransformComponent>()->translation.y=0;
    assert(level.Update(1.0f/60));
    assert(!level.PhysicsContacts().empty() && level.PhysicsContacts()[0].sensor && level.PhysicsContacts()[0].kind==1);
    const auto path=std::filesystem::path("Generated/GameServicesTests/Mesh.level"); assert(level.Save(path));
    Level restored; assert(restored.Load(path));
    assert(api.close(client)==NEXUS_GAME_OK && api.close(quiet)==NEXUS_GAME_OK);
    std::cout << "PASS: Jolt triangle/convex cooking, CCD configuration, contact sleep/exit/sensor and opt-in Event routing\n";
}

#include "GameDebugTests.inl"
#include "Public/Nexus/GameTime.h"
#include "Runtime/Reflection/GamePropertyMetadata.h"
#include "Runtime/Scene/EntityReference.h"

namespace {
/** 保存IDと時刻のprotocolを実Worldで検証し、削除・再利用・再読込の境界を確認する。 */
void TimeAndReferenceOperations() {
    Level level;
    assert(level.LoadEmpty());
    auto entity = level.CreateEntity("Persistent target");
    const auto id = entity.GetComponent<PersistentIdComponent>()->value;
    const auto text = FormatEntityReference(id);
    assert(ParseEntityReference(text) == id);
    assert(!ParseEntityReference("not-an-id"));
    assert(ValidateReferenceValue(text + "|Transform", true));
    assert(!ValidateReferenceValue(text + "|", true));
    assert(GamePropertyMetadata::Kind("EntityReference") == PropertyKind::EntityReference);
    assert(std::get<std::string>(GamePropertyMetadata::Value("ComponentReference", text + "|Transform")) == text + "|Transform");
    auto* saved = entity.AddComponent<ScriptComponent>();
    saved->scriptType = "test.references";
    saved->propertyState = "{\"schemaVersion\":1,\"types\":{\"test.references\":{\"version\":1,\"values\":{\"Target\":{\"EntityReference\":\"" + text + "\"},\"Transform\":{\"ComponentReference\":\"" + text + "|Transform\"}}}}}";
    const auto path = std::filesystem::path("Generated/GameServicesTests/References.level");
    std::filesystem::create_directories(path.parent_path());
    assert(level.Save(path));
    Level restored;
    assert(restored.Load(path));
    assert(restored.FindEntity(id).GetComponent<ScriptComponent>()->propertyState == saved->propertyState);
    GameApiBridge bridge(level);
    auto api = GameApiBridge::Services();
    uint64_t client = 0;
    assert(api.open(bridge.World(), &client) == NEXUS_GAME_OK);
    NexusEntityHandle handle{bridge.World(), entity.GetId().index, entity.GetId().generation};
    const auto reference = [&](NexusEntityReferenceRequestV1 request, NexusEntityHandle target, void* output, uint32_t size) {
        uint32_t length = 0;
        constexpr char command[] = NEXUS_REFERENCE_COMMAND;
        const auto status = api.invoke(client, {command, sizeof(command) - 1}, 1, target,
            {reinterpret_cast<const uint8_t*>(&request), sizeof(request)}, static_cast<uint8_t*>(output), size, &length);
        assert(status != NEXUS_GAME_OK || length == size);
        return status;
    };
    NexusEntityReferenceV1 persistent{};
    assert(reference({1}, handle, &persistent, sizeof(persistent)) == NEXUS_GAME_OK);
    assert(persistent.high == id.high && persistent.low == id.low);
    NexusEntityHandle resolved{};
    assert(reference({2, 0, id.high, id.low}, {}, &resolved, sizeof(resolved)) == NEXUS_GAME_OK && resolved.index == handle.index && resolved.generation == handle.generation);
    assert(level.DestroyEntity(entity));
    auto replacement = level.CreateEntity("Reused slot");
    assert(replacement.GetId().index == handle.index);
    assert(reference({2, 0, id.high, id.low}, {}, &resolved, sizeof(resolved)) == NEXUS_GAME_OK && resolved.world == 0);
    auto& clock = level.GetScene()->Time();
    clock.timeScale = .5f;
    assert(clock.BeginFrame(.1f) && clock.pendingSteps == 3 && std::abs(clock.deltaTime - .05f) < 1e-6);
    clock.timeScale = 0;
    assert(clock.BeginFrame(.1f) && clock.pendingSteps == 0 && clock.unscaledElapsed > .19);
    clock.timeScale = 1;
    assert(clock.BeginFrame(10) && clock.pendingSteps == 64 && clock.droppedFixedTime > 8);
    const auto frame = clock.frame;
    assert(!clock.BeginFrame(-1) && !clock.BeginFrame(std::numeric_limits<float>::infinity()) && clock.frame == frame);
    const NexusTimeRequestV1 query{1};
    NexusGameTimeV1 snapshot{};
    uint32_t length = 0;
    constexpr char command[] = NEXUS_TIME_COMMAND;
    assert(api.invoke(client, {command, sizeof(command) - 1}, 1, {}, {reinterpret_cast<const uint8_t*>(&query), sizeof(query)},
        reinterpret_cast<uint8_t*>(&snapshot), sizeof(snapshot), &length) == NEXUS_GAME_OK);
    assert(length == sizeof(snapshot) && snapshot.frame == frame && snapshot.droppedFixedTime > 8);
    assert(api.close(client) == NEXUS_GAME_OK);
}
/** bilateral maskを実simulationで確認し、ray maskと保存値にも同じ32bit契約を適用する。 */
void CollisionLayerOperations() {
    Level level;
    assert(level.LoadEmpty());
    auto floor = level.CreateEntity("Layer floor");
    auto* collider = floor.AddComponent<ColliderComponent>();
    collider->halfExtent = {10, .5f, 10};
    collider->collisionLayer = 0;
    collider->collisionMask = 0;
    floor.GetComponent<TransformComponent>()->translation.y = -.5f;
    auto ball = level.CreateEntity("Layer ball");
    auto* body = ball.AddComponent<RigidBodyComponent>();
    body->shape = 1;
    body->collisionLayer = 31;
    body->collisionMask = 1;
    ball.GetComponent<TransformComponent>()->translation.y = 2;
    GameApiBridge bridge(level);
    uint64_t client = 0;
    assert(GameApiBridge::OpenNativeServices(bridge.World(), &client) == NEXUS_GAME_OK);
    const NexusGameServicesContextV1 context{1, sizeof(NexusGameServicesContextV1), bridge.World(), client, GameApiBridge::Services()};
    NexusGame::Physics physics(context);
    NexusPhysicsHitV1 hit{};
    assert(physics.Raycast({0, 5, 0}, {0, -1, 0}, 20, hit, 0x80000000u) == NEXUS_GAME_OK && hit.entity.index == ball.GetId().index);
    assert(physics.Raycast({0, 5, 0}, {0, -1, 0}, 20, hit, 1) == NEXUS_GAME_OK && hit.entity.index == floor.GetId().index);
    assert(physics.Raycast({0, 5, 0}, {0, -1, 0}, 20, hit, 0) == NEXUS_GAME_OK && hit.entity.world == 0);
    for(int step = 0; step < 90; ++step) {
        assert(level.Update(1.f / 60));
    }
    assert(ball.GetComponent<TransformComponent>()->translation.y < -1);
    // 一方のmaskだけが許可しても通過した。両方を許可すると床で停止する。
    collider->collisionMask = static_cast<int32_t>(0x80000000u);
    ball.GetComponent<TransformComponent>()->translation.y = 2;
    body->velocity = {};
    for(int step = 0; step < 180; ++step) {
        assert(level.Update(1.f / 60));
    }
    assert(ball.GetComponent<TransformComponent>()->translation.y > .4f);
    const auto id = floor.GetComponent<PersistentIdComponent>()->value;
    const auto path = std::filesystem::path("Generated/GameServicesTests/Layers.level");
    assert(level.Save(path));
    Level restored;
    assert(restored.Load(path));
    const auto* saved = restored.FindEntity(id).GetComponent<ColliderComponent>();
    assert(saved && saved->collisionLayer == 0 && static_cast<uint32_t>(saved->collisionMask) == 0x80000000u);
    assert(GameApiBridge::Services().close(client) == NEXUS_GAME_OK);
}
}

void RunGameServicesTests() {
    TimeAndReferenceOperations();
    CollisionLayerOperations();
    std::cout << "PASS: persistent Entity/Component reference values, deletion/reuse/load, clock limits, bilateral collision layers and masked rays\n";
    GameDebugOperations();
    InputOperations();
    GamepadOperations();
    AudioOperations();
    PhysicsOperations();
    PhysicsMeshAndContacts();
    ComponentOperations();
    ScriptAttachments();
    Level level, other; assert(level.LoadEmpty() && other.LoadEmpty());
    // SDKは起動せず、パッチ境界中の新規World拒否と取得Thread限定の解除を検証するソース。
    assert(GameApiBridge::BeginLegacyPatch());
    bool rejected = false;
    std::thread worker([&] {
        GameApiBridge::EndLegacyPatch(); // 他Threadの境界を解除できない。
        try { GameApiBridge denied(other); } catch(const std::runtime_error&) { rejected = true; }
    }); worker.join();
    assert(rejected && !GameApiBridge::BeginLegacyPatch()); GameApiBridge::EndLegacyPatch();
    { LiveCodingAgent disabled(false); assert(LiveCodingAgent::State() == NativeReloadState::Unavailable && !LiveCodingAgent::RequestReload()); }
    GameApiBridge bridge(level), another(other); const auto api = GameApiBridge::Services();
    assert(!GameApiBridge::BeginLegacyPatch());
    uint64_t native = 0, managed = 0, foreign = 0;
    assert(GameApiBridge::OpenNativeServices(bridge.World(), &native) == NEXUS_GAME_OK);
    assert(api.open(bridge.World(), &managed) == NEXUS_GAME_OK && api.open(another.World(), &foreign) == NEXUS_GAME_OK);
    CommandState state{0, GameApiBridge::Api(), native};
    assert(api.registerCommand(native, {"test.count", 10}, 1, Count, &state) == NEXUS_GAME_OK);
    assert(api.registerCommand(native, {"test.count", 10}, 1, Count, &state) == NEXUS_GAME_FAILED);
    assert(api.registerCommand(managed, {"test.denied", 11}, 1, Count, &state) == NEXUS_GAME_FAILED);
    NexusEntityHandle entity{}; assert(state.game.createEntity(bridge.World(), {}, &entity) == NEXUS_GAME_OK);
    uint8_t response[4]{}; uint32_t length = 0;
    assert(api.invoke(managed, {"test.count", 10}, 1, entity, {}, response, 4, &length) == NEXUS_GAME_OK && state.calls == 1 && length == 4);
    assert(api.invoke(foreign, {"test.count", 10}, 1, {}, {}, response, 4, &length) == NEXUS_GAME_INVALID_HANDLE);
    assert(api.invoke(managed, {"test.count", 10}, 1, {another.World(), entity.index, entity.generation}, {}, response, 4, &length) == NEXUS_GAME_INVALID_HANDLE);
    assert(bridge.FreezeManagedServices(true));
    assert(api.invoke(managed, {"test.count", 10}, 1, entity, {}, response, 4, &length) == NEXUS_GAME_FAILED && state.calls == 1);
    assert(api.publish(managed, {"test.event", 10}, 1, entity, {}) == NEXUS_GAME_FAILED);
    assert(bridge.FreezeManagedServices(false));
    NexusGameStatus threaded = NEXUS_GAME_OK;
    std::thread invokeWorker([&] { uint32_t count = 0; threaded = api.invoke(managed, {"test.count", 10}, 1, entity, {}, response, 4, &count); }); invokeWorker.join();
    assert(threaded == NEXUS_GAME_WRONG_THREAD);
    for(int count = 0; count < 256; ++count) assert(api.publish(native, {"test.event", 10}, 1, entity, {}) == NEXUS_GAME_OK);
    assert(api.publish(native, {"test.event", 10}, 1, entity, {}) == NEXUS_GAME_FAILED);
    NexusGameEventV1 event{}; uint8_t buffer[4224]{};
    assert(api.poll(managed, &event, nullptr, 0, &length) == NEXUS_GAME_BUFFER_TOO_SMALL && length == 10);
    const auto sequence = event.sequence;
    assert(api.poll(managed, &event, buffer, 4224, &length) == NEXUS_GAME_OK && event.sequence == sequence && event.source.world == bridge.World());
    assert(api.poll(foreign, &event, buffer, 4224, &length) == NEXUS_GAME_OK && length == 0);
    assert(api.close(managed) == NEXUS_GAME_OK && api.poll(managed, &event, buffer, 4224, &length) == NEXUS_GAME_INVALID_HANDLE);
    assert(api.open(bridge.World(), &managed) == NEXUS_GAME_OK && api.poll(managed, &event, buffer, 4224, &length) == NEXUS_GAME_OK && length == 0);
    assert(api.close(native) == NEXUS_GAME_OK);
    assert(api.invoke(managed, {"test.count", 10}, 1, entity, {}, response, 4, &length) == NEXUS_GAME_INVALID_HANDLE);
}
