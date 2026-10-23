#include "Editor/Services/EntityConsoleCommands.h"
#include "Editor/Gui/Panels/UtilityPanels.h"

namespace {
void TerminalSuggestionTests() {
    Fixture f;
    ConsoleService console;
    int alpha = 0, beta = 0;
    console.Register("alpha", [&](auto) { ++alpha; return ConsoleService::Output{}; });
    console.Register("beta", [&](auto) { ++beta; return ConsoleService::Output{}; });
    TerminalPanel terminal(console);
    PropertyDrawerRegistry drawers;
    LocalizationService text;
    EditorCommandRegistry commands;
    const EditorContext context{f.session, f.session.Selection(), f.properties, f.types, drawers, text, commands, f.edits};
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = {800,600}; io.IniFilename = nullptr; io.DeltaTime = 1.0f/60;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGuiID inputId = 0;
    const auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({700,500});
        ImGui::Begin("Terminal test", nullptr, ImGuiWindowFlags_NoSavedSettings);
        inputId = ImGui::GetID(text.Label("console.input", "input").c_str());
        terminal.Draw(context);
        ImGui::End(); ImGui::Render();
    };
    const auto key = [&](ImGuiKey value) {
        io.AddKeyEvent(value, true); frame();
        io.AddKeyEvent(value, false); frame(); frame(); frame();
    };
    frame(); ImGui::ActivateItemByID(inputId); frame(); frame();
    io.AddInputCharactersUTF8("/"); frame(); frame();
    auto* suggestions = ImGui::FindWindowByName("###TerminalSuggestions");
    assert(suggestions && suggestions->Active);
    key(ImGuiKey_DownArrow); key(ImGuiKey_Tab);
    assert(std::string(ImGui::GetInputTextState(inputId)->GetText()) == "/beta ");
    assert(!suggestions->Active && alpha == 0 && beta == 0);
    key(ImGuiKey_Enter);
    assert(beta == 1 && console.History().back() == "/beta ");
    io.AddInputCharactersUTF8("/alp"); frame(); frame();
    assert(suggestions->Active);
    key(ImGuiKey_Enter);
    assert(alpha == 0);
    assert(std::string(ImGui::GetInputTextState(inputId)->GetText()) == "/alpha ");
    key(ImGuiKey_Enter);
    assert(alpha == 1);
    io.AddInputCharactersUTF8("/"); frame(); frame();
    assert(suggestions->Active);
    const ImVec2 secondRow{suggestions->Pos.x + 20,
        suggestions->Pos.y + ImGui::GetStyle().WindowPadding.y + ImGui::GetTextLineHeightWithSpacing() * 1.5f};
    io.AddMousePosEvent(secondRow.x, secondRow.y); frame();
    io.AddMouseButtonEvent(0, true); frame();
    io.AddMouseButtonEvent(0, false); frame(); frame(); frame(); frame();
    assert(std::string(ImGui::GetInputTextState(inputId)->GetText()) == "/beta ");
    key(ImGuiKey_Enter); assert(beta == 2);
    // With no slash suggestions, Up keeps its command-history behavior.
    key(ImGuiKey_UpArrow);
    assert(std::string(ImGui::GetInputTextState(inputId)->GetText()) == "/beta ");
    ImGui::DestroyContext();
}

