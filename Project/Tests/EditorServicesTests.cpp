#include "Editor/Services/EditorDocumentService.h"
#include "Editor/Services/SceneEditService.h"
#include "Editor/Services/EditorCommandRegistry.h"
#include "Editor/Gui/Panels/PanelRegistry.h"
#include "Editor/Gui/PropertyDrawerRegistry.h"
#include "Editor/Gui/Widgets/PropertyWidgets.h"
#include "Editor/Services/LocalizationService.h"
#include "Editor/Gui/ViewportTools.h"
#include "Editor/Services/TransformGizmoTransaction.h"
#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/ImGuizmo/ImGuizmo.h"
#include "Runtime/Rendering/RenderSceneExtractor.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "Graphics/Model/ModelAssetLibrary.h"
#include "Graphics/Model/ModelThumbnail.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Scene/Components/DirectionalLightComponent.h"
#include "Editor/Core/EditorWorkspace.h"
#include "Editor/Gui/Panels/OutlinerPanel.h"
#include "Editor/Gui/Panels/DetailsPanel.h"
#include "Editor/Gui/Panels/UtilityPanels.h"
#include "Editor/Gui/Properties/MaterialPage.h"
#include "Editor/Gui/Properties/MaterialSlots.h"
#include "Runtime/Level/LevelSerializer.h"
#include "Editor/ImGui/ImGuiStyle.h"
#include "ThirdParty/imgui/imgui_internal.h"
#include "Runtime/Scene/Components/HierarchyComponent.h"
#include "ThirdParty/imgui/imgui.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <cstring>
#include <fstream>
#include <crtdbg.h>

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
void ComponentAndAssetEditing() {
    Fixture f;
    auto a = f.session.GetLevel()->CreateEntity("A"), b = f.session.GetLevel()->CreateEntity("B");
    const auto first=Id(a), second=Id(b);
    f.session.Selection().Replace(std::vector<PersistentId>{first,second});
    const auto* primitive=f.types.Find("PrimitiveRender");
    assert(primitive && primitive->add && f.edits.AddComponent(*primitive));
    assert(f.session.GetLevel()->FindEntity(first).HasComponent<PrimitiveRenderComponent>());
    assert(f.session.GetLevel()->FindEntity(second).HasComponent<PrimitiveRenderComponent>());
    assert(!f.edits.AddComponent(*primitive));
    assert(f.properties.Undo());
    assert(!f.session.GetLevel()->FindEntity(first).HasComponent<PrimitiveRenderComponent>());
    assert(f.properties.Redo());
    assert(f.edits.AddComponent(*f.types.Find("Camera")));
    assert(f.edits.AddComponent(*f.types.Find("DirectionalLight")));
    assert(f.session.GetLevel()->Update(0));
    assert(RenderSceneExtractor{}.Extract(*f.session.GetLevel()->GetScene(),1));
    assert(f.session.StartPlay()); assert(!f.edits.AddComponent(*primitive)); f.session.StopPlay();
    const std::vector<PropertyHandle> handles{{first,"PrimitiveRender","meshAsset"},{first,"PrimitiveRender","nodeIndex"},
        {first,"PrimitiveRender","baseColorTexture"}};
    const std::vector<PropertyValue> values{std::string("Models/テスト mesh.obj"),-1,std::string("Textures/test image.bmp")};
    assert(f.properties.PreviewValues(handles,values)); f.properties.Commit();
    const auto folder=std::filesystem::path("Generated/EditorTests/Asset Editing");
    std::filesystem::create_directories(folder);
    const auto file=folder/"Level.json";
    assert(f.documents.Save(file));
    assert(f.properties.Undo());
    assert(std::get<std::string>(*f.properties.Read(handles[0])).empty());
    assert(f.properties.Redo());
    assert(f.session.Open(file));
    const auto* saved=f.session.GetLevel()->FindEntity(first).GetComponent<PrimitiveRenderComponent>();
    assert(saved && saved->meshAsset==std::get<std::string>(values[0]) && saved->baseColorTexture==std::get<std::string>(values[2]));

    const auto mesh=folder/"triangle.obj";
    { std::ofstream output(mesh); output << "v -1 -1 0\nv 0 1 0\nv 1 -1 0\nf 1 2 3\n"; }
    ModelAssetData empty;
    ModelAssetLibrary library(folder,empty);
    assert(!library.Reference(folder/"../outside.obj"));
    auto reference=library.Reference(std::filesystem::absolute(mesh)); assert(reference && *reference=="triangle.obj");
    auto model=library.Model(*reference); assert(model && !(*model)->meshes.empty());
    auto range=library.Resolve(*reference,""); assert(range && range->end>range->begin);
    const auto revision=library.Revision();
    auto again=library.Resolve(*reference,""); assert(again && again->begin==range->begin && library.Revision()==revision);
    ModelInstance pose; assert(pose.Initialize(library.Snapshot()));
    assert(!library.Resolve(*reference,"missing.bmp")); assert(library.Revision()==revision);
    auto image=CreateModelThumbnail(**model); assert(image && image->rgbaPixels.size()==160*160*4);
    bool colored=false;
    for(size_t pixel=0;pixel<160*160;++pixel) colored |= image->rgbaPixels[pixel*4]>43;
    assert(colored);
    std::vector<uint8_t> bitmap(70);
    bitmap[0]='B'; bitmap[1]='M'; bitmap[2]=70; bitmap[10]=54; bitmap[14]=40;
    bitmap[18]=2; bitmap[22]=2; bitmap[26]=1; bitmap[28]=24; bitmap[34]=16;
    bitmap[56]=255; bitmap[59]=255; bitmap[64]=255; bitmap[67]=255;
    const auto texture=folder/"red.bmp";
    { std::ofstream output(texture,std::ios::binary); output.write(reinterpret_cast<const char*>(bitmap.data()),static_cast<std::streamsize>(bitmap.size())); }
    auto textured=library.Resolve(*reference,"red.bmp"); assert(textured && textured->begin>=range->end);
    assert(textured->materialBegin == range->materialBegin + (*model)->materials.size());
    assert(library.Snapshot().materials.back().baseColorImageData==bitmap);
    pose.Shutdown(); assert(pose.Initialize(library.Snapshot()));
    auto bytes=library.Texture("red.bmp"); assert(bytes);
    auto decoded=WicImageDecoder{}.Decode(**bytes); assert(decoded && decoded->width==2);
    auto texturePreview=CreateImageThumbnail(*decoded); assert(texturePreview.width==160);
    std::filesystem::remove(texture);
    std::filesystem::remove(mesh); std::filesystem::remove(file);
}
void AssetDropInteraction() {
    const auto folder=std::filesystem::absolute("Generated/EditorTests/Drop");
    std::filesystem::create_directories(folder);
    const auto mesh=folder/"triangle.obj";
    { std::ofstream output(mesh); output << "v -1 -1 0\nv 0 1 0\nv 1 -1 0\nf 1 2 3\n"; }
    ModelAssetLibrary library(folder,{});
    AssetPropertyWidget widget;
    widget.Configure(folder,[&](const auto& path,bool texture) -> Result<std::string> {
        auto reference=library.Reference(path); if(!reference) return std::unexpected(reference.error());
        if(texture) { auto image=library.Texture(*reference); if(!image) return std::unexpected(image.error()); }
        else { auto model=library.Model(*reference); if(!model) return std::unexpected(model.error()); }
        return reference;
    },{},{});
    ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.DisplaySize={640,480}; io.DeltaTime=1.0f/60;
    io.IniFilename=nullptr;
    unsigned char* fontPixels; int width,height;
    io.Fonts->GetTexDataAsRGBA32(&fontPixels,&width,&height); io.Fonts->SetTexID(1);
    LocalizationService text;
    PropertyValue value=std::string{};
    PropertyDescriptor descriptor{"mesh","property.mesh",PropertyKind::MeshAsset,{},{},{}};
    UI::ItemOptions options; options.id=UI::ItemId::FromString("mesh"); options.label="Mesh";
    const auto path=AssetCatalog::Utf8(mesh);
    ImVec2 target{100,100}; bool delivered=false;
    const auto frame=[&](bool source,bool down) {
        io.AddMousePosEvent(target.x,target.y); io.AddMouseButtonEvent(0,down);
        ImGui::NewFrame(); ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({400,300});
        ImGui::Begin("Drop test",nullptr,ImGuiWindowFlags_NoSavedSettings);
        const auto cursor=ImGui::GetCursorScreenPos();
        target={cursor.x+60,cursor.y+ImGui::GetTextLineHeightWithSpacing()+ImGui::GetFrameHeight()*.5f};
        if(source && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern)) {
            ImGui::SetDragDropPayload("NEXUS_ASSET_PATH",path.c_str(),path.size()+1);
            ImGui::TextUnformatted("triangle.obj"); ImGui::EndDragDropSource();
        }
        const auto result=widget.Draw(options,value,descriptor,text,"",-1);
        delivered |= result.Changed() && result.EditFinished();
        ImGui::End(); ImGui::Render();
    };
    frame(false,false); frame(true,true); frame(true,true); frame(true,false);
    assert(delivered && std::get<std::string>(value)=="triangle.obj");
    // 同じPayloadをTexture欄へ落としても、型検証が元の値を保つ。
    descriptor.kind=PropertyKind::TextureAsset; value=std::string("unchanged.png"); delivered=false;
    frame(false,false); frame(true,true); frame(true,true); frame(true,false);
    assert(!delivered && std::get<std::string>(value)=="unchanged.png");
    ImGui::DestroyContext(); std::filesystem::remove(mesh);
}
void GizmoWorldTransactions() {
    Fixture f;
    auto* level=f.session.GetLevel();
    auto parent=level->CreateEntity("Parent"), child=level->CreateEntity("Child"), other=level->CreateEntity("Other");
    const auto parentId=Id(parent), childId=Id(child), otherId=Id(other);
    parent.GetComponent<TransformComponent>()->translation={3,1,0};
    parent.GetComponent<TransformComponent>()->rotation=Quaternion::FromEulerRadians({0,0,.7f});
    parent.GetComponent<TransformComponent>()->scale={2,2,2};
    child.GetComponent<TransformComponent>()->translation={1,0,0};
    assert(f.edits.Reparent(childId,parentId));
    assert(level->Update(0));
    f.session.Selection().Replace(std::vector<PersistentId>{childId,otherId});
    TransformGizmoTransaction gizmo(f.session,f.properties);
    const auto pivot=level->FindEntity(childId).GetComponent<TransformComponent>()->worldMatrix;
    const auto originalWorld=pivot;
    assert(gizmo.Begin(pivot,true));
    assert(gizmo.Preview(Multiply(pivot,MakeTranslationMatrix({1,2,3}))));
    assert(gizmo.Preview(Multiply(pivot,MakeTranslationMatrix({2,3,4}))));
    gizmo.Commit(); assert(!gizmo.IsActive()); assert(level->Update(0));
    auto moved=level->FindEntity(childId).GetComponent<TransformComponent>()->worldMatrix;
    for(size_t axis=0;axis<3;++axis) assert(std::abs(moved.At(3,axis)-originalWorld.At(3,axis)-static_cast<float>(axis+2))<1e-4f);
    assert(f.properties.Undo()); assert(level->Update(0));
    assert(std::abs(level->FindEntity(otherId).GetComponent<TransformComponent>()->translation.x)<1e-5f);
    assert(f.properties.Redo()); assert(f.properties.Undo()); assert(level->Update(0));

    // 選択親と選択子を同時に移動しても、子のLocal値に差分を重ねない。
    f.session.Selection().Replace(std::vector<PersistentId>{parentId,childId});
    const auto parentPivot=level->FindEntity(parentId).GetComponent<TransformComponent>()->worldMatrix;
    assert(gizmo.Begin(parentPivot,true));
    assert(gizmo.Preview(Multiply(parentPivot,MakeTranslationMatrix({4,0,0}))));
    assert(std::abs(level->FindEntity(childId).GetComponent<TransformComponent>()->translation.x-1)<1e-4f);
    gizmo.Cancel(); assert(level->Update(0));
    assert(std::abs(level->FindEntity(parentId).GetComponent<TransformComponent>()->translation.x-3)<1e-4f);

    f.session.Select(otherId); auto* transform=level->FindEntity(otherId).GetComponent<TransformComponent>();
    transform->translation={2,0,0}; assert(level->Update(0));
    auto start=transform->worldMatrix;
    assert(gizmo.Begin(start,false));
    assert(gizmo.Preview(Multiply(start,MakeAffineMatrix(Vector3::One(),Quaternion::FromEulerRadians({0,0,.4f}),{}))));
    gizmo.Commit(); assert(f.properties.Undo()); assert(level->Update(0));
    start=transform->worldMatrix;
    assert(gizmo.Begin(start,false));
    assert(gizmo.Preview(Multiply(start,MakeScaleMatrix({1.5f,2,1})))); gizmo.Cancel();
    Matrix4x4 shear=start; shear.At(0,1)=.25f;
    assert(gizmo.Begin(start,false)); assert(!gizmo.Preview(shear)); gizmo.Cancel();
    transform->scale={-1,2,3}; assert(level->Update(0)); start=transform->worldMatrix;
    assert(gizmo.Begin(start,true)); assert(gizmo.Preview(Multiply(start,MakeTranslationMatrix({1,0,0}))));
    assert(transform->scale.x==-1 && transform->scale.y==2); gizmo.Cancel();
}
void ImGuizmoInteraction() {
    Fixture f;
    auto entity=f.session.GetLevel()->CreateEntity("Gizmo"); const auto id=Id(entity);
    entity.GetComponent<TransformComponent>()->translation={0,0,4};
    f.session.Select(id);
    ViewportTools tools(f.session,f.properties);
    ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.DisplaySize={640,480}; io.DeltaTime=1.0f/60; io.IniFilename=nullptr;
    unsigned char* pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h); io.Fonts->SetTexID(1);
    RenderScene scene; scene.camera=RenderCamera{};
    const auto frame=[&](float x,bool down) {
        assert(f.session.GetLevel()->Update(0)); tools.Prepare(scene,4.0f/3);
        io.AddMousePosEvent(x,250); io.AddMouseButtonEvent(0,down);
        ImGui::NewFrame(); ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext()); ImGuizmo::BeginFrame();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({640,480});
        ImGui::Begin("Viewport test",nullptr,ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetCursorScreenPos({100,100}); ImGui::Dummy({400,300});
        tools.DrawOverlay(100,100,400,300);
        ImGui::End(); ImGui::Render();
    };
    frame(330,false); frame(330,false); frame(330,true);
    assert(tools.IsManipulating());
    frame(350,true); frame(370,true);
    assert(entity.GetComponent<TransformComponent>()->translation.x>0);
    frame(370,false); assert(!tools.IsManipulating());
    assert(f.properties.Undo()); assert(std::abs(entity.GetComponent<TransformComponent>()->translation.x)<1e-5f);
    assert(!f.properties.CanUndo());
    ImGui::DestroyContext();
}
void OffsetMeshGizmoInteraction() {
    Fixture f;
    auto* level=f.session.GetLevel();
    auto entity=level->CreateEntity("Offset mesh"), background=level->CreateEntity("Behind handles");
    const auto id=Id(entity), backgroundId=Id(background);
    assert(entity.AddComponent<PrimitiveRenderComponent>());
    assert(background.AddComponent<PrimitiveRenderComponent>());
    entity.GetComponent<PrimitiveRenderComponent>()->meshAsset = kStartupModelAssetReference;
    background.GetComponent<PrimitiveRenderComponent>()->meshAsset = kStartupModelAssetReference;
    entity.GetComponent<TransformComponent>()->translation={-3,0,4};
    background.GetComponent<TransformComponent>()->translation={-300,0,8};
    background.GetComponent<TransformComponent>()->scale={100,100,1};
    std::array<ModelVertex,3> vertices{};
    const float positions[3][3]{{1.9f,-.1f,0},{2,.1f,0},{2.1f,-.1f,0}};
    for(size_t i=0;i<3;++i) std::copy(std::begin(positions[i]),std::end(positions[i]),vertices[i].position);
    const std::array<uint16_t,3> indices{0,1,2};
    ModelAssetData model;
    MeshAssetData mesh; mesh.vertexStride=sizeof(ModelVertex);
    const auto* vertexBytes=reinterpret_cast<const uint8_t*>(vertices.data());
    mesh.vertexData.assign(vertexBytes,vertexBytes+sizeof(vertices));
    const auto* indexBytes=reinterpret_cast<const uint8_t*>(indices.data());
    mesh.indexData.assign(indexBytes,indexBytes+sizeof(indices));
    mesh.indexFormat=IndexFormat::UInt16; mesh.submeshes.push_back({0,3,0,0}); model.meshes.push_back(std::move(mesh));
    ModelNodeAssetData node; node.meshIndices={0}; node.localTransform.translation={1,0,0};
    model.nodes.push_back(node);
    ModelInstance pose; assert(pose.Initialize(model));
    f.session.Select(id);
    ViewportTools tools(f.session,f.properties); tools.SetModel(model,pose);
    ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.DisplaySize={640,480}; io.DeltaTime=1.0f/60; io.IniFilename=nullptr;
    unsigned char* pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h); io.Fonts->SetTexID(1);
    RenderScene scene;
    const auto frame=[&](ImVec2 mouse,bool down,ImGuiKey key=ImGuiKey_None,bool keyDown=false) {
        assert(level->Update(0));
        assert(RenderSceneExtractor{}.ExtractInto(*level->GetScene(),4.0f/3,scene));
        scene.camera=RenderCamera{}; tools.Prepare(scene,4.0f/3);
        io.AddMousePosEvent(mouse.x,mouse.y); io.AddMouseButtonEvent(0,down);
        if(key!=ImGuiKey_None) io.AddKeyEvent(key,keyDown);
        ImGui::NewFrame(); ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext()); ImGuizmo::BeginFrame();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({640,480}); ImGui::SetNextWindowFocus();
        ImGui::Begin("Offset viewport",nullptr,ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetCursorScreenPos({100,100}); ImGui::Dummy({400,300});
        tools.DrawOverlay(100,100,400,300);
        ImGui::End(); ImGui::Render();
    };
    const ImVec2 outside{110,110};
    const auto setMode=[&](ImGuiKey key) { frame(outside,false,key,true); frame(outside,false,key,false); };
    const auto assertCenter=[&] {
        assert(level->Update(0));
        const auto pivot=tools.GetSelectionPivot(); assert(pivot);
        assert(std::abs(pivot->At(3,0))<1e-4f && std::abs(pivot->At(3,1))<1e-4f && std::abs(pivot->At(3,2)-4)<1e-4f);
    };
    frame(outside,false); frame(outside,false); assertCenter();
    // ギズモの位置には別Entityがあることを先に確認する。
    assert(tools.Pick(.575f,.5f,false)); assert(f.session.GetSelection()==backgroundId); f.session.Select(id);

    // ホバーを挟まず外側から直接click。軸の背後を選択してはいけない。
    frame(outside,false); frame({330,250},true);
    assert(tools.IsManipulating() && f.session.GetSelection()==id);
    assert(!tools.Pick(.575f,.5f,false)); assert(f.session.GetSelection()==id);
    frame({350,250},true); frame({370,250},true); frame({370,250},false);
    assert(entity.GetComponent<TransformComponent>()->translation.x>-3);
    assert(f.properties.Undo()); assert(!f.properties.CanUndo()); assertCenter();

    const auto findHandle=[&](ImGuizmo::MOVETYPE type) {
        std::array<int,18> hovered{};
        for(int y=170;y<=330;y+=4) for(int x=220;x<=380;x+=4) {
            frame({static_cast<float>(x),static_cast<float>(y)},false);
            ++hovered[static_cast<size_t>(ImGuizmo::GetHoveredHandleType())];
            if(ImGuizmo::GetHoveredHandleType()==type) return ImVec2{static_cast<float>(x),static_cast<float>(y)};
        }
        std::cerr << "Missing gizmo handle " << static_cast<int>(type) << "; hovered:";
        for(size_t i=0;i<hovered.size();++i) if(hovered[i]) std::cerr << ' ' << i << '=' << hovered[i];
        std::cerr << '\n';
        assert(false && "Expected ImGuizmo handle was not found"); return ImVec2{};
    };
    setMode(ImGuiKey_E);
    const auto ring=findHandle(ImGuizmo::MT_ROTATE_Z);
    frame(outside,false); frame(ring,true);
    assert(tools.IsManipulating() && f.session.GetSelection()==id);
    const ImVec2 rotated{300-(ring.y-250),250+(ring.x-300)};
    frame(rotated,true); frame(rotated,false);
    assert(std::abs(entity.GetComponent<TransformComponent>()->rotation.z)>.01f); assertCenter();
    assert(f.properties.Undo()); assert(!f.properties.CanUndo()); assertCenter();

    setMode(ImGuiKey_R);
    const auto handle=findHandle(ImGuizmo::MT_SCALE_X);
    frame(outside,false); frame(handle,true);
    assert(tools.IsManipulating() && f.session.GetSelection()==id);
    const ImVec2 scaled{handle.x+30,handle.y}; frame(scaled,true); frame(scaled,false);
    const auto scaledX=entity.GetComponent<TransformComponent>()->scale.x;
    assert(scaledX>1.01f && scaledX<10); assertCenter();
    assert(f.properties.Undo()); assert(!f.properties.CanUndo()); assertCenter();

    setMode(ImGuiKey_W); frame({330,250},true); frame({350,250},true);
    frame({350,250},true,ImGuiKey_Escape,true);
    assert(!tools.IsManipulating() && f.session.GetSelection()==id); assertCenter();
    frame({400,260},true,ImGuiKey_Escape,false); assert(f.session.GetSelection()==id);
    frame({400,260},false); frame({400,260},true); frame({400,260},false);
    assert(f.session.GetSelection()==backgroundId);
    ImGui::DestroyContext();
}
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
    auto* firstWindow = ImGui::FindWindowByName("###Test");
    assert(firstWindow && firstWindow->SizeFull.x >= 300 && firstWindow->SizeFull.y >= 300);
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
    mesh.submeshes.push_back({0,3,0,0});
    ModelInstance pose; assert(pose.Initialize(model));
    assert(f.session.GetLevel()->Update(0));
    RenderScene frame; assert(RenderSceneExtractor{}.ExtractInto(*f.session.GetLevel()->GetScene(),1,frame));
    ViewportTools tools{f.session,f.properties}; tools.SetModel(model,pose); tools.Prepare(frame,1);
    f.session.Selection().Clear();
    assert(!tools.Pick(0.5f,0.5f,false));
    assert(f.session.Selection().All().empty());
    entity.GetComponent<PrimitiveRenderComponent>()->meshAsset = kStartupModelAssetReference;
    assert(RenderSceneExtractor{}.ExtractInto(*f.session.GetLevel()->GetScene(),1,frame));
    tools.Prepare(frame,1);
    f.session.Selection().Clear(); assert(tools.Pick(0.5f,0.5f,false)); assert(f.session.GetSelection() == selected);
    assert(tools.Pick(0.5f,0.5f,true)); assert(f.session.Selection().All().empty());
    assert(!tools.Pick(0,0,false)); assert(!tools.Pick(-1,0,false));
    // Only the submitted index range is selectable, with the draw's base vertex.
    const float offsetVertices[]{20,0,0, 24,0,0, 22,4,0, -2,0,0, 2,0,0, 0,4,0};
    const uint16_t offsetIndices[]{0,0,0,0,1,2};
    mesh.vertexData.resize(sizeof(offsetVertices)); std::memcpy(mesh.vertexData.data(),offsetVertices,sizeof(offsetVertices));
    mesh.indexData.resize(sizeof(offsetIndices)); std::memcpy(mesh.indexData.data(),offsetIndices,sizeof(offsetIndices));
    mesh.submeshes = {{3,3,3,0}};
    tools.SetModel(model,pose);
    assert(tools.Pick(.5f,.5f,false) && f.session.GetSelection() == selected);
    mesh.submeshes = {{0,3,0,0}};
    assert(!tools.Pick(.5f,.5f,false));
    mesh.submeshes.clear(); assert(!tools.Pick(.5f,.5f,false));
    // Skinning uses the current palette and must not apply the mesh node twice.
    model.nodes[0].localTransform.translation = {40,0,0};
    model.nodes.resize(2); model.nodes[1].localTransform.translation = {-22,0,0};
    model.skins.push_back({"skin",{1},{Matrix4x4::Identity()}});
    std::array<ModelVertex,3> skinned{};
    for(size_t v = 0; v < skinned.size(); ++v) {
        std::copy_n(offsetVertices+v*3,3,skinned[v].position);
        skinned[v].position[1] = vertices[v*3+1];
        skinned[v].jointWeights[0] = 1;
    }
    mesh.vertexStride = sizeof(ModelVertex); mesh.vertexData.resize(sizeof(skinned));
    std::memcpy(mesh.vertexData.data(),skinned.data(),sizeof(skinned));
    mesh.submeshes = {{3,3,0,0}}; mesh.skinIndex = 0;
    pose.Shutdown(); assert(pose.Initialize(model)); tools.SetModel(model,pose);
    assert(tools.Pick(.5f,.5f,false) && f.session.GetSelection() == selected);
    model.nodes[1].localTransform.translation.x = 0;
    pose.Shutdown(); assert(pose.Initialize(model));
    assert(!tools.Pick(.5f,.5f,false));
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
    // GI defaults, persistence and invalid values use a disposable project configuration.
    const auto settingsRoot = std::filesystem::absolute("Generated/EditorTests/GIPreferences");
    std::filesystem::create_directories(settingsRoot / "Config");
    std::ofstream(settingsRoot / "Config/Editor.settings", std::ios::trunc).close();
    EditorSettingsStore giSettings; giSettings.Load(settingsRoot);
    assert(giSettings.editor.giMode == "Off");
    for(const char* mode : {"Off", "DDGI", "RTGI", "Baked"}) {
        giSettings.editor.giMode = mode;
        giSettings.editor.gizmo = false;
        assert(giSettings.SaveEditor());
        EditorSettingsStore restored; restored.Load(settingsRoot);
        assert(restored.editor.giMode == mode && !restored.editor.gizmo);
    }
    std::ofstream(settingsRoot / "Config/Editor.settings", std::ios::trunc) << "giMode \"Invalid\"\n";
    giSettings.Load(settingsRoot);
    assert(giSettings.editor.giMode == "Off");
    // 旧Propertiesだけを表示していた設定を、Sceneや他の個人設定を変えずDetailsへ移行する。
    EditorSettingsStore legacySettings; legacySettings.Load(std::filesystem::absolute("Project"));
    legacySettings.user.panels["Details"] = false; legacySettings.user.panels["Properties"] = true;
    assert(legacySettings.SaveUser());
    Fixture f;
    for(int i = 0; i < 10000; ++i) static_cast<void>(f.session.GetLevel()->CreateEntity("Entity " + std::to_string(i)));
    ImGui::CreateContext(); auto& io = ImGui::GetIO(); io.DisplaySize = {1600,1000}; io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    io.Fonts->SetTexID(2); // Keep the font distinct from the fake viewport texture (1).
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
        assert(workspace.Commands().Execute("window.reset"));
        for(int i = 0; i < 4; ++i) frame();
        auto* detailsWindow = ImGui::FindWindowByName("###Details");
        assert(detailsWindow && detailsWindow->DockNode);
        assert(!ImGui::FindWindowByName("###Properties"));
        for(const auto& entry : workspace.Panels().Entries()) assert(entry.Id() != "Properties");
        assert(detailsWindow->Active);
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
        const auto assertViewportOpaque = [&] {
            auto* viewport = ImGui::FindWindowByName("###Viewport"); assert(viewport && viewport->Active);
            bool imageFound = false;
            for(const auto& command : viewport->DrawList->CmdBuffer) if(command.GetTexID() == 1 && command.ElemCount > 0) {
                for(unsigned int i = command.IdxOffset; i < command.IdxOffset + command.ElemCount; ++i) {
                    const auto& vertex = viewport->DrawList->VtxBuffer[command.VtxOffset + viewport->DrawList->IdxBuffer[i]];
                    assert((vertex.col & IM_COL32_A_MASK) == IM_COL32_A_MASK);
                }
                imageFound = true;
            }
            assert(imageFound);
        };
        assertViewportOpaque();
        assert(!workspace.Commands().Execute("file.save"));
        assert(workspace.Commands().Execute("play.pause")); assert(f.session.IsPaused());
        frame(); assertViewportOpaque();
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
        EditorSettingsStore migrated; migrated.Load(std::filesystem::absolute("Project"));
        assert(!migrated.user.panels.contains("Properties") && migrated.user.panels.at("Details"));
    }
    ImGui::DestroyContext();
    _wputenv_s(L"LOCALAPPDATA",previous ? previous : L""); std::free(previous);
}
}
#include "DetailsCategoryTests.inl"
#include "PrimitiveShapeTests.inl"
#include "StartupPathTests.inl"
#include "EntityConsoleTests.inl"
#include "ViewportSceneGuidesTests.inl"
#include "ViewportPreviewTests.inl"
#include "EditorToolbarTests.inl"
// Composite axis controls must forward activation and release even on frames
// without a value change; the property service uses these to bound one undo step.
void AxisWidgetInteraction() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.DisplaySize = {800,600}; io.DeltaTime = 1.0f/60;
    io.IniFilename = nullptr;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height); io.Fonts->SetTexID(1);
    UI::ApplyEditorStyle();
    for(float fieldWidth : {360.0f, 95.0f}) {
        std::array<float,3> values{1,2,3};
        UI::ItemOptions options; options.id = UI::ItemId::FromString("axis-test"); options.size.x = fieldWidth;
        bool enabled = true;
        ImVec2 origin;
        const auto frame = [&](ImVec2 mouse, bool down) {
            io.AddMousePosEvent(mouse.x, mouse.y); io.AddMouseButtonEvent(0, down);
            ImGui::NewFrame(); ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({500,500});
            ImGui::Begin("Axis test", nullptr, ImGuiWindowFlags_NoSavedSettings);
            origin = ImGui::GetCursorScreenPos(); options.enabled = enabled;
            auto result = UI::PropertyVector(options, values, 0.1f);
            ImGui::End(); ImGui::Render(); return result;
        };
        frame({-100,-100}, false); frame({-100,-100}, false);
        const ImVec2 target{origin.x + 35, origin.y + ImGui::GetFrameHeight() * .5f};
        frame(target, false);
        assert(UI::HasStatus(frame(target, true).status, UI::ItemStatus::Activated));
        const ImVec2 moved{target.x + 30, target.y};
        const auto dragged = frame(moved, true);
        assert(dragged.Changed() && !dragged.EditFinished());
        assert(values[0] != 1 && values[1] == 2 && values[2] == 3);
        assert(frame(moved, false).EditFinished());
        enabled = false;
        const auto before = values;
        frame(target, false); frame(target, true);
        assert(!frame(moved, true).Changed() && values == before);
        frame(moved, false);
    }
    ImGui::DestroyContext();
}
void RunReinstancingTests();
void RunGameRuntimeTests();
void RunReflectedComponentTests();
int main() {
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    RunReinstancingTests();
    RunGameRuntimeTests();
    RunReflectedComponentTests();
    EditorToolbarTests();
    EditorToolbarSettingsTests();
    AxisWidgetInteraction();
    ComponentAndAssetEditing();
    EntityConsoleTests();
    TerminalSuggestionTests();
    PrimitiveShapeTests();
    StartupPathTests();
    DetailsCategoryIntegration();
    AssetDropInteraction();
    GizmoWorldTransactions(); ImGuizmoInteraction();
    OffsetMeshGizmoInteraction();
    PropertyTransactions(); StructuralHistory(); DocumentsAndPlay(); HistoryBoundsAndCommands(); LazyPanelLifecycle();
    ViewportPickingAndValidation();
    ViewportSceneGuidesTests();
    ViewportPreviewTests(); ViewportPreviewOverlayTests();
    CameraFarPlaneWorldProjectionTests();
    CacheAndWorkspaceIntegration();
    std::cout << "PASS: unified Details categories/header, component fallback/extensions, material source/undo/save, selection lifetime, offset-mesh pivot/rotation/scale, gizmo click capture over background, ImGuizmo mouse interaction, parent/multi-selection transforms, gizmo undo/cancel, component addition, asset round-trip/undo, model/texture variants, thumbnails, property transactions, rollback, structural undo, save protection, play isolation, history bounds, shortcuts, lazy panels, viewport picking, rotation/camera/light validation\n";
}
