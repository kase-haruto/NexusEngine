namespace {
void ViewportSceneGuidesTests() {
    Fixture f;
    auto* level=f.session.GetLevel();
    auto camera=level->CreateEntity("Debug camera");
    assert(camera.AddComponent<CameraComponent>());
    camera.GetComponent<TransformComponent>()->translation={-2,0,10};
    camera.GetComponent<CameraComponent>()->primary=false;
    auto light=level->CreateEntity("Debug sun");
    assert(light.AddComponent<DirectionalLightComponent>());
    light.GetComponent<TransformComponent>()->translation={2,0,10};
    auto hidden=level->CreateEntity("Behind eye");
    assert(hidden.AddComponent<CameraComponent>());
    hidden.GetComponent<CameraComponent>()->primary=false;
    hidden.GetComponent<TransformComponent>()->translation={0,0,-10};
    assert(level->Update(0));
    RenderScene render; render.camera=RenderCamera{};
    ViewportTools tools(f.session,f.properties); // No model is needed to pick camera/light icons.
    tools.Prepare(render,4.0f/3);
    ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.DisplaySize={800,600}; io.DeltaTime=1.0f/60; io.IniFilename=nullptr;
    unsigned char* pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h); io.Fonts->SetTexID(1);
    const auto frame=[&](ImVec2 mouse,bool down,float width=400,float height=300) {
        tools.Prepare(render,4.0f/3);
        io.AddMousePosEvent(mouse.x,mouse.y); io.AddMouseButtonEvent(0,down);
        ImGui::NewFrame(); ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext()); ImGuizmo::BeginFrame();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({800,600});
        ImGui::Begin("Scene guides",nullptr,ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetCursorScreenPos({100,100}); ImGui::Dummy({width,height});
        auto* draw=ImGui::GetWindowDrawList(); const int before=draw->VtxBuffer.Size;
        tools.DrawOverlay(100,100,width,height);
        const int added=draw->VtxBuffer.Size-before;
        for(int i=before;i<draw->VtxBuffer.Size;++i) {
            assert(std::isfinite(draw->VtxBuffer[i].pos.x) && std::isfinite(draw->VtxBuffer[i].pos.y));
        }
        ImGui::End(); ImGui::Render(); return added;
    };
    const auto position=[&](Entity entity) {
        const auto& m=render.camera->viewProjectionMatrix;
        const auto& world=entity.GetComponent<TransformComponent>()->worldMatrix;
        const float x=world.At(3,0), y=world.At(3,1), z=world.At(3,2);
        const float cw=x*m.At(0,3)+y*m.At(1,3)+z*m.At(2,3)+m.At(3,3);
        return ImVec2{(x*m.At(0,0)+y*m.At(1,0)+z*m.At(2,0)+m.At(3,0))/cw*.5f+.5f,
            .5f-(x*m.At(0,1)+y*m.At(1,1)+z*m.At(2,1)+m.At(3,1))/cw*.5f};
    };
    const auto cp=position(camera), lp=position(light);
    assert(frame({110,110},false)>0);
    assert(frame({110,110},false)>0);
    // Click the actual overlay at an offset viewport origin.
    frame({100+cp.x*400,100+cp.y*300},true);
    assert(f.session.GetSelection()==Id(camera));
    frame({110,110},false);
    assert(tools.Pick(lp.x,lp.y,false) && f.session.GetSelection()==Id(light));
    assert(tools.Pick(cp.x,cp.y,true) && f.session.Selection().All().size()==2);
    assert(tools.Pick(cp.x,cp.y,true) && !f.session.Selection().Contains(Id(camera)));
    assert(!tools.Pick(.5f,.5f,false) && f.session.Selection().All().empty()); // Behind-eye icon is excluded.
    // A nearer overlapping icon wins; disabled cameras remain editable.
    hidden.GetComponent<TransformComponent>()->translation={-1.6f,0,8};
    hidden.GetComponent<CameraComponent>()->enabled=false;
    assert(level->Update(0));
    assert(tools.Pick(cp.x,cp.y,false) && f.session.GetSelection()==Id(hidden));
    f.session.Selection().Clear();
    // Pixel hit targets stay fixed when the viewport is resized.
    frame({110,110},false,600,300);
    assert(tools.Pick(cp.x+10.0f/600,cp.y,false));
    assert(!tools.Pick(cp.x+18.0f/600,cp.y,false));
    // Frusta crossing the eye plane and invalid settings must produce finite draw vertices.
    camera.GetComponent<TransformComponent>()->translation={0,0,-1};
    assert(level->Update(0));
    assert(frame({110,110},false)>0);
    camera.GetComponent<CameraComponent>()->verticalFieldOfViewRadians=std::numeric_limits<float>::quiet_NaN();
    assert(frame({110,110},false)>0);
    camera.GetComponent<CameraComponent>()->verticalFieldOfViewRadians=1;
    assert(f.session.StartPlay());
    assert(frame({110,110},false)==0);
    assert(!tools.Pick(lp.x,lp.y,false));
    f.session.StopPlay();
    ImGui::DestroyContext();
}

