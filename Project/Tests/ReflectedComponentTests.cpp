#include "Runtime/Reflection/Generated/GeneratedReflection.h"
#include "Runtime/Reflection/ReflectedInspector.h"
#include "Runtime/Reflection/ComponentReflection.h"
#include "Foundation/Reflection/ReflectionTextArchive.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Services/PropertyEditService.h"
#include "Editor/Services/SceneEditService.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Services/EditorCommandRegistry.h"
#include "Editor/Gui/Panels/DetailsPanel.h"
#include "Editor/Gui/Properties/PropertyCategoryRegistry.h"
#include "Editor/Gui/PropertyDrawerRegistry.h"
#include "ThirdParty/imgui/imgui_internal.h"
#include "Runtime/Application/GameApplication.h"
#include "Game/SampleGame.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace NexusEngine;
using NexusGame::MoveComponent;
namespace {
std::string ReadFile(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void WriteFile(const std::filesystem::path& file, const std::string& text) {
    std::ofstream output(file, std::ios::binary | std::ios::trunc); output << text;
}
void EditorComponentRoundTrip() {
    EditorSession session; assert(session.Create());
    TypeRegistry types; RegisterSceneTypes(types); RegisterGeneratedInspectorTypes(types);
    PropertyEditService properties(types); properties.Reset(session.GetLevel());
    SceneEditService edits(session, properties);
    const auto* type = types.Find("Game.Move");
    assert(type && type->add && type->properties.size() == 1);
    LocalizationService text; assert(text.Load("Project/Resources/Editor/Localization"));
    assert(text.Text(type->labelKey) == "Movement"); text.SetLanguage("ja");
    assert(text.Text(type->labelKey) == "移動");
    auto entity = session.GetLevel()->CreateEntity("Custom component");
    const auto id = entity.GetComponent<PersistentIdComponent>()->value;
    auto second = session.GetLevel()->CreateEntity("Without component");
    const auto otherId = second.GetComponent<PersistentIdComponent>()->value;
    session.Select(id);
    // Detailsの追加一覧と同じTypeDescriptor/編集サービスを使う。
    assert(edits.AddComponent(*type));
    assert(type->contains(session.GetLevel()->FindEntity(id)));
    assert(!edits.AddComponent(*type));
    assert(properties.Undo()); assert(!type->contains(session.GetLevel()->FindEntity(id)));
    assert(properties.Redo());
    const PropertyHandle speed{id, "Game.Move", "speed"};
    assert(properties.Preview(speed, 7.5f)); properties.Commit();
    assert(properties.Undo()); assert(std::get<float>(*properties.Read(speed)) == 3);
    assert(properties.Redo()); assert(std::get<float>(*properties.Read(speed)) == 7.5f);
    assert(!properties.Preview(speed, std::numeric_limits<float>::infinity()));
    const auto file = std::filesystem::path{"Generated/ReflectedComponentTests/Editor.level"};
    std::filesystem::create_directories(file.parent_path());
    assert(session.GetLevel()->Save(file));
    const auto saved = ReadFile(file);
    assert(saved.starts_with("NEXUS_LEVEL 12\n"));
    assert(saved.find("COMPONENT \"Game.Move\"") != std::string::npos);
    assert(session.Open(file)); properties.Reset(session.GetLevel());
    assert(session.GetLevel()->FindEntity(id).GetComponent<MoveComponent>()->speed == 7.5f);
    assert(!session.GetLevel()->FindEntity(otherId).HasComponent<MoveComponent>());
    // Live++の値キャプチャにも同じ生成登録が含まれる。
    ComponentReflection registry; RegisterGeneratedComponents(registry);
    auto snapshot = registry.Capture(*session.GetLevel()->GetScene());
    Scene restored; registry.Restore(restored, snapshot);
    int count = 0;
    restored.ForEach<MoveComponent>([&](Entity, const MoveComponent& value) { ++count; assert(value.speed == 7.5f); });
    assert(count == 1);
    // 不正な値で保存に失敗しても旧ファイルを失わない。
    session.GetLevel()->FindEntity(id).GetComponent<MoveComponent>()->speed = std::numeric_limits<float>::quiet_NaN();
    assert(!session.GetLevel()->Save(file)); assert(ReadFile(file) == saved);
}
void GeneratedComponentMenuAndDetails() {
    EditorSession session; assert(session.Create());
    auto entity = session.GetLevel()->CreateEntity("Menu test");
    const auto id = entity.GetComponent<PersistentIdComponent>()->value; session.Select(id);
    TypeRegistry types; RegisterSceneTypes(types); RegisterGeneratedInspectorTypes(types);
    PropertyEditService properties(types); properties.Reset(session.GetLevel());
    SceneEditService edits(session, properties);
    PropertyDrawerRegistry drawers; drawers.RegisterDefaults();
    LocalizationService text; assert(text.Load("Project/Resources/Editor/Localization"));
    EditorCommandRegistry commands;
    const EditorContext context{session, session.Selection(), properties, types, drawers, text, commands, edits};
    PropertyCategoryRegistry categories; RegisterPropertyPages(categories); DetailsPanel panel(categories);
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.DisplaySize = {1000, 800}; io.DeltaTime = 1.f/60; io.IniFilename = nullptr;
    unsigned char* pixels; int width, height; io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height); io.Fonts->SetTexID(1);
    const auto frame = [&](bool open = false) {
        ImGui::NewFrame(); ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({600,750});
        ImGui::Begin("Generated Component Details");
        if(open) {
            const auto key = std::to_string(id.high) + ":" + std::to_string(id.low);
            ImGui::PushID(key.c_str()); ImGui::OpenPopup("AddComponent"); ImGui::PopID();
        }
        panel.Draw(context); ImGui::End(); ImGui::Render();
    };
    frame(); frame(true); frame();
    assert(!ImGui::GetCurrentContext()->OpenPopupStack.empty());
    auto* popup = ImGui::GetCurrentContext()->OpenPopupStack.back().Window; assert(popup);
    const auto* type = types.Find("Game.Move"); assert(type);
    ImGui::FocusWindow(popup);
    ImGui::ActivateItemByID(popup->GetID(text.Label(type->labelKey, type->id).c_str()));
    frame(); frame();
    assert(session.GetSelectedEntity().HasComponent<MoveComponent>());
    const auto available = categories.Available(context);
    assert(std::ranges::any_of(available, [](const auto* category) { return category->id == "Other"; }));
    // 自動分類された「その他」ページを実際に開き、生成propertyを描画する。
    ImGuiWindow* navigation = nullptr;
    for(auto* window : ImGui::GetCurrentContext()->Windows)
        if(window->Active && window->ParentWindow && std::string_view(window->ParentWindow->Name) == "Generated Component Details" &&
            std::string_view(window->Name).find("CategoryNavigation") != std::string_view::npos) navigation = window;
    assert(navigation); ImGui::FocusWindow(navigation);
    ImGui::ActivateItemByID(ImHashStr("##category", 0, ImHashStr("Other", 0, navigation->ID)));
    frame(); frame(); assert(panel.ActiveCategory() == "Other");
    assert(properties.Undo()); frame(); assert(!session.GetSelectedEntity().HasComponent<MoveComponent>());
    ImGui::DestroyContext();
}
void LevelValidationAndSchemaDefaults() {
    const auto file = std::filesystem::path{"Generated/ReflectedComponentTests/Validation.level"};
    Level level; assert(level.LoadEmpty());
    auto entity = level.CreateEntity("Keep me");
    const auto id = entity.GetComponent<PersistentIdComponent>()->value;
    assert(entity.AddComponent<MoveComponent>(MoveComponent{7.5f}));
    assert(level.Save(file)); const auto valid = ReadFile(file);
    const std::string original = "COMPONENT \"Game.Move\" object 2 \"fields\" object 1 \"speed\" float 7.5 \"type\" string \"Game.Move\"";
    assert(valid.find(original) != std::string::npos);
    auto replaceRecord = [&](const std::string& record) {
        auto changed = valid; changed.replace(changed.find(original), original.size(), record); WriteFile(file, changed);
    };
    auto rejectsWithoutReplacing = [&](const std::string& record) {
        auto* scene = level.GetScene(); replaceRecord(record);
        assert(!level.Load(file)); assert(level.GetScene() == scene);
        assert(level.FindEntity(id).GetComponent<MoveComponent>()->speed == 7.5f);
    };
    rejectsWithoutReplacing("COMPONENT \"Game.Removed\" null");
    rejectsWithoutReplacing("COMPONENT \"Transform\" null");
    rejectsWithoutReplacing("COMPONENT \"Game.Move\" object 2 \"fields\" object 1 \"speed\" string \"bad\" \"type\" string \"Game.Move\"");
    rejectsWithoutReplacing("COMPONENT \"Game.Move\" object 2 \"fields\" object 1 \"speed\" float 1e300 \"type\" string \"Game.Move\"");
    rejectsWithoutReplacing("COMPONENT \"Game.Move\" object 2 \"fields\" object 1 \"speed\" float 7.5 \"type\" string \"WrongType\"");
    rejectsWithoutReplacing(original + "\n" + original);
    auto duplicate = valid;
    duplicate.replace(duplicate.find("COMPONENTS 1"), std::string("COMPONENTS 1").size(), "COMPONENTS 2");
    duplicate.replace(duplicate.find(original), original.size(), original + "\n" + original);
    WriteFile(file, duplicate); assert(!level.Load(file));
    // 追加フィールドの欠落は既定値、削除済みフィールドは読み飛ばす。
    replaceRecord("COMPONENT \"Game.Move\" object 2 \"fields\" object 1 \"removed\" int 100 \"type\" string \"Game.Move\"");
    assert(level.Load(file)); assert(level.FindEntity(id).GetComponent<MoveComponent>()->speed == 3);
    WriteFile(file, valid); assert(level.Load(file));
}
void GameLoadsAuthoredComponent() {
    Level level; assert(level.Load("Project/Resources/Assets/Levels/RuntimeDemoLevel.json"));
    const PersistentId id{70001, 3};
    auto entity = level.FindEntity(id); assert(entity.IsAlive());
    assert(entity.AddComponent<MoveComponent>(MoveComponent{2}));
    const auto file = std::filesystem::path{"Generated/ReflectedComponentTests/Runtime.level"};
    assert(level.Save(file));
    GameApplication game; assert(game.SetGameLevelSetup(NexusGame::SetupSampleGame));
    assert(game.Load(file, "Project/Resources/Assets")); assert(game.Update(0.5f));
    auto played = game.Runtime().GetLevel()->FindEntity(id);
    assert(played.GetComponent<TransformComponent>()->translation.x == 1);
    assert(level.FindEntity(id).GetComponent<TransformComponent>()->translation.x == 0);
}
void TextArchiveTypesAndInvalidData() {
    const ReflectionValue value(ReflectionValue::Object{
        {"null", {}}, {"bool", ReflectionValue(true)}, {"int", ReflectionValue(int64_t{-42})},
        {"uint", ReflectionValue(std::numeric_limits<uint64_t>::max())},
        {"float", ReflectionValue(0.1234567890123456)},
        {"string", ReflectionValue(std::string("日本語 \"quoted\" \\ path\nline"))},
        {"array", ReflectionValue(ReflectionValue::Array{ReflectionValue(false), ReflectionValue(2.5)})}});
    std::ostringstream output; WriteReflectionTextArchive(output, value);
    std::istringstream input(output.str()); const auto decoded = ReadReflectionTextArchive(input);
    std::ostringstream rewritten; WriteReflectionTextArchive(rewritten, decoded); assert(output.str() == rewritten.str());
    for(const auto* malformed : {"object 2 \"x\" null \"x\" null", "array 999999999999", "uint -1", "bool 2", "string \"unfinished", "float 1e999", "mystery 0"}) {
        std::istringstream bad(malformed); bool rejected = false;
        try { static_cast<void>(ReadReflectionTextArchive(bad)); } catch(const std::exception&) { rejected = true; }
        assert(rejected);
    }
    std::string deep;
    for(int i = 0; i < 66; ++i) deep += "array 1 ";
    deep += "null"; std::istringstream inputDeep(deep); bool rejected = false;
    try { static_cast<void>(ReadReflectionTextArchive(inputDeep)); } catch(const std::exception&) { rejected = true; }
    assert(rejected);
}
}
void RunReflectedComponentTests() {
    EditorComponentRoundTrip(); GeneratedComponentMenuAndDetails(); LevelValidationAndSchemaDefaults();
    GameLoadsAuthoredComponent(); TextArchiveTypesAndInvalidData();
    std::cout << "PASS: game component registration, edit/undo, Level persistence, schema defaults, runtime loading and malformed data rejection\n";
}