void EntityConsoleTests() {
    Fixture f;
    ConsoleService console;
    bool enabled = true;
    EntityConsoleCommands{f.session, f.properties, f.edits, f.types,
        [&] { return enabled && !f.session.IsPlaying(); }}.Register(console);
    const auto run = [&](const char* line) { return console.Execute(line).text; };
    assert(console.Complete("entity.").size() == 9);
    assert(console.Complete("/") == console.Complete(""));
    assert(console.Complete("/entity.") == console.Complete("entity."));
    assert(console.Complete("/entity.set @selected").empty());
    assert(run("/").empty());
    assert(run(" /entity.list") == "No matching entities.");
    assert(run("/unknown") == "console.unknown");
    assert(run("entity.list") == "No matching entities.");
    assert(run("entity.create Entity \"Console Entity\"").starts_with("Created #"));
    const auto first = f.session.GetSelection();
    assert(f.session.GetSelectedEntity().GetComponent<NameComponent>()->name == "Console Entity");
    assert(f.properties.Undo()); assert(!f.session.GetLevel()->FindEntity(first).IsAlive());
    assert(f.properties.Redo()); assert(f.session.GetLevel()->FindEntity(first).IsAlive());
    assert(run("entity.list Console").find("\"Console Entity\" *") != std::string::npos);
    const auto listing = run("entity.list");
    const auto id = listing.substr(0,33);
    assert(console.Execute("entity.select " + id).text.starts_with("Selected #"));
    assert(run("entity.info @selected").find("Transform.translation = 0 0 0") != std::string::npos);
    assert(run("entity.set @selected Transform.translation 1 2 3") == "OK");
    assert(f.session.GetSelectedEntity().GetComponent<TransformComponent>()->translation.x == 1);
    assert(f.properties.Undo());
    assert(f.session.GetSelectedEntity().GetComponent<TransformComponent>()->translation.x == 0);
    assert(f.properties.Redo());
    const auto revision = f.properties.Revision();
    for(const char* line : {"entity.set @selected Transform.translation 9 8", "entity.set @selected Transform.translation 9x 8 7",
        "entity.set @selected Transform.translation nan 8 7", "entity.set @selected Transform.rotation 0 0 0 0",
        "entity.set @selected Transform.scale 0 1 1", "entity.rename @selected \"\"", "entity.select #bad",
        "entity.rename @selected \"unfinished", "entity.rename @selected \"name\"suffix", "entity.delete missing"}) {
        assert(run(line) != "OK");
        assert(f.properties.Revision() == revision);
        assert(f.session.GetSelection() == first);
    }
    assert(run("entity.rename @selected \"Renamed Entity\"") == "OK");
    assert(run("entity.select \"Renamed Entity\"").starts_with("Selected #"));
    assert(run("entity.add @selected Camera") == "OK");
    assert(run("entity.set @selected Camera.primary false") == "OK");
    assert(!f.session.GetSelectedEntity().GetComponent<CameraComponent>()->primary);
    assert(run("entity.create Entity Parent").starts_with("Created #"));
    const auto parent = f.session.GetSelection();
    assert(run("entity.parent \"Renamed Entity\" Parent") == "OK");
    assert(run("entity.parent Parent \"Renamed Entity\"") != "OK");
    assert(run("entity.parent \"Renamed Entity\" @root") == "OK");
    assert(run("entity.create Entity Parent").starts_with("Created #"));
    assert(run("entity.delete Parent").starts_with("Ambiguous"));
    assert(console.Execute("entity.delete " + id).text == "OK");
    assert(!f.session.GetLevel()->FindEntity(first).IsAlive());
    assert(f.session.GetLevel()->FindEntity(parent).IsAlive());
    assert(f.properties.Undo()); assert(f.session.GetLevel()->FindEntity(first).IsAlive());
    enabled = false;
    const auto count = f.session.GetLevel()->GetScene()->GetEntityCount();
    assert(run("entity.create Entity") == "console.unavailable");
    assert(console.Execute("entity.delete " + id).text == "console.unavailable");
    assert(f.session.GetLevel()->GetScene()->GetEntityCount() == count);
    assert(run("entity.list").find(id) != std::string::npos);
    enabled = true;
    assert(f.session.StartPlay());
    assert(run("entity.set @selected Name.name Changed") == "console.unavailable");
    f.session.StopPlay();
    assert(f.session.Create()); f.properties.Reset(f.session.GetLevel());
    assert(run("entity.list") == "No matching entities.");
    assert(console.Execute("entity.info " + id).text == "Entity not found.");
}
}
