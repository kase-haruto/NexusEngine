#include "Runtime/Reflection/NativeComponentMetadata.h"
#include "Runtime/Project/ProjectJson.h"
#include "Runtime/Gameplay/GameApiBridge.h"
#include "Runtime/Gameplay/NativeGameSystemScope.h"
#include "Runtime/Scene/Components/GameComponentData.h"
#include "Editor/Services/SceneEditService.h"
#include "Public/Nexus/EntityBehaviourSystem.hpp"
#include <cassert>
#include <filesystem>

using namespace NexusEngine;
namespace {
constexpr auto Metadata = R"({"schemaVersion":1,"types":[{"id":"Game.Mover","fullName":"Game.NativeMover","version":2,"aliases":["Old.Mover"],"sourceFiles":["NativeMover.cpp"],"properties":[{"id":"enabled","label":"Enabled","aliases":[],"kind":"Bool","default":true},{"id":"speed","label":"Speed","aliases":["oldSpeed"],"kind":"Float","default":1}]},{"id":"Game.Tag","fullName":"Game.Tag","version":1,"aliases":[],"properties":[]}]})";
/*-----------------------------------------------------------------------------------------
 * BehaviourProbe
 * - 既存Scheduler経由のEntity Lifecycleと終了時の書込拒否を記録する検証用Logic
 * - 実際のDLL/CLR/GPUは起動しない。このソースの実行はユーザーが行う
 *---------------------------------------------------------------------------------------*/
struct BehaviourProbe {
    inline static int created = 0, enabled = 0, updated = 0, disabled = 0, destroyed = 0;
    NexusGame::EntityBehaviourContext& context; //< System所有借用先
    explicit BehaviourProbe(NexusGame::EntityBehaviourContext& value) noexcept : context(value) {}
    void OnCreate() { ++created; }
    void OnEnable() { ++enabled; }
    void OnDisable() { ++disabled; }
    void OnDestroy() {
        ++destroyed; NexusLocalTransform transform{};
        assert(context.SetTransform(transform) == NEXUS_GAME_INVALID_HANDLE);
        NexusComponentValueV1 speed{}; speed.kind = NEXUS_COMPONENT_FLOAT;
        assert(context.Write({"speed", 5}, speed) == NEXUS_GAME_INVALID_HANDLE);
    }
    NexusGameStatus OnUpdate(float dt) {
        ++updated; NexusLocalTransform value{};
        auto status = context.GetTransform(value); if(status != NEXUS_GAME_OK) return status;
        value.translation[0] += dt; return context.SetTransform(value);
    }
};
/** \brief 共通Undo/Redo・Level保存にDLL objectや実行tokenが混入しないことを確認する */
void EditorAndSerialization(const ReflectionRegistration& reflection) {
    EditorSession session(reflection); assert(session.Create());
    TypeRegistry types; RegisterSceneTypes(types); reflection.RegisterInspector(types);
    PropertyEditService properties(types); properties.Reset(session.GetLevel()); SceneEditService edits(session, properties);
    auto entity = session.GetLevel()->CreateEntity("Native"); const auto id = entity.GetComponent<PersistentIdComponent>()->value;
    session.Select(id); const auto* mover = types.Find("Game.Mover"); assert(mover && mover->valueBacked);
    assert(edits.AddComponent(*mover));
    const PropertyHandle speed{id, "Game.Mover", "speed"};
    assert(properties.Preview(speed, 7.0f)); properties.Commit();
    assert(properties.Undo() && std::get<float>(*properties.Read(speed)) == 1);
    assert(properties.Redo() && std::get<float>(*properties.Read(speed)) == 7);
    assert(edits.RemoveValueComponent(*mover) && !mover->contains(session.GetLevel()->FindEntity(id)));
    assert(properties.Undo() && std::get<float>(*properties.Read(speed)) == 7);
    entity = session.GetLevel()->FindEntity(id); const auto state = entity.GetComponent<GameComponentData>()->propertyState;
    const auto file = std::filesystem::path("Generated/NativeComponentTests/Native.level");
    std::filesystem::create_directories(file.parent_path()); assert(session.GetLevelDocument()->SaveAs(file));
    LevelDocument loaded(reflection); assert(loaded.Open(file));
    const auto* restored = loaded.GetLevel()->FindEntity(id).GetComponent<GameComponentData>();
    assert(restored && restored->propertyState == state && restored->attachments.empty());
    // 宣言がなくても既存保存基盤だけで未知の値を保持・再保存できる。
    LevelDocument withoutModule; assert(withoutModule.Open(file));
    assert(withoutModule.GetLevel()->FindEntity(id).GetComponent<GameComponentData>()->propertyState == state);
    assert(withoutModule.Save());
    auto* data = entity.GetComponent<GameComponentData>();
    data->propertyState = R"({"schemaVersion":1,"types":{"Game.Mover":{"version":9,"values":{}}}})";
    const auto future = data->propertyState;
    assert(mover->contains(entity) && !mover->properties[1].get(entity) && !mover->properties[1].set(entity, 2.0f));
    assert(!mover->properties[1].unavailableReason(entity).empty() && data->propertyState == future);
    // 旧ID/Property名を読み、編集後も未知の値を失わない。
    data->propertyState = R"({"schemaVersion":1,"types":{"Old.Mover":{"version":1,"values":{"oldSpeed":{"Float":4},"removed":{"String":"retain"}}}}})";
    assert(std::get<float>(*mover->properties[1].get(entity)) == 4);
    assert(mover->properties[1].set(entity, 6.0f) && data->propertyState.find("retain") != std::string::npos);
    assert(std::get<float>(*mover->properties[1].get(entity)) == 6); properties.Reset(nullptr);
}
/** \brief 同じWorldの値API・Scheduler・Entity再追加を通す。別のECSは作らない */
void Gameplay(const ReflectionRegistration& reflection) {
    BehaviourProbe::created = BehaviourProbe::enabled = BehaviourProbe::updated = BehaviourProbe::disabled = BehaviourProbe::destroyed = 0;
    Level level("Native", reflection); assert(level.LoadEmpty()); GameApiBridge bridge(level);
    const auto game = GameApiBridge::Api(); const auto services = GameApiBridge::Services();
    uint64_t client = 0; assert(GameApiBridge::OpenNativeServices(bridge.World(), &client) == NEXUS_GAME_OK);
    const NexusGameServicesContextV1 context{1, sizeof(NexusGameServicesContextV1), bridge.World(), client, services};
    NexusGame::Components components(context); NexusEntityHandle entity{};
    assert(game.createEntity(bridge.World(), {}, &entity) == NEXUS_GAME_OK);
    assert(components.Add(entity, {"Game.Mover", 10}) == NEXUS_GAME_OK);
    assert(components.Add(entity, {"Game.Tag", 8}) == NEXUS_GAME_OK);
    uint64_t first = 0; assert(components.Attachment(entity, {"Game.Mover", 10}, first) == NEXUS_GAME_OK);
    NativeGameSystemScope scope(level, bridge.World()); const auto registration = scope.Context();
    const auto descriptor = NexusGame::EntityBehaviourSystem<BehaviourProbe>::Descriptor({"Probe", 5}); uint64_t system = 0;
    assert(registration.systems.add(registration.scope, &descriptor, &system) == NEXUS_GAME_OK); scope.Seal();
    auto borrowed = context;
    assert(registration.systems.access(registration.scope, system, [](void* state, void* user) -> NexusGameStatus {
        return NexusGame::EntityBehaviourSystem<BehaviourProbe>::Connect(state, *static_cast<NexusGameServicesContextV1*>(user), {"Game.Mover", 10});
    }, &borrowed) == NEXUS_GAME_OK);
    assert(level.Update(0.25f)); assert(BehaviourProbe::created == 1 && BehaviourProbe::enabled == 1 && BehaviourProbe::updated == 1);
    NexusComponentValueV1 enabled{}; enabled.kind = NEXUS_COMPONENT_BOOL;
    assert(components.Write(entity, {"Game.Mover", 10}, {"enabled", 7}, enabled) == NEXUS_GAME_OK);
    assert(level.Update(0.25f)); assert(BehaviourProbe::disabled == 1 && BehaviourProbe::updated == 1);
    enabled.integer = 1; assert(components.Write(entity, {"Game.Mover", 10}, {"enabled", 7}, enabled) == NEXUS_GAME_OK);
    assert(level.Update(0.25f)); assert(BehaviourProbe::enabled == 2 && BehaviourProbe::updated == 2);
    assert(components.Remove(entity, {"Game.Mover", 10}) == NEXUS_GAME_OK);
    bool tag = false; assert(components.Has(entity, {"Game.Tag", 8}, tag) == NEXUS_GAME_OK && tag);
    assert(components.Add(entity, {"Game.Mover", 10}) == NEXUS_GAME_OK);
    uint64_t second = 0; assert(components.Attachment(entity, {"Game.Mover", 10}, second) == NEXUS_GAME_OK && second != first);
    assert(components.Write(entity, {"Game.Mover", 10}, {"enabled", 7}, enabled, {}, first) == NEXUS_GAME_INVALID_HANDLE);
    assert(level.Update(0.25f)); assert(BehaviourProbe::destroyed == 1 && BehaviourProbe::created == 2);
    assert(game.destroyEntity(entity) == NEXUS_GAME_OK);
    assert(level.Update(0.25f)); assert(BehaviourProbe::destroyed == 2);
    NexusEntityHandle replacement{}; assert(game.createEntity(bridge.World(), {}, &replacement) == NEXUS_GAME_OK);
    assert(components.Add(replacement, {"Game.Mover", 10}) == NEXUS_GAME_OK);
    assert(level.Update(0.25f)); assert(BehaviourProbe::created == 3);
    // 実Loaderと同じくclientを先に閉じても、終了callbackはWorldへの書込みを要求しない。
    assert(services.close(client) == NEXUS_GAME_OK); assert(scope.Close()); assert(BehaviourProbe::destroyed == 3);
}
}
/** \brief ユーザーが既存EditorTestsから実行するNative Component検証ソース */
void RunNativeComponentTests() {
    const auto catalog = NativeComponentMetadata::Parse(Metadata, std::filesystem::absolute("Generated/NativeComponentTests/Source")); assert(catalog);
    ReflectionRegistration reflection; reflection.nativeComponents = *catalog;
    TypeRegistry types; reflection.RegisterInspector(types); assert(types.Find("Game.Mover") && types.Find("Game.Tag"));
    assert(types.Find("Game.Mover")->sourceFiles.size() == 1 && !(*catalog)->Register(types));
    auto duplicate = ProjectJson::Parse(Metadata); assert(duplicate);
    (*duplicate)["types"][1]["aliases"] = ProjectJson::Json::array({"Game.Mover"});
    assert(!NativeComponentMetadata::Parse(duplicate->dump()));
    (*duplicate)["types"][1]["aliases"] = ProjectJson::Json::array();
    (*duplicate)["types"][0]["sourceFiles"] = ProjectJson::Json::array({"../escape.cpp"});
    assert(!NativeComponentMetadata::Parse(duplicate->dump()));
    assert(!NativeComponentMetadata::Parse(R"({"schemaVersion":1,"schemaVersion":1,"types":[]})"));
    EditorAndSerialization(reflection); Gameplay(reflection);
}
