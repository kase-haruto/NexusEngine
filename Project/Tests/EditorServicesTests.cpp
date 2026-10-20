#include "Editor/Services/EditorDocumentService.h"
#include "Editor/Services/SceneEditService.h"
#include "Editor/Services/EditorCommandRegistry.h"
#include "Editor/Gui/Panels/PanelRegistry.h"
#include "Editor/Gui/PropertyDrawerRegistry.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Gui/ViewportTools.h"
#include "Runtime/Rendering/RenderSceneExtractor.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/DirectionalLightComponent.h"
#include "Editor/Core/EditorWorkspace.h"
#include "Editor/Gui/Panels/OutlinerPanel.h"
#include "Editor/ImGui/ImGuiStyle.h"
#include "ThirdParty/imgui/imgui_internal.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include "ThirdParty/imgui/imgui.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <cstring>
#include <fstream>

using namespace NexusEngine;
namespace {
struct CustomComponent { std::string value = "preserved"; };
struct MoveOnlyComponent { std::unique_ptr<int> value = std::make_unique<int>(7); };
PersistentId Id(Entity entity) { return entity.GetComponent<PersistentIdComponent>()->value; }
struct Fixture {
    EditorSession session;
    TypeRegistry types;
    PropertyEditService properties{types};
    SceneEditService edits{session, properties};
    EditorDocumentService documents{session, properties};
    Fixture() { assert(session.Create()); RegisterSceneTypes(types); properties.Reset(session.GetLevel()); }
};
void PropertyTransactions() {
    Fixture f;
    auto a = f.session.GetLevel()->CreateEntity("A"), b = f.session.GetLevel()->CreateEntity("B");
    const PropertyHandle position{Id(a), "Transform", "translation"};
    const std::vector<PropertyHandle> handles{position, {Id(b), "Transform", "translation"}};
    f.properties.Begin(handles);
    assert(f.properties.Preview(handles, std::array<float, 3>{1, 2, 3}));
    assert(f.properties.Preview(handles, std::array<float, 3>{4, 5, 6}));
    assert(!f.properties.Preview(handles, std::array<float, 3>{std::numeric_limits<float>::infinity(), 0, 0}));
    f.properties.Commit(); assert(f.properties.IsDirty());
    assert(f.properties.Undo());
    for(const auto& handle : handles) assert((std::get<std::array<float, 3>>(*f.properties.Read(handle))[0] == 0));
    assert(!f.properties.IsDirty()); assert(f.properties.Redo());
    f.properties.MarkSaved(); assert(!f.properties.IsDirty());
    assert(f.properties.Preview(position, std::array<float, 3>{9, 0, 0}));
    assert(f.properties.Cancel()); assert(!f.properties.IsDirty());
    assert((std::get<std::array<float, 3>>(*f.properties.Read(position))[0] == 4));
    assert(f.properties.Undo()); assert(f.properties.IsDirty());
    assert(f.properties.Preview(position, std::array<float, 3>{2, 0, 0})); f.properties.Commit();
    assert(!f.properties.CanRedo() && f.properties.IsDirty());

    // 後の対象だけが拒否するsetterでも、先行対象を更新したまま残さない。
    const auto rejected = Id(b);
    TypeDescriptor limited{"Limited", "test", [](Entity) { return true; }, {}};
    limited.properties.push_back({"name", "test", PropertyKind::String,
        [](Entity entity) -> std::optional<PropertyValue> { return entity.GetComponent<NameComponent>()->name; },
        [rejected](Entity entity, const PropertyValue& value) {
            if(Id(entity) == rejected) return false;
            entity.GetComponent<NameComponent>()->name = std::get<std::string>(value); return true;
        }, {}});
    f.types.Register(std::move(limited));
    const std::vector<PropertyHandle> names{{Id(a), "Limited", "name"}, {Id(b), "Limited", "name"}};
    assert(!f.properties.Preview(names, std::string("rejected")));
    assert(a.GetComponent<NameComponent>()->name == "A"); f.properties.Commit();
    const std::vector<PropertyHandle> heterogeneous{{Id(a),"Name","name"}, position};
    const std::vector<PropertyValue> values{std::string("Renamed"),std::array<float,3>{7,8,9}};
    assert(f.properties.PreviewValues(heterogeneous,values)); f.properties.Commit();
    assert(a.GetComponent<NameComponent>()->name == "Renamed");
    assert(f.properties.Undo()); assert(a.GetComponent<NameComponent>()->name == "A");
    assert((std::get<std::array<float,3>>(*f.properties.Read(position))[0] == 2));
}
void StructuralHistory() {
    Fixture f;
    assert(f.edits.Create("Entity")); const auto parent = f.session.GetSelection();
    auto entity = f.session.GetLevel()->FindEntity(parent);
    assert(entity.AddComponent<CustomComponent>());
    assert(f.edits.Create("Entity")); const auto child = f.session.GetSelection();
    assert(f.edits.Reparent(child, parent));
    assert(!f.edits.Reparent(parent, child));
    assert(f.session.GetLevel()->Update(0));
    f.properties.MarkSaved();
    f.session.Select(parent); assert(f.edits.DeleteSelection());
    assert(!f.session.GetLevel()->FindEntity(parent).IsAlive());
    assert(!f.session.GetLevel()->FindEntity(child).GetComponent<HierarchyComponent>()->parent.IsValid());
    assert(f.properties.Undo()); assert(!f.properties.IsDirty());
    auto restored = f.session.GetLevel()->FindEntity(parent);
    assert(restored.GetComponent<CustomComponent>()->value == "preserved");
    assert(f.session.GetLevel()->FindEntity(child).GetComponent<HierarchyComponent>()->parent == restored.GetId());
    assert(f.session.GetSelection() == parent);
    assert(f.properties.Redo()); assert(f.properties.Undo());
    assert(f.properties.Undo()); // 親子変更を取り消す
    auto* hierarchy = f.session.GetLevel()->FindEntity(child).GetComponent<HierarchyComponent>();
    assert(!hierarchy || !hierarchy->parent.IsValid());
    assert(f.properties.Undo()); assert(!f.session.GetLevel()->FindEntity(child).IsAlive());
    assert(f.properties.Redo()); assert(f.session.GetLevel()->FindEntity(child).IsAlive());
    auto current = f.session.GetLevel()->FindEntity(parent);
    assert(current.AddComponent<MoveOnlyComponent>());
    f.session.Select(parent);
    assert(!f.edits.DeleteSelection()); assert(f.session.GetLevel()->FindEntity(parent).IsAlive());
}
void DocumentsAndPlay() {
    Fixture f;
    const auto folder = std::filesystem::path("Generated/EditorTests");
    std::filesystem::create_directories(folder);
    const auto file = folder / "SavedLevel.json";
    assert(f.edits.Create("Entity")); const auto selected = f.session.GetSelection();
    f.documents.Request(EditorDocumentService::Action::New);
    assert(f.documents.ConfirmationRequired()); f.documents.Cancel();
    assert(f.session.GetLevel()->FindEntity(selected).IsAlive());
    assert(f.session.StartPlay());
    auto* edit = f.session.GetLevel(); auto* play = f.session.GetPreviewLevel();
    assert(edit != play);
    play->FindEntity(selected).GetComponent<TransformComponent>()->translation.x = 99;
    assert(edit->FindEntity(selected).GetComponent<TransformComponent>()->translation.x == 0);
    assert(!f.edits.DeleteSelection()); f.session.TogglePause(); assert(f.session.IsPaused());
    f.session.StopPlay(); assert(f.session.GetPreviewLevel() == edit);
    f.documents.Request(EditorDocumentService::Action::Exit);
    assert(f.documents.ConfirmationRequired());
    assert(!f.documents.Save()); assert(f.documents.NeedsSaveAs());
    assert(!f.documents.Save(folder)); // directoryへの保存失敗でも終了しない
    assert(!f.documents.ExitRequested() && f.documents.IsDirty());
    f.documents.Cancel();
    assert(f.documents.Save(file)); assert(!f.documents.IsDirty());
    f.documents.Request(EditorDocumentService::Action::Open, folder / "Missing.json");
    assert(!f.documents.Error().empty()); assert(f.session.GetLevel()->FindEntity(selected).IsAlive());
    assert(f.edits.DeleteSelection());
    f.documents.Request(EditorDocumentService::Action::Open, file);
    assert(f.documents.ConfirmationRequired()); f.documents.Discard();
    assert(f.session.GetLevel()->FindEntity(selected).IsAlive()); assert(!f.documents.IsDirty());
    f.documents.Request(EditorDocumentService::Action::Exit); assert(f.documents.ExitRequested());
    std::filesystem::remove(file);
}
void HistoryBoundsAndCommands() {
    EditorHistory history;
    int value = 0;
    for(int i = 0; i < 300; ++i) {
        ++value; history.Record({"test", [&] { --value; return true; }, [&] { ++value; return true; }});
    }
    int count = 0; while(history.Undo()) ++count;
    assert(count == 256 && value == 44 && history.IsDirty());
    history.MarkSaved(); assert(history.Redo()); assert(history.IsDirty());
    assert(history.Undo() && !history.IsDirty());
    history.Record({"failure", [] { return false; }, [] { return false; }});
    auto revision = history.Revision(); assert(!history.Undo() && history.Revision() == revision);
    EditorCommandRegistry commands;
    bool enabled = false;
    assert(commands.Register("test", {[&] { ++value; }, [&] { return enabled; }}));
    assert(!commands.Register("test", {[]{}, {}}));
    assert(commands.Bind("test", 1, true, false, false, "Ctrl+Test"));
    assert(commands.Register("other", {[]{}, {}}));
    assert(!commands.Bind("other", 1, true, false, false, "conflict"));
    const auto before = value;
    commands.Dispatch(true, false, false, [](int) { return true; }); assert(value == before);
    enabled = true; commands.Dispatch(false, false, false, [](int) { return true; }); assert(value == before);
    commands.Dispatch(true, false, false, [](int) { return true; }); assert(value == before + 1);
}
class TestPanel final : public IEditorPanel {
public:
    std::string_view Id() const noexcept override { return "Test"; }
    std::string_view TitleKey() const noexcept override { return "panel.test"; }
    void Draw(const EditorContext&) override { ++draws; registry->Unregister("Test"); }
    PanelRegistry* registry = nullptr;
    static inline int draws = 0;
};
void LazyPanelLifecycle() {
    Fixture f; PanelRegistry panels; PropertyDrawerRegistry drawers; LocalizationService text; EditorCommandRegistry commands;
    int created = 0;
    assert(panels.RegisterFactory("Test", "panel.test", [&] {
        ++created; auto panel = std::make_unique<TestPanel>(); panel->registry = &panels; return panel;
    }, false));
    assert(!panels.RegisterFactory("Test", "duplicate", [] { return std::make_unique<TestPanel>(); }));
    assert(created == 0);
    ImGui::CreateContext(); auto& io = ImGui::GetIO(); io.DisplaySize = {800, 600}; io.IniFilename = nullptr;
    unsigned char* pixels; int width, height; io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    EditorContext context{f.session, f.session.Selection(), f.properties, f.types, drawers, text, commands, f.edits};
    ImGui::NewFrame(); panels.Draw(context); ImGui::Render(); assert(created == 0);
    panels.Open("Test");
    ImGui::NewFrame(); panels.Draw(context); ImGui::Render();
    assert(created == 1 && TestPanel::draws == 1 && panels.Entries().empty());
    ImGui::DestroyContext();
}
void ViewportPickingAndValidation() {
    Fixture f;
    assert(f.edits.Create("Model")); const auto selected = f.session.GetSelection();
    auto entity = f.session.GetSelectedEntity();
    ModelAssetData model; model.nodes.resize(1); model.nodes[0].meshIndices = {0}; model.meshes.resize(1);
    auto& mesh = model.meshes[0]; mesh.vertexStride = 12;
    const float vertices[]{-2,0,0, 2,0,0, 0,4,0}; const uint16_t indices[]{0,1,2};
    mesh.vertexData.resize(sizeof(vertices)); std::memcpy(mesh.vertexData.data(),vertices,sizeof(vertices));
    mesh.indexData.resize(sizeof(indices)); std::memcpy(mesh.indexData.data(),indices,sizeof(indices));
    ModelInstance pose; assert(pose.Initialize(model));
    assert(f.session.GetLevel()->Update(0));
    RenderScene frame; assert(RenderSceneExtractor{}.ExtractInto(*f.session.GetLevel()->GetScene(),1,frame));
    ViewportTools tools{f.session,f.properties}; tools.SetModel(model,pose); tools.Prepare(frame,1);
    f.session.Selection().Clear(); assert(tools.Pick(0.5f,0.5f,false)); assert(f.session.GetSelection() == selected);
    assert(tools.Pick(0.5f,0.5f,true)); assert(f.session.Selection().All().empty());
    assert(!tools.Pick(0,0,false)); assert(!tools.Pick(-1,0,false));
    assert(!f.properties.Preview({selected,"Transform","rotation"},std::array<float,4>{0,0,0,0}));
    assert(f.properties.Preview({selected,"Transform","rotation"},std::array<float,4>{0,2,0,2})); f.properties.Commit();
    assert(std::abs(f.session.GetLevel()->FindEntity(selected).GetComponent<TransformComponent>()->rotation.LengthSquared()-1) < 1e-5f);
    assert(f.properties.Undo());
    assert(f.edits.Create("Camera")); const auto firstCamera = f.session.GetSelection();
    assert(f.session.GetSelectedEntity().GetComponent<CameraComponent>()->primary);
    assert(f.edits.Create("Camera")); const auto secondCamera = f.session.GetSelection();
    assert(!f.session.GetSelectedEntity().GetComponent<CameraComponent>()->primary);
    assert(!f.properties.Preview({secondCamera,"Camera","primary"},true));
    assert(!f.properties.Preview({firstCamera,"Camera","near"},10000.0f));
    assert(f.properties.Preview({firstCamera,"Camera","near"},100.0f)); f.properties.Commit();
    const std::vector<PropertyHandle> clips{{firstCamera,"Camera","near"},{firstCamera,"Camera","far"}};
    const std::vector<PropertyValue> clipping{1.0f,50.0f};
    assert(f.properties.PreviewValues(clips,clipping)); f.properties.Commit();
    assert(f.properties.Undo());
    auto* camera = f.session.GetLevel()->FindEntity(firstCamera).GetComponent<CameraComponent>();
    assert(camera->nearClip == 100 && camera->farClip == 1000); assert(f.properties.Redo());
    assert(f.edits.Create("DirectionalLight")); assert(f.session.GetSelectedEntity().GetComponent<DirectionalLightComponent>()->enabled);
    assert(f.edits.Create("DirectionalLight")); assert(!f.session.GetSelectedEntity().GetComponent<DirectionalLightComponent>()->enabled);
    assert(f.session.GetLevel()->Update(0));
    assert(RenderSceneExtractor{}.ExtractInto(*f.session.GetLevel()->GetScene(),1,frame));
}
void CacheAndWorkspaceIntegration() {
    BufferedLogBuffer buffer; uint64_t version = 0;
    assert(buffer.SnapshotIfChanged(version)); assert(!buffer.SnapshotIfChanged(version));
    LogRecord record{LogLevel::Info,"EditorTest","line1\nline2",std::chrono::system_clock::now(),std::source_location::current()};
    for(int i = 0; i < 2050; ++i) buffer.Append(record);
    auto snapshot = buffer.SnapshotIfChanged(version); assert(snapshot && snapshot->size() == 2048);
    assert(!buffer.SnapshotIfChanged(version)); buffer.Clear(); assert(buffer.SnapshotIfChanged(version)->empty());
    const auto temporary = std::filesystem::absolute("Generated/EditorTests") / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(temporary);
    AssetCatalog catalog(temporary);
    auto empty = catalog.Snapshot(temporary); assert(empty->empty());
    std::ofstream(temporary / "one.txt") << "test";
    assert(catalog.Snapshot(temporary) == empty); catalog.Refresh();
    auto changed = catalog.Snapshot(temporary); assert(changed->size() == 1 && empty->empty());
    std::filesystem::remove(temporary / "one.txt"); std::filesystem::remove(temporary);

    // 個人設定はテスト専用ディレクトリへ隔離する。実利用者の設定は変更しない。
    wchar_t* previous = nullptr; size_t length = 0; _wdupenv_s(&previous,&length,L"LOCALAPPDATA");
    const auto user = std::filesystem::absolute("Generated/EditorTests/User").wstring(); _wputenv_s(L"LOCALAPPDATA",user.c_str());
    Fixture f;
    for(int i = 0; i < 10000; ++i) static_cast<void>(f.session.GetLevel()->CreateEntity("Entity " + std::to_string(i)));
    ImGui::CreateContext(); auto& io = ImGui::GetIO(); io.DisplaySize = {1600,1000}; io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    {
        EditorWorkspace workspace(f.session); workspace.Initialize(std::filesystem::absolute("Project"));
        class TestTextures final : public UI::ITextureProvider {
        public:
            std::optional<UI::ResolvedTexture> Resolve(UI::TextureHandle) const noexcept override { return UI::ResolvedTexture{1,640,480}; }
        } textures;
        assert(workspace.Viewport().AttachLocalPreview(textures,{1},[](uint32_t,uint32_t) { return true; }));
        RenderScene imageFrame;
        workspace.ViewportEditor().Prepare(imageFrame,1);
        const auto frame = [&] { workspace.PrepareFrame(); ImGui::NewFrame(); workspace.Draw(); ImGui::Render(); };
        for(int i = 0; i < 4; ++i) frame();
        auto* outlinerWindow = ImGui::FindWindowByName("###Outliner"); assert(outlinerWindow && outlinerWindow->Active);
        OutlinerPanel* outliner = nullptr;
        for(auto& entry : workspace.Panels().Entries()) if(entry.Id() == "Outliner") outliner = dynamic_cast<OutlinerPanel*>(entry.panel.get());
        assert(outliner); const auto builds = outliner->HierarchyBuilds();
        const auto started = std::chrono::steady_clock::now();
        for(int i = 0; i < 100; ++i) frame();
        const auto ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()/100;
        assert(outliner->HierarchyBuilds() == builds);
        assert(ImGui::GetDrawData()->TotalVtxCount < 20000);
        std::cout << "10k entities: " << ms << " ms/CPU UI frame, " << ImGui::GetDrawData()->TotalVtxCount << " vertices, no hierarchy rebuild in 100 stable frames\n";
        assert(workspace.Commands().Execute("scene.entity")); frame(); assert(outliner->HierarchyBuilds() > builds);
        assert(workspace.Commands().Execute("edit.undo")); frame(); assert(f.session.GetLevel()->GetScene()->GetEntityCount() == 10000);
        assert(workspace.Commands().Execute("edit.redo")); frame();
        assert(workspace.Commands().Execute("file.new")); frame(); frame();
        auto* modal = ImGui::FindWindowByName("###Unsaved"); assert(modal && modal->Active);
        ImGui::ActivateItemByID(modal->GetID("###cancel")); frame(); frame();
        assert(f.session.GetLevel()->GetScene()->GetEntityCount() == 10001);
        assert(workspace.Commands().Execute("play.start")); frame(); assert(f.session.IsPlaying());
        assert(!workspace.Commands().Execute("file.save"));
        assert(workspace.Commands().Execute("play.pause")); assert(f.session.IsPaused());
        assert(workspace.Commands().Execute("play.stop")); frame(); assert(!f.session.IsPlaying());
        const auto basePadding = ImGui::GetStyle().FramePadding;
        UI::ApplyEditorStyle({true,18,2}); const auto scaled = ImGui::GetStyle().FramePadding;
        UI::ApplyEditorStyle({true,18,2}); assert(ImGui::GetStyle().FramePadding.x == scaled.x);
        assert(scaled.x >= basePadding.x);
        const auto theme = UI::CurrentEditorTheme(); assert(theme.surface.red == ImGui::GetStyle().Colors[ImGuiCol_FrameBg].x);
        UI::ApplyEditorStyle({false,18,1});
        assert(workspace.Commands().Execute("workspace.Lighting")); frame();
        assert(workspace.Commands().Execute("workspace.General")); frame();
        workspace.Shutdown();
    }
    ImGui::DestroyContext();
    _wputenv_s(L"LOCALAPPDATA",previous ? previous : L""); std::free(previous);
}
}
int main() {
    PropertyTransactions(); StructuralHistory(); DocumentsAndPlay(); HistoryBoundsAndCommands(); LazyPanelLifecycle();
    ViewportPickingAndValidation();
    CacheAndWorkspaceIntegration();
    std::cout << "PASS: property transactions, rollback, structural undo, save protection, play isolation, history bounds, shortcuts, lazy panels, viewport picking, rotation/camera/light validation\n";
}
