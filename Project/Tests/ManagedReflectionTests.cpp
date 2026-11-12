#include "Runtime/Reflection/ManagedScriptMetadata.h"
#include "Runtime/Project/ProjectJson.h"
#include "Runtime/Gameplay/GameApiBridge.h"
#include "Runtime/Scene/Components/ScriptComponent.h"
#include "Editor/Services/PropertyEditService.h"
#include "Editor/Services/SceneEditService.h"
#include "Editor/Gui/PropertyDrawerRegistry.h"
#include "Runtime/Level/LevelDocument.h"
#include "Game/GameReflection.h"
#include <cassert>
#include <fstream>
#include <thread>

using namespace NexusEngine;
namespace {
/** \brief C++ / C#の値を同一Registry・Drawer・履歴・Documentで扱う。別Inspectorは作らない */
void CommonGameplayProperties(const std::filesystem::path& root, const std::filesystem::path& metadata) {
    auto catalog = ManagedScriptMetadata::Load(metadata); assert(catalog);
    TypeRegistry types; RegisterSceneTypes(types); NexusGame::RegisterInspector(types); assert((*catalog)->Register(types));
    LevelDocument document(NexusGame::SampleReflection()); assert(document.Create("Hybrid Properties")); auto* level = document.GetLevel();
    auto entity = level->CreateEntity("Hybrid"); assert(entity.AddComponent<NexusGame::MoveComponent>());
    auto* script = entity.AddComponent<ScriptComponent>(); script->scriptType = "game.player";
    const auto id = entity.GetComponent<PersistentIdComponent>()->value;
    const std::array<PropertyHandle, 2> handles{{ {id, "Game.Move", "speed"}, {id, "ManagedScript:game.player", "speed"} }};
    PropertyEditService edits(types); edits.Reset(level);
    const auto* native = types.Find(handles[0].type); const auto* managed = types.Find(handles[1].type); assert(native && managed);
    // 実際のImGuiを起動せず、同じFloat Drawerへ両Descriptorが到達する検証ソースを用意する。
    PropertyDrawerRegistry drawers; size_t drawn = 0; LocalizationService text;
    drawers.Register(PropertyKind::Float, [&](const auto&, auto& value, const auto& descriptor, const auto&) {
        assert(descriptor.kind == PropertyKind::Float && std::holds_alternative<float>(value)); ++drawn; return UI::ItemResult{};
    });
    auto cppValue = *edits.Read(handles[0]), csValue = *edits.Read(handles[1]);
    drawers.Draw({}, cppValue, native->properties.front(), text); drawers.Draw({}, csValue, managed->properties.front(), text); assert(drawn == 2);
    const std::array<PropertyValue, 2> changed{9.0f, 11.0f};
    assert(edits.PreviewValues(handles, changed)); edits.Commit();
    assert(edits.Undo() && std::get<float>(*edits.Read(handles[0])) == 3 && std::get<float>(*edits.Read(handles[1])) == 5);
    assert(edits.Redo() && std::get<float>(*edits.Read(handles[0])) == 9 && std::get<float>(*edits.Read(handles[1])) == 11);
    const std::array<PropertyValue, 2> invalid{12.0f, std::string("invalid")};
    assert(!edits.PreviewValues(handles, invalid));
    assert(std::get<float>(*edits.Read(handles[0])) == 9 && std::get<float>(*edits.Read(handles[1])) == 11);
    assert(edits.Cancel());
    // 全setterが成功しても、Script型変更で同じtransactionの旧Propertyが読めなくなる編集は拒否する。
    // C++側の先行編集も含めて戻し、言語ごとのInspectorや別の履歴へ分けない。
    edits.MarkSaved(); const auto revision = edits.Revision(); const auto stateBeforeSwitch = script->propertyState;
    const PropertyHandle scriptType{id, "Script", "scriptType"}; assert(edits.Read(scriptType));
    const std::array<PropertyHandle, 3> typeLast{{handles[0], handles[1], scriptType}}, typeFirst{{scriptType, handles[1], handles[0]}};
    const std::array<PropertyValue, 3> lastValues{12.0f, 14.0f, std::string("Other.Script")}, firstValues{std::string("Other.Script"), 14.0f, 12.0f};
    assert(!edits.PreviewValues(typeLast, lastValues));
    assert(script->scriptType == "game.player" && script->propertyState == stateBeforeSwitch && std::get<float>(*edits.Read(handles[0])) == 9 && std::get<float>(*edits.Read(handles[1])) == 11);
    assert(edits.Cancel() && !edits.IsDirty() && edits.Revision() == revision);
    assert(!edits.PreviewValues(typeFirst, firstValues));
    assert(script->scriptType == "game.player" && script->propertyState == stateBeforeSwitch && std::get<float>(*edits.Read(handles[0])) == 9 && std::get<float>(*edits.Read(handles[1])) == 11);
    assert(edits.Cancel() && !edits.IsDirty() && edits.Revision() == revision);
    // 失敗したbatchを履歴へ追加せず、既存の成功操作を同じUndo / Redoで扱える。
    assert(edits.Undo() && std::get<float>(*edits.Read(handles[0])) == 3 && std::get<float>(*edits.Read(handles[1])) == 5);
    assert(edits.Redo() && std::get<float>(*edits.Read(handles[0])) == 9 && std::get<float>(*edits.Read(handles[1])) == 11 && !edits.IsDirty());
    // 同じ型のAliasへの切替は全Handleを維持するため、一律禁止せず通常のPreview / Cancelで扱う。
    const std::array<PropertyValue, 3> aliasValues{12.0f, 14.0f, std::string("Old.Player")};
    assert(edits.PreviewValues(typeLast, aliasValues) && script->scriptType == "Old.Player");
    assert(std::get<float>(*edits.Read(handles[0])) == 12 && std::get<float>(*edits.Read(handles[1])) == 14);
    assert(edits.Cancel() && script->scriptType == "game.player" && script->propertyState == stateBeforeSwitch && !edits.IsDirty());
    script->instance.value = 123; script->attachment = 456;
    const auto savedState = script->propertyState; const auto file = root / "Hybrid.level"; assert(document.SaveAs(file));
    LevelDocument loaded(NexusGame::SampleReflection()); assert(loaded.Open(file));
    auto restored = loaded.GetLevel()->FindEntity(id); const auto* restoredScript = restored.GetComponent<ScriptComponent>();
    assert(restored.IsAlive() && restored.GetComponent<NexusGame::MoveComponent>()->speed == 9 && restoredScript && restoredScript->propertyState == savedState && !restoredScript->instance.value && !restoredScript->attachment);
    // 新しい既知Versionは表示値にも書込先にも使わず、opaqueな保存値を保持する。
    script->propertyState = R"({"schemaVersion":1,"types":{"game.player":{"version":3,"values":{"speed":{"Float":13}}},"Old.Player":{"version":4,"values":{}}}})";
    const auto future = script->propertyState;
    assert(!edits.Read(handles[1]) && !managed->properties[0].set(entity, 8.0f));
    assert(managed->properties[0].unavailableReason && managed->properties[0].unavailableReason(entity).find("newer") != std::string::npos && script->propertyState == future);
    assert(document.Save()); assert(loaded.Open(file)); assert(loaded.GetLevel()->FindEntity(id).GetComponent<ScriptComponent>()->propertyState == future);
    // 同じ永続Handleでも、新しいCatalogなら値を読める。旧Descriptorは旧契約のまま拒否する。
    auto upgradedJson = ProjectJson::Read(metadata); assert(upgradedJson); (*upgradedJson)["types"][0]["version"] = 4;
    const auto upgradedFile = root / "Upgraded.metadata.json"; assert(ProjectJson::Write(upgradedFile, *upgradedJson));
    auto upgraded = ManagedScriptMetadata::Load(upgradedFile); assert(upgraded);
    auto newTypes = types; (*catalog)->Unregister(newTypes); assert((*upgraded)->Register(newTypes));
    PropertyEditService newEdits(newTypes); newEdits.Reset(level);
    assert(std::get<float>(*newEdits.Read(handles[1])) == 13 && !edits.Read(handles[1]) && script->propertyState == future);
    newEdits.Reset(nullptr);
    // 無関係な型の将来Versionは保持し、現在型の編集を妨げない。
    script->propertyState = R"({"schemaVersion":1,"types":{"unrelated":{"version":100,"values":{"removed":{"String":"retain"}}}}})";
    assert(managed->properties[0].set(entity, 6.0f) && script->propertyState.find("retain") != std::string::npos);
    edits.Reset(nullptr);
}
}
/** \brief CLR不要で共通Property履歴・Level保存・Alias・不正Metadataの拒否を検証する。実行はユーザーが行う */
/** \brief 同型Scriptも永続IDで編集・個別解除し、Undo/保存で混同しないことを確認する */
void SameTypeScriptInstances(const std::filesystem::path& root,const std::filesystem::path& metadata) {
    auto catalog=ManagedScriptMetadata::Load(metadata); assert(catalog);
    TypeRegistry types; assert((*catalog)->Register(types));
    const auto* type=types.Find("ManagedScript:game.player"); assert(type && type->instances && type->addInstance);
    EditorSession session; assert(session.Create());
    auto entity=session.GetLevel()->CreateEntity("Repeated Script");
    const auto id=entity.GetComponent<PersistentIdComponent>()->value;
    session.Select(id);
    PropertyEditService history(types); history.Reset(session.GetLevel());
    SceneEditService edits(session,history);
    assert(edits.AddScriptInstance(*type) && edits.AddScriptInstance(*type));
    auto slots=type->instances(session.GetLevel()->FindEntity(id)); assert(slots.size()==2 && slots[0]!=slots[1]);
    const PropertyHandle first{id,type->id,"speed",slots[0]},second{id,type->id,"speed",slots[1]};
    assert(history.Preview(first,2.0f)); history.Commit();
    assert(history.Preview(second,7.0f)); history.Commit();
    assert(std::get<float>(*history.Read(first))==2 && std::get<float>(*history.Read(second))==7);
    assert(history.Undo() && std::get<float>(*history.Read(second))==5 && std::get<float>(*history.Read(first))==2);
    assert(history.Redo());
    const auto file=root/"RepeatedScripts.level"; assert(session.GetLevelDocument()->SaveAs(file));
    Level loaded; assert(loaded.Load(file));
    assert(type->instances(loaded.FindEntity(id))==slots);
    PropertyEditService loadedValues(types); loadedValues.Reset(&loaded);
    assert(std::get<float>(*loadedValues.Read(first))==2 && std::get<float>(*loadedValues.Read(second))==7);
    assert(edits.DetachScript(*type,slots[0]));
    assert(!history.Read(first) && std::get<float>(*history.Read(second))==7);
    assert(history.Undo());
    assert(type->instances(session.GetLevel()->FindEntity(id))==slots && std::get<float>(*history.Read(first))==2);
    assert(history.Redo() && !history.Read(first) && std::get<float>(*history.Read(second))==7);
    auto* component=session.GetLevel()->FindEntity(id).GetComponent<ScriptComponent>();
    ScriptAttachment duplicate=*component; component->additional.push_back(std::move(duplicate));
    assert(!component->IsValid()); // Typeの重複ではなく、同じ永続instance IDの二重所有を拒否する。
}