void CameraFarPlaneWorldProjectionTests() {
    Fixture f;
    auto* level=f.session.GetLevel();
    auto camera=level->CreateEntity("Stationary scene camera");
    auto* settings=camera.AddComponent<CameraComponent>(); assert(settings);
    settings->primary=false; settings->verticalFieldOfViewRadians=.6f;
    settings->nearClip=.2f; settings->farClip=8;
    camera.GetComponent<TransformComponent>()->translation={0,0,10};
    assert(level->Update(0));
    const auto originalWorld=camera.GetComponent<TransformComponent>()->worldMatrix;
    RenderScene render; render.camera=RenderCamera{};
    ViewportTools tools(f.session,f.properties); tools.Prepare(render,4.0f/3);
    ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.DisplaySize={800,600}; io.DeltaTime=1.0f/60; io.IniFilename=nullptr;
    unsigned char* pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h); io.Fonts->SetTexID(1);
    const auto project=[&](Vector3 p) {
        const auto& m=render.camera->viewProjectionMatrix;
        const float cw=p.x*m.At(0,3)+p.y*m.At(1,3)+p.z*m.At(2,3)+m.At(3,3);
        return ImVec2{100+(1+(p.x*m.At(0,0)+p.y*m.At(1,0)+p.z*m.At(2,0)+m.At(3,0))/cw)*300,
            100+(1-(p.x*m.At(0,1)+p.y*m.At(1,1)+p.z*m.At(2,1)+m.At(3,1))/cw)*150};
    };
    const auto drawAndCheckFar=[&](float farDepth=3) {
        ImGui::NewFrame(); ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext()); ImGuizmo::BeginFrame();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({800,600});
        ImGui::Begin("Fixed camera far",nullptr,ImGuiWindowFlags_NoSavedSettings);
        // GUI size can differ from the rendered image's prepared aspect during resize.
        ImGui::SetCursorScreenPos({100,100}); ImGui::Dummy({600,300});
        auto* draw=ImGui::GetWindowDrawList(); const int begin=draw->VtxBuffer.Size;
        tools.DrawOverlay(100,100,600,300);
        const float halfHeight=std::tan(.3f)*farDepth, halfWidth=halfHeight*4/3;
        for(int i=0;i<4;++i) {
            // Far is anchored to the scene camera, regardless of editor eye/rotation/FOV.
            const auto expected=project({i&1 ? halfWidth : -halfWidth,i&2 ? halfHeight : -halfHeight,10+farDepth});
            bool found=false;
            for(int v=begin;v<draw->VtxBuffer.Size;++v) {
                const auto& vertex=draw->VtxBuffer[v];
                if(vertex.col!=IM_COL32(100,205,255,220)) continue;
                const float dx=vertex.pos.x-expected.x,dy=vertex.pos.y-expected.y;
                found |= dx*dx+dy*dy<2.0f;
            }
            assert(found);
        }
        ImGui::End(); ImGui::Render();
    };
    drawAndCheckFar();
    const auto before=project({0,0,13});
    // Change only the observer, including its projection. The scene far remains fixed.
    const auto view=TryInverse(MakeAffineMatrix(Vector3::One(),Quaternion::FromEulerRadians({-.1f,.15f,0}),{2,1,-3}));
    assert(view);
    const auto projection=MakePerspectiveFovMatrix(.9f,4.0f/3,.05f,100000);
    render.camera=RenderCamera{{},*view,projection,Multiply(*view,projection)};
    drawAndCheckFar();
    const auto after=project({0,0,13});
    assert(std::abs(before.x-after.x)>20 && std::abs(before.y-after.y)>5);
    assert(camera.GetComponent<TransformComponent>()->worldMatrix.elements==originalWorld.elements);
    // Larger real clip distances keep the symbol compact and do not alter camera settings.
    settings->farClip=12; tools.Prepare(render,4.0f/3);
    drawAndCheckFar(); assert(settings->farClip==12 && settings->nearClip==.2f);
    settings->farClip=1000; tools.Prepare(render,4.0f/3);
    drawAndCheckFar(); assert(settings->farClip==1000);
    settings->farClip=2; tools.Prepare(render,4.0f/3);
    drawAndCheckFar(2);
    settings->farClip=8; tools.Prepare(render,4.0f/3);
    drawAndCheckFar();
    assert(!settings->smallFrustum);
    const PropertyHandle size{Id(camera),"Camera","frustumSize"};
    assert(f.properties.Preview(size,EnumValue{1})); f.properties.Commit();
    assert(settings->smallFrustum);
    assert(settings->farClip==8 && settings->nearClip==.2f);
    tools.Prepare(render,4.0f/3); drawAndCheckFar(3.0f/7.0f);
    assert(!f.properties.Preview(size,EnumValue{2}));
    assert(f.properties.Undo()); assert(!settings->smallFrustum);
    tools.Prepare(render,4.0f/3);
    drawAndCheckFar();
    assert(f.properties.Redo()); assert(settings->smallFrustum);
    tools.Prepare(render,4.0f/3); drawAndCheckFar(3.0f/7.0f);
    // Another camera retains its own normal-size setting, including after save/reload.
    auto other=level->CreateEntity("Normal-size camera");
    auto* otherSettings=other.AddComponent<CameraComponent>(); assert(otherSettings);
    otherSettings->primary=false;
    assert(!otherSettings->smallFrustum);
    const auto path=std::filesystem::absolute("Generated/EditorTests/CameraGuideSize.level");
    std::filesystem::create_directories(path.parent_path());
    assert(level->Save(path));
    Level loaded; assert(loaded.Load(path));
    assert(loaded.FindEntity(Id(camera)).GetComponent<CameraComponent>()->smallFrustum);
    assert(!loaded.FindEntity(Id(other)).GetComponent<CameraComponent>()->smallFrustum);
    const auto legacyPath=path.parent_path()/"CameraGuideLegacy.level";
    {
        std::ofstream legacy(legacyPath);
        legacy << "NEXUS_LEVEL 10\nLEVEL \"legacy\"\nLIGHTING 0\nENVIRONMENT \"\" 1 0\n"
            "ENTITY 1 1 \"camera\"\nTRANSFORM 0 0 0 0 0 0 1 1 1 1\nPARENT 0 0\n"
            "CAMERA 1 0.6 0.2 8 1 1\nPRIMITIVE 0\nDIRECTIONAL_LIGHT 0\nEND_ENTITY\nEND_LEVEL\n";
    }
    Level legacy; assert(legacy.Load(legacyPath));
    assert(!legacy.FindEntity({1,1}).GetComponent<CameraComponent>()->smallFrustum);
    ImGui::DestroyContext();
}
}
