#include "Editor/Gui/ComponentBrowser/ComponentBrowserModel.h"
#include "Runtime/Reflection/NativeComponentMetadata.h"
#include "Runtime/Reflection/ManagedScriptMetadata.h"
#include "Runtime/Project/ProjectJson.h"
void ComponentBrowserIntegration() {
    Fixture f;
    f.session.GetReflectionRegistration().RegisterInspector(f.types);
    const auto id = Id(f.session.GetLevel()->CreateEntity("Browser target"));
    f.session.Select(id);
    LocalizationService text;
    assert(text.Load(std::filesystem::absolute("Project/Resources/Editor/Localization")));
    ComponentBrowserModel model;
    model.Update(f.types, text);
    assert(!model.Filter("CAMERA", "Camera", {}).empty());
    assert(!model.Filter("CameraComponent", "Camera", {}).empty());
    assert(!model.Filter("collision", "Physics", {}).empty());
    assert(model.Filter("collision", "Camera", {}).empty());
    assert(!model.Filter("Rendering", "", {}).empty());
    const auto* cached = model.Entries().data();
    model.Update(f.types, text);
    assert(model.Entries().data() == cached);
    const auto generation = f.types.Generation();
    assert(!f.types.Register(*f.types.Find("Camera")) && f.types.Generation() == generation);
    auto plugin = *f.types.Find("Camera");
    plugin.id = "Plugin.Viewer";
    plugin.editor = {"Viewer", "Navigation", "Find a route through the level.", "details.Camera", "Plugin", {"waypoint"}};
    assert(f.types.Register(plugin) && f.types.Generation() != generation);
    model.Update(f.types, text);
    assert(model.Filter("WAYPOINT", "Navigation", {}).size() == 1);
    assert(f.types.Unregister(plugin.id));
    model.Update(f.types, text);
    assert(model.Filter("waypoint", "", {}).empty());
    text.SetLanguage("ja");
    model.Update(f.types, text);
    assert(!model.Filter("衝突", "Physics", {}).empty());
    text.SetLanguage("en");

    const auto native = NativeComponentMetadata::Parse(R"({"schemaVersion":1,"types":[{
        "id":"Game.Health","fullName":"Game.Health","version":1,"aliases":[],"properties":[],
        "editor":{"displayName":"Health Stats","category":"Gameplay","description":"Tracks damage and health.",
            "icon":"details.Object","keywords":["hitpoints"]}}]})");
    assert(native && (*native)->Register(f.types));
    const auto directory = std::filesystem::absolute("Generated/ComponentBrowserTests");
    std::filesystem::create_directories(directory);
    const auto managedFile = directory / "Scripts.metadata.json";
    {
        std::ofstream file(managedFile);
        file << R"({"schemaVersion":1,"types":[{"id":"demo.controller","fullName":"Game.PlayerController","version":1,
            "aliases":[],"properties":[],"editor":{"displayName":"Player Controller","category":"Gameplay",
            "description":"Responds to player input.","icon":"component.Script","keywords":["joystick"]}}]})";
    }
    const auto managed = ManagedScriptMetadata::Load(managedFile);
    assert(managed && (*managed)->Register(f.types));
    model.Update(f.types, text);
    assert(model.Filter("hitpoints", "Gameplay", {}).size() == 1);
    assert(model.Filter("joystick", "Gameplay", {}).size() == 1);
    PropertyDrawerRegistry drawers;
    drawers.RegisterDefaults();
    EditorCommandRegistry commands;
    const EditorContext context{f.session, f.session.Selection(), f.properties, f.types, drawers, text, commands, f.edits};
    const std::array targets{id};
    const auto* health = f.types.Find("Game.Health");
    assert(ComponentBrowserModel::Availability(context, *health, targets).enabled);
    assert(f.edits.AddComponent(*health));
    assert(!ComponentBrowserModel::Availability(context, *health, targets).enabled);
    assert(f.properties.Undo());
    const auto* script = f.types.Find("ManagedScript:demo.controller");
    assert(script && script->addInstance && ComponentBrowserModel::Availability(context, *script, targets).enabled);
    assert(f.edits.AddScriptInstance(*script));
    assert(f.edits.AddScriptInstance(*script));
    assert(script->instances(f.session.GetSelectedEntity()).size() == 2);
    assert(f.properties.Undo() && script->instances(f.session.GetSelectedEntity()).size() == 1);
    assert(f.properties.Undo() && !script->contains(f.session.GetSelectedEntity()));
    assert(!ComponentBrowserModel::Availability(context, *f.types.Find("Transform"), targets).enabled);

    // User設定を実ユーザーの保存先から分離し、カードのお気に入り保存を実行する。
    wchar_t* previous = nullptr;
    size_t previousLength = 0;
    _wdupenv_s(&previous, &previousLength, L"LOCALAPPDATA");
    const auto userDirectory = (directory / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())).wstring();
    _wputenv_s(L"LOCALAPPDATA", userDirectory.c_str());
    EditorSettingsStore settings;
    settings.Load(std::filesystem::absolute("Project"));
    settings.user.componentFavorites.clear();
    PropertyCategoryRegistry categories;
    RegisterPropertyPages(categories);
    DetailsPanel panel(categories, &settings);
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = {1200, 900};
    io.DeltaTime = 1.f / 60;
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(1);
    UI::SetEditorIcon("details.Camera", ImTextureRef(static_cast<ImTextureID>(91)));
    const auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({700, 750});
        ImGui::Begin("Component Browser Details");
        panel.Draw(context);
        ImGui::End();
        ImGui::Render();
    };
    const auto open = [&] {
        auto* window = ImGui::FindWindowByName("Component Browser Details");
        const auto entitySeed = ImHashStr((std::to_string(id.high) + ":" + std::to_string(id.low)).c_str(), 0, window->ID);
        ImGui::FocusWindow(window);
        ImGui::ActivateItemByID(ImHashStr("###AddComponentButton", 0, entitySeed));
        frame();
        frame();
        assert(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId));
    };
    const auto child = [&](const char* name) -> ImGuiWindow* {
        for(auto* window : ImGui::GetCurrentContext()->Windows) {
            if(window->Active && window->ParentWindow && std::strstr(window->Name, name)) {
                return window;
            }
        }
        assert(false);
        return nullptr;
    };
    const auto cardAction = [&](const char* type, const char* action) {
        auto* cards = child("ComponentCards");
        ImGui::FocusWindow(cards);
        const auto typeSeed = ImHashStr(type, 0, cards->ID);
        ImGui::ActivateItemByID(ImHashStr(action, 0, typeSeed));
        frame();
        frame();
    };
    frame();
    frame();
    open();
    io.AddInputCharactersUTF8("camera");
    frame();
    frame();
    auto* cards = child("ComponentCards");
    bool iconVisible = false;
    for(const auto& command : cards->DrawList->CmdBuffer) {
        iconVisible |= command.GetTexID() == 91 && command.ElemCount > 0;
    }
    assert(iconVisible);
    cardAction("Camera", "###Favorite");
    assert(settings.user.componentFavorites == std::vector<std::string>{"Camera"});
    EditorSettingsStore restored;
    restored.Load(std::filesystem::absolute("Project"));
    assert(restored.user.componentFavorites == settings.user.componentFavorites);
    auto* navigation = child("ComponentCategories");
    ImGui::FocusWindow(navigation);
    ImGui::ActivateItemByID(ImHashStr("###Favorites", 0, navigation->ID));
    frame();
    frame();
    cardAction("Camera", "##ComponentCard");
    assert(f.session.GetSelectedEntity().HasComponent<CameraComponent>());
    assert(!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId));
    open();
    io.AddInputCharactersUTF8("camera");
    frame();
    frame();
    cardAction("Camera", "##ComponentCard");
    assert(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId));
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    frame();
    assert(!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId));
    assert(f.properties.Undo() && !f.session.GetSelectedEntity().HasComponent<CameraComponent>());
    assert(!f.properties.CanUndo());
    assert(f.properties.Redo() && f.session.GetSelectedEntity().HasComponent<CameraComponent>());
    // 登録済みEngine型もDetailsの共通解除メニューから外せる。
    auto* details = ImGui::FindWindowByName("Component Browser Details");
    ImGui::FocusWindow(details);
    const auto entitySeed = ImHashStr((std::to_string(id.high) + ":" + std::to_string(id.low)).c_str(), 0, details->ID);
    ImGui::ActivateItemByID(ImHashStr("###RemoveComponentButton", 0, entitySeed));
    frame();
    frame();
    auto* removal = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
    assert(removal);
    ImGui::FocusWindow(removal);
    const int firstEntry = 0;
    const auto cameraSeed = ImHashData(&firstEntry, sizeof(firstEntry), ImHashStr("Camera", 0, removal->ID));
    ImGui::ActivateItemByID(ImHashStr("###RemoveEntry", 0, cameraSeed));
    frame();
    frame();
    assert(!f.session.GetSelectedEntity().HasComponent<CameraComponent>());
    assert(f.properties.Undo() && f.session.GetSelectedEntity().HasComponent<CameraComponent>());
    assert(f.properties.Redo() && !f.session.GetSelectedEntity().HasComponent<CameraComponent>());
    assert(!f.edits.RemoveComponent(*f.types.Find("Transform")));
    assert(!f.edits.RemoveComponent(*f.types.Find("Name")));
    // C#はインスタンス番号を解決して一つだけ外し、元の保存IDと値をUndoで復元する。
    assert(f.edits.AddScriptInstance(*script));
    assert(f.edits.AddScriptInstance(*script));
    const auto slots = script->instances(f.session.GetSelectedEntity());
    assert(slots.size() == 2);
    assert(f.edits.RemoveComponent(*script, size_t{1}));
    assert(script->instances(f.session.GetSelectedEntity()) == std::vector<PersistentId>{slots[0]});
    assert(f.properties.Undo() && script->instances(f.session.GetSelectedEntity()) == slots);
    assert(f.properties.Redo() && script->instances(f.session.GetSelectedEntity()).size() == 1);
    assert(f.edits.RemoveComponent(*script) && !script->contains(f.session.GetSelectedEntity()));
    assert(f.edits.AddComponent(*health));
    assert(f.edits.RemoveComponent(*health) && !health->contains(f.session.GetSelectedEntity()));
    assert(f.properties.Undo() && health->contains(f.session.GetSelectedEntity()));
    open();
    const auto other = Id(f.session.GetLevel()->CreateEntity("Other selection"));
    f.session.Select(other);
    frame();
    assert(!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId));
    UI::ClearEditorIcons();
    ImGui::DestroyContext();
    _wputenv_s(L"LOCALAPPDATA", previous ? previous : L"");
    std::free(previous);
    std::cout << "PASS: Component Browser metadata/search/categories/cache/language, Engine/Native/C# types, instances, cards/icons/favorites, add/remove/undo, protected core types, disabled duplicates, Escape and selection lifetime\n";
}