void RunManagedReflectionTests() {
    const auto root = std::filesystem::absolute("Generated/ManagedReflectionTests"); std::filesystem::create_directories(root);
    const auto metadata = root / "GameScripts.metadata.json";
    {
        std::ofstream file(metadata);
        file << R"({"schemaVersion":1,"types":[{"id":"game.player","fullName":"Game.Player","version":2,"aliases":["Old.Player"],"properties":[{"id":"speed","label":"Speed","aliases":["oldSpeed"],"kind":"Float","default":5}]}]})";
    }
    auto catalog = ManagedScriptMetadata::Load(metadata); assert(catalog);
    CommonGameplayProperties(root, metadata);
    SameTypeScriptInstances(root,metadata);
    TypeRegistry types; assert((*catalog)->Register(types) && !(*catalog)->Register(types));
    const auto* descriptor = types.Find("ManagedScript:game.player"); assert(descriptor && descriptor->properties.size() == 2);
    // 旧sidecarはSource情報なしで読み込み、追加情報は同じ共通Descriptorへ渡す。
    assert(descriptor->sourceFiles.empty());
    auto sourceJson = ProjectJson::Read(metadata); assert(sourceJson);
    (*sourceJson)["types"][0]["sourceFiles"] = ProjectJson::Json::array({"Player.cs"});
    const auto withSource = root / "Source.metadata.json";
    assert(ProjectJson::Write(withSource, *sourceJson));
    auto sourceCatalog = ManagedScriptMetadata::Load(withSource); assert(sourceCatalog);
    TypeRegistry sourceTypes; assert((*sourceCatalog)->Register(sourceTypes));
    assert(sourceTypes.Find(descriptor->id)->sourceFiles == std::vector<std::string>{"Player.cs"});
    {
        EditorSession session; assert(session.Create());
        auto target = session.GetLevel()->CreateEntity("Attach target");
        const auto targetId = target.GetComponent<PersistentIdComponent>()->value;
        auto* previous = target.AddComponent<ScriptComponent>(); previous->scriptType = "Other.Script";
        previous->propertyState = R"({"schemaVersion":1,"types":{}})";
        session.Select(targetId);
        PropertyEditService history(types); history.Reset(session.GetLevel());
        SceneEditService structural(session, history);
        assert(structural.AddComponent(*descriptor));
        const auto* attached = session.GetLevel()->FindEntity(targetId).GetComponent<ScriptComponent>();
        assert(attached->scriptType == "Other.Script" && attached->additional.size() == 1 && attached->additional[0].scriptType == "game.player");
        assert(history.Undo());
        auto* restoredScript = session.GetLevel()->FindEntity(targetId).GetComponent<ScriptComponent>();
        assert(restoredScript->scriptType == "Other.Script" && !restoredScript->propertyState.empty() && restoredScript->additional.empty());
        assert(history.Redo());
        assert(session.GetLevel()->FindEntity(targetId).GetComponent<ScriptComponent>()->additional.size() == 1);
        const PropertyHandle enabled{targetId, descriptor->id, "@enabled"};
        assert(history.Preview(enabled, false)); history.Commit();
        assert(!session.GetLevel()->FindEntity(targetId).GetComponent<ScriptComponent>()->additional[0].enabled);
        assert(session.GetLevel()->FindEntity(targetId).GetComponent<ScriptComponent>()->enabled);
        assert(structural.DetachScript(*descriptor));
        assert(session.GetLevel()->FindEntity(targetId).GetComponent<ScriptComponent>()->scriptType == "Other.Script");
        assert(session.GetLevel()->FindEntity(targetId).GetComponent<ScriptComponent>()->additional.empty());
        assert(history.Undo() && session.GetLevel()->FindEntity(targetId).GetComponent<ScriptComponent>()->additional.size() == 1);
        // 追加Scriptの値と順序を保存し、実行IDは復元しない。
        auto* data = session.GetLevel()->FindEntity(targetId).GetComponent<ScriptComponent>();
        data->additional[0].propertyState = R"({"schemaVersion":1,"types":{"game.player":{"version":2,"values":{"speed":{"Float":8}}}}})";
        data->additional[0].instance.value = 123; data->additional[0].attachment = 456;
        const auto multiple = root / "Multiple.level"; assert(session.GetLevelDocument()->SaveAs(multiple));
        LevelDocument loaded; assert(loaded.Open(multiple));
        const auto* restored = loaded.GetLevel()->FindEntity(targetId).GetComponent<ScriptComponent>();
        assert(restored && restored->scriptType == "Other.Script" && restored->additional.size() == 1);
        assert(restored->additional[0].propertyState == data->additional[0].propertyState && !restored->additional[0].enabled);
        assert(!restored->additional[0].instance.value && !restored->additional[0].attachment);
    }
    Level level; assert(level.LoadEmpty()); auto entity = level.CreateEntity("Player");
    auto* script = entity.AddComponent<ScriptComponent>(); script->scriptType = "Old.Player";
    assert(descriptor->contains(entity));
    auto id = entity.GetComponent<PersistentIdComponent>()->value;
    PropertyHandle handle{id, descriptor->id, "speed"};
    PropertyEditService edits(types); edits.Reset(&level);
    assert(std::get<float>(*edits.Read(handle)) == 5);
    script->propertyState = R"({"schemaVersion":1,"types":{"Old.Player":{"version":1,"values":{"oldSpeed":{"Float":7},"obsolete":{"String":"keep"}}}}})";
    assert(std::get<float>(*edits.Read(handle)) == 7);
    edits.Begin(handle); assert(edits.Preview(handle, 9.0f)); edits.Commit();
    assert(edits.Undo() && std::get<float>(*edits.Read(handle)) == 7);
    assert(edits.Redo() && std::get<float>(*edits.Read(handle)) == 9);
    assert(script->propertyState.find("keep") != std::string::npos);
    assert(!descriptor->properties[0].set(entity, std::string("wrong kind")));
    script->instance.value = 42; script->attachment = 99;
    assert(level.Save(root / "Player.level")); Level restored; assert(restored.Load(root / "Player.level"));
    const auto* saved = restored.FindEntity(id).GetComponent<ScriptComponent>();
    assert(saved && saved->propertyState == script->propertyState && !saved->instance.value && !saved->attachment);
    // 型変更前の値を保持しつつ、対応kindのないPropertyは既定値で表示する。
    script->propertyState = R"({"schemaVersion":1,"types":{"game.player":{"version":1,"values":{"speed":{"Int":3}}}}})";
    assert(std::get<float>(*edits.Read(handle)) == 5);
    assert(descriptor->properties[0].set(entity, 4.0f));
    assert(script->propertyState.find("\"Int\":3") != std::string::npos);
    const auto before = script->propertyState;
    GameApiBridge bridge(level); const auto api = GameApiBridge::Properties();
    const auto local = entity.GetId(); NexusEntityHandle native{bridge.World(), local.index, local.generation};
    NexusScriptStateV1 state{}; assert(GameApiBridge::Scripts().getState(native, &state) == NEXUS_GAME_OK);
    const std::string malformed = R"({"schemaVersion":1,"schemaVersion":1,"types":{}})";
    assert(api.write(native, state.attachment, {malformed.data(), static_cast<uint32_t>(malformed.size())}) == NEXUS_GAME_INVALID_ARGUMENT);
    assert(script->propertyState == before);
    uint32_t length = 0; assert(api.read(native, state.attachment, nullptr, 0, &length) == NEXUS_GAME_BUFFER_TOO_SMALL && length == before.size());
    NexusGameStatus wrongThread = NEXUS_GAME_OK;
    std::thread worker([&] { uint32_t size = 0; wrongThread = api.read(native, state.attachment, nullptr, 0, &size); }); worker.join();
    assert(wrongThread == NEXUS_GAME_WRONG_THREAD);
    assert(GameApiBridge::Scripts().removeScript(native) == NEXUS_GAME_OK);
    assert(GameApiBridge::Scripts().setScript(native, {"game.player", 11}, 1) == NEXUS_GAME_OK);
    assert(api.write(native, state.attachment, {before.data(), static_cast<uint32_t>(before.size())}) == NEXUS_GAME_INVALID_HANDLE);
    // 不正な既知値は既定値で上書きせず、編集失敗として返す。
    script = entity.GetComponent<ScriptComponent>(); script->propertyState = malformed;
    assert(!edits.Read(handle) && !descriptor->properties[0].set(entity, 3.0f) && script->propertyState == malformed);
    {
        std::ofstream file(metadata); file << R"({"schemaVersion":1,"schemaVersion":1,"types":[]})";
    }
    assert(!ManagedScriptMetadata::Load(metadata));
}
