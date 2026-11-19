#include "Editor/Services/AnimationStudioDocument.h"
#include "Editor/Services/CurveEditDocument.h"
#include "Editor/Services/AnimationCurvePreview.h"
#include "Runtime/Scene/Components/KeyframeAnimationComponent.h"
#include "Runtime/Scene/Components/CameraComponent.h"
#include "Runtime/Rendering/RenderSceneExtractor.h"
#include "Editor/Core/EditorSession.h"
#include "Editor/Core/EditorWorkspace.h"
#include "Runtime/Scene/Components/PrimitiveRenderComponent.h"
#include "ThirdParty/imgui/imgui.h"
#include <iostream>
#include <stdexcept>

namespace NexusEngine::Tests {
namespace {
void StudioExpect(bool result, const char* message) {
    if(!result) {
        throw std::runtime_error(message);
    }
}
template<class T> void StudioCheck(const Result<T>& result) {
    if(!result) {
        throw std::runtime_error(result.error().GetMessageText());
    }
}
}
void RunAnimationStudioTests() {

    const auto project = std::filesystem::absolute("Generated/AnimationStudioTests");
    const auto assets = project/"Assets";
    std::filesystem::create_directories(assets);
    AnimationStudioDocument document;
    document.Initialize(assets);
    document.New(0);
    document.reference = "Animations/Move.nxanim";
    auto& clip = std::get<AnimationClipDocument>(document.value);
    AnimationTrack track;
    track.keys = {{0,{0,0,0},{},{}},{1,{10,0,0},{},{}}};
    clip.tracks.push_back(track);
    const auto before = document.value;
    clip.tracks[0].keys[1].value[0] = 20;
    document.Changed(before,false);
    document.Undo();
    StudioExpect(std::get<AnimationClipDocument>(document.value).tracks[0].keys[1].value[0] == 10,"Asset Undo failed.");
    document.Redo();

    StudioCheck(document.Save());
    StudioExpect(!document.dirty,"Asset Save did not clear dirty state.");
    StudioCheck(document.Open(assets/"Animations/Move.nxanim"));
    StudioExpect(std::get<AnimationClipDocument>(document.value).tracks[0].keys[1].value[0] == 20,"Asset open failed.");

    EditorSession session;
    StudioCheck(session.Create());
    StudioCheck(session.SetResourceRoot(project));
    auto entity = session.GetLevel()->CreateEntity("Actor");
    StudioExpect(entity.AddComponent<PrimitiveRenderComponent>() != nullptr,"Cannot create preview actor.");
    const auto id = entity.GetComponent<PersistentIdComponent>()->value;
    entity.GetComponent<TransformComponent>()->translation.x = 7;
    session.Select(id);
    document.preview = true;
    document.time = .5f;

    StudioCheck(document.Preview(session,0));
    StudioExpect(session.HasAnimationPreview(),"Preview World was not created.");
    StudioExpect(session.GetPreviewLevel()->FindEntity(id).GetComponent<TransformComponent>()->translation.x == 10,"Preview did not sample draft.");
    StudioExpect(session.GetLevel()->FindEntity(id).GetComponent<TransformComponent>()->translation.x == 7,"Preview changed the editing World.");
    document.StopPreview(session);
    StudioExpect(!session.HasAnimationPreview() && session.GetLevel()->FindEntity(id).GetComponent<TransformComponent>()->translation.x == 7,"Preview cleanup failed.");

    StudioCheck(document.Attach(session));
    StudioCheck(session.UpdatePreview(0));
    StudioExpect(session.GetLevel()->FindEntity(id).GetComponent<TransformComponent>()->translation.x == 7,"Attached clip overwrote saved edit values.");

    StudioExpect(session.StartPlay(),"PIE failed to start.");
    StudioCheck(session.UpdatePreview(.5f));
    StudioExpect(session.GetPlayLevel()->FindEntity(id).GetComponent<TransformComponent>()->translation.x == 10,"PIE did not evaluate authored clip.");
    session.StopPlay();

    CurveEditDocument curve;
    curve.Initialize(assets);
    curve.New();
    StudioCheck(curve.Save("Curves/Speed.curve"));
    entity.GetComponent<KeyframeAnimationComponent>()->speedCurveAsset = "Curves/Speed.curve";
    ConnectAnimationCurvePreview(curve, document, session);
    curve.changed = [&](const auto& reference, auto snapshot) {
        if(session.HasAnimationPreview() && snapshot) {
            StudioCheck(session.GetPreviewLevel()->Animations().Curves().Override(reference, *snapshot));
        }
    };
    document.preview = true;
    document.time = .5f;
    StudioCheck(document.Preview(session, 0));
    StudioExpect(curve.preview.input().has_value(), "Curve preview context did not connect");
    StudioCheck(curve.SetKeys({{0, .5}, {1, .5}}));
    curve.EndEdit();
    StudioCheck(document.Preview(session, 0));
    StudioExpect(session.GetPreviewLevel()->FindEntity(id).GetComponent<TransformComponent>()->translation.x == 5,
        "Unsaved speed curve did not reevaluate paused preview");
    StudioExpect(session.GetLevel()->FindEntity(id).GetComponent<TransformComponent>()->translation.x == 7,
        "Curve draft modified edit World");
    StudioExpect(LoadFloatCurve(assets / "Curves/Speed.curve")->Evaluate(.5) == 1,
        "Curve draft overwrote saved asset");
    StudioExpect(curve.Undo(), "Curve preview undo failed");
    StudioCheck(document.Preview(session, 0));
    StudioExpect(session.GetPreviewLevel()->FindEntity(id).GetComponent<TransformComponent>()->translation.x == 10,
        "Curve undo did not restore preview");
    curve.preview.seek(.25);
    StudioCheck(document.Preview(session, 0));
    StudioExpect(session.GetPreviewLevel()->FindEntity(id).GetComponent<TransformComponent>()->translation.x == 5,
        "Curve context seek failed");
    document.StopPreview(session);
    StudioExpect(!curve.preview.input(), "Curve preview context retained a closed World");

    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = {1600,1000};
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().DeltaTime = 1.0f/60;
    unsigned char* pixels = nullptr;
    int width = 0,height = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    ImGui::GetIO().Fonts->SetTexID(1);
    {
        EditorWorkspace workspace(session);

        workspace.Initialize(project,{},std::filesystem::absolute("Project"));
        document.preview = true;
        StudioCheck(document.Preview(session, 0));
        RenderScene previewFrame;
        StudioCheck(RenderSceneExtractor{}.ExtractInto(*session.GetPreviewLevel()->GetScene(), 1, previewFrame, true));
        workspace.ViewportEditor().Prepare(previewFrame, 1);
        StudioExpect(previewFrame.camera.has_value(), "Animation preview without a Camera lost its Editor camera");
        auto cameraEntity = session.GetPreviewLevel()->CreateEntity("Preview camera");
        auto* camera = cameraEntity.AddComponent<CameraComponent>();
        camera->primary = true;
        camera->verticalFieldOfViewRadians = .6f;
        StudioCheck(session.GetPreviewLevel()->Update(0));
        StudioCheck(RenderSceneExtractor{}.ExtractInto(*session.GetPreviewLevel()->GetScene(), 1, previewFrame, true));
        StudioExpect(previewFrame.camera.has_value(), "Preview primary camera was not extracted");
        const auto animatedProjection = previewFrame.camera->projectionMatrix;
        workspace.ViewportEditor().Prepare(previewFrame, 1);
        StudioExpect(previewFrame.camera->projectionMatrix.elements == animatedProjection.elements, "Editor camera overwrote preview FOV");
        document.StopPreview(session);
        workspace.Panels().Open("AnimationStudio");
        workspace.Panels().Open("CurveEditor");
        for(int kind = 0; kind < 3; ++kind) {
            workspace.AnimationStudio().New(kind);
            for(int frame = 0; frame < 2; ++frame) {
                workspace.PrepareFrame();
                ImGui::NewFrame();

                workspace.Draw();
                ImGui::Render();
                StudioExpect(ImGui::GetDrawData()->TotalVtxCount > 0,"Studio panel produced no UI.");
            }
        }

        workspace.Shutdown();
    }
    ImGui::DestroyContext();
    std::cout << "PASS: Animation Studio, Curve panels, asset history, isolated unsaved speed preview, seek/disconnect and PIE\n";
}
}
