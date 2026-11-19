#include "Runtime/Project/ProjectService.h"
void ContentSourceCreationIntegration() {
    const auto parent = std::filesystem::absolute("Generated/ContentSourceCreation");
    std::filesystem::create_directories(parent);
    ProjectDescriptor descriptor;
    descriptor.name = "Browser Sources";
    descriptor.modules = {{"Native", "Native", ProjectModuleLanguage::Cpp, "Source/Native"},
        {"Scripts", "Scripts", ProjectModuleLanguage::CSharp, "Source/Scripts"}};
    const auto project = ProjectService::Create(parent / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()),
        descriptor, [](const ProjectContext& context) -> Result<void> {
            std::filesystem::create_directories(context.Source() / "Native");
            std::filesystem::create_directories(context.Source() / "Scripts");
            Level level;
            auto empty = level.LoadEmpty();
            if(!empty) {
                return empty;
            }
            return level.Save(context.StartupLevel());
        });
    assert(project);
    SourceCreationService creation;
    bool ready = true;
    std::vector<SourceCreationRequest> requests;
    creation.Configure(project->File(), [&] {
        return ready;
    }, [&](const SourceCreationRequest& request) -> Result<void> {
        requests.push_back(request);
        std::ofstream(request.file) << "// generated source fixture";
        creation.Complete(request.file);
        return {};
    });
    const auto managed = creation.Modules(false), native = creation.Modules(true);
    assert(managed && managed->size() == 1 && native && native->size() == 1);
    assert(!creation.Create(managed->front(), "../Escape"));
    ready = false;
    assert(!creation.Create(managed->front(), "Busy"));
    ready = true;
    auto stale = managed->front();
    stale.directory = project->Assets();
    assert(!creation.Create(stale, "Stale"));
    Fixture f;
    AssetCatalog assets(project->Assets()), sources(project->Source());
    std::filesystem::path opened;
    ContentBrowserPanel browser(assets, [&](const auto& file) {
        opened = file;
    }, &sources, &creation);
    LocalizationService text;
    assert(text.Load(std::filesystem::absolute("Project/Resources/Editor/Localization")));
    PropertyDrawerRegistry drawers;
    drawers.RegisterDefaults();
    EditorCommandRegistry commands;
    const EditorContext context{f.session, f.session.Selection(), f.properties, f.types, drawers, text, commands, f.edits};
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = {1200, 900};
    io.DeltaTime = .1f;
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(1);
    const auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({1050, 750});
        ImGui::Begin("Content Source Test");
        browser.Draw(context);
        ImGui::End();
        ImGui::Render();
    };
    frame();
    frame();
    const auto child = [&]() -> ImGuiWindow* {
        for(auto* window : ImGui::GetCurrentContext()->Windows) {
            if(window->Active && window->ParentWindow && std::strstr(window->Name, "asset-list")) {
                return window;
            }
        }
        assert(false);
        return nullptr;
    };
    const auto createFromMenu = [&](bool cpp) {
        auto* list = child();
        const auto point = ImVec2{list->Pos.x + 100, list->Pos.y + list->Size.y - 70};
        io.AddMousePosEvent(point.x, point.y);
        frame();
        io.AddMouseButtonEvent(1, true);
        frame();
        io.AddMouseButtonEvent(1, false);
        frame();
        frame();
        assert(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId));
        auto* menu = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
        assert(menu);
        io.AddMousePosEvent(menu->Pos.x + 30, menu->Pos.y + 12);
        for(int hover = 0; hover < 6; ++hover) {
            frame();
        }
        assert(ImGui::GetCurrentContext()->OpenPopupStack.Size == 2);
        auto* submenu = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
        assert(submenu);
        ImGui::FocusWindow(submenu);
        ImGui::ActivateItemByID(ImHashStr(text.Text(cpp ? "source.create.cpp" : "source.create.csharp").c_str(), 0, submenu->ID));
        frame();
        frame();
        const auto title = text.Label("source.create.title", "CreateSource");
        auto* modal = ImGui::FindWindowByName(title.c_str());
        assert(modal && modal->Active);
        ImGui::FocusWindow(modal);
        ImGui::ActivateItemByID(ImHashStr(text.Label("source.create.confirm", "CreateSourceConfirm").c_str(), 0, modal->ID));
        frame();
        frame();
    };
    createFromMenu(false);
    assert(requests.size() == 1 && requests[0].module == "Scripts" && requests[0].name == "NewScript");
    assert(opened == project->Source() / "Scripts/NewScript.cs" && std::filesystem::exists(opened));
    assert(sources.Current() == opened.parent_path());
    assert(!creation.Create(managed->front(), "NewScript"));
    std::ifstream existing(opened);
    std::string contents{std::istreambuf_iterator<char>(existing), std::istreambuf_iterator<char>()};
    assert(contents == "// generated source fixture");
    createFromMenu(true);
    assert(requests.size() == 2 && requests[1].module == "Native" && requests[1].name == "NewBehaviour");
    assert(opened == project->Source() / "Native/NewBehaviour.cpp");
    ImGui::DestroyContext();
    {
        EditorWorkspace workspace(f.session);
        auto captured = std::make_shared<int>(1);
        std::weak_ptr<int> lifetime = captured;
        workspace.SourceCreation().Configure(project->File(), [captured] {
            return *captured == 1;
        }, {});
        captured.reset();
        assert(!lifetime.expired());
        workspace.ReleaseForReinstancing();
        assert(lifetime.expired());
    }
    std::cout << "PASS: Content Browser right-click C#/C++ creation dialogs, module routing, duplicate/path/busy rejection, source refresh and code editor callback\n";
}
