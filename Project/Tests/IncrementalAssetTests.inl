namespace {
void IncrementalAssetTests(bool largeScene) {
    performanceMeasurement = largeScene;
    Fixture test(false);
    performanceMeasurement = false;
    const auto root = std::filesystem::absolute("Project/Resources/Assets");
    ModelAssetData source;
    std::vector<ModelAssetLibrary::Request> requests;
    if(largeScene) {
        auto loaded = ModelLoader{}.LoadGltf(root / "Models/Sponza/Sponza.gltf"); Check(loaded);
        Check(AppendSponzaOpticalSpheres(*loaded)); Check(AppendPBRTestScene(*loaded));
        source = std::move(*loaded);
        requests.push_back({PrimitiveShape::Asset,kStartupModelAssetReference,""});
    } else {
        requests.push_back({PrimitiveShape::Cube,"",""}); requests.push_back({PrimitiveShape::Sphere,"",""});
    }
    ModelAssetLibrary library(root, source);
    library.Retain(requests);
    for(const auto& request : requests) Check(library.Resolve(request.shape,request.mesh,request.texture));
    auto snapshot = library.SharedSnapshot();
    ModelInstance pose; Check(pose.Initialize(*snapshot));
    PrimitiveRenderer active; active.SetModelInput(snapshot.get(), &pose);
    const GraphicsRendererInitializationContext context{test.factory,L"Project/Shaders",root};
    Check(active.Initialize(context));
    const auto initialProfile = active.GetInitializationProfile();
    Require(initialProfile.meshletPipelineMs == 0, "Disabled mesh shaders created a pipeline.");
    auto oldMesh = std::weak_ptr<MeshResource>{GraphicsBackendTestAccess::Mesh(active,0)};
    std::vector<uint64_t> generations;
    for(uint32_t i=0;i<snapshot->meshes.size();++i) generations.push_back(GraphicsBackendTestAccess::Mesh(active,i)->GetGeometryGeneration());
    const auto firstTexture = active.GetOpticalMaterials().front().texture;
    auto reuse = active.CaptureModelResources();
    auto changed = library;
    requests.push_back({largeScene ? PrimitiveShape::Cube : PrimitiveShape::Plane,"",""});
    changed.Retain(requests);
    for(const auto& request : requests) Check(changed.Resolve(request.shape,request.mesh,request.texture));
    auto next = changed.SharedSnapshot();
    ModelInstance nextPose; Check(nextPose.Initialize(*next));
    PrimitiveRenderer prepared; prepared.SetModelInput(next.get(), &nextPose);
    Check(prepared.PrepareModel(context,reuse));
    const auto measured = prepared.GetInitializationProfile();
    Require(measured.meshesUploaded==1 && measured.meshesReused==snapshot->meshes.size(),"Unchanged geometry was uploaded again.");
    Require(measured.texturesCreated==0 && measured.texturesReused>0,"Existing images were decoded/uploaded again.");
    Require(measured.pipelineMs==0 && measured.selectionMs==0 && measured.meshletPipelineMs==0,"Model preparation recreated pipelines.");
    Check(test.queue.WaitForIdle()); active.CommitPreparedModel(prepared); prepared.Shutdown(); reuse.reset();
    for(uint32_t i=0;i<generations.size();++i)
        Require(GraphicsBackendTestAccess::Mesh(active,i)->GetGeometryGeneration()==generations[i],"GPU geometry generation changed on reuse.");
    const auto retainedTexture = active.GetOpticalMaterials().front().texture;
    Require(retainedTexture.index==firstTexture.index && retainedTexture.generation==firstTexture.generation &&
        retainedTexture.resourceClass==firstTexture.resourceClass,"Texture descriptor changed on reuse.");
    std::cout << "PASS: incremental " << (largeScene?"Sponza":"primitives") << " update: initial=" << initialProfile.totalMs
        << " ms; added mesh=" << measured.totalMs << " ms; uploaded/reused meshes=" << measured.meshesUploaded << "/" << measured.meshesReused
        << "; created/reused textures=" << measured.texturesCreated << "/" << measured.texturesReused << "\n";
    if(!largeScene) {
        // 先頭のモデルを削除し、材質番号が変わってもGPU Bufferの再転送がないことを確認する。
        const std::array retained{requests[1],requests[2]};
        changed.Retain(retained);
        for(const auto& request : retained) Check(changed.Resolve(request.shape,request.mesh,request.texture));
        auto compacted = changed.SharedSnapshot();
        ModelInstance compactPose; Check(compactPose.Initialize(*compacted));
        PrimitiveRenderer compact; compact.SetModelInput(compacted.get(),&compactPose);
        reuse=active.CaptureModelResources(); Check(compact.PrepareModel(context,reuse));
        Require(compact.GetInitializationProfile().meshesUploaded==0 && compact.GetInitializationProfile().texturesCreated==0,
            "Compacting material slots uploaded unchanged resources.");
        Check(test.queue.WaitForIdle()); active.CommitPreparedModel(compact); compact.Shutdown(); reuse.reset();
        Require(GraphicsBackendTestAccess::Mesh(active,0)->GetGeometryGeneration()==generations[1],"Compaction changed retained geometry.");
        Require(GraphicsBackendTestAccess::Mesh(active,0)->GetSubmeshes().front().materialSlot==0,"Compaction retained an old material offset.");
        Require(oldMesh.expired(),"Removing a model retained the old GPU mesh wrapper.");
        Require(test.factory.IsPersistentShaderResourceValid(firstTexture),"A shared texture descriptor was retired too early.");
        // 失敗した準備とキャンセルは、描画中の資源を変更しない。
        auto invalid = *compacted;
        invalid.materials.front().baseColorImageData={1,2,3}; invalid.materials.front().baseColorImageId=NextModelResourceIdentity();
        ModelInstance badPose; Check(badPose.Initialize(invalid));
        PrimitiveRenderer bad; bad.SetModelInput(&invalid,&badPose);
        reuse=active.CaptureModelResources(); Require(!bad.PrepareModel(context,reuse),"Broken image preparation succeeded."); bad.Shutdown();
        PrimitiveRenderer cancelled; cancelled.SetModelInput(compacted.get(),&compactPose);
        std::stop_source stop; stop.request_stop();
        Require(!cancelled.PrepareModel(context,reuse,stop.get_token()),"Cancelled preparation succeeded."); cancelled.Shutdown(); reuse.reset();
        Require(test.factory.IsPersistentShaderResourceValid(firstTexture),"Failed preparation damaged the active texture.");
        // プレビューと共有している資源は、メインビューから削除しても有効なまま残す。
        PrimitiveRenderer preview; preview.SetModelInput(compacted.get(),&compactPose);
        Check(preview.InitializeShared(context,active.CaptureModelResources()));
        Require(preview.GetInitializationProfile().meshesUploaded==0 && preview.GetInitializationProfile().texturesCreated==0,
            "Preview duplicated main-view GPU assets.");
        ModelAssetData empty; ModelInstance emptyPose; Check(emptyPose.Initialize(empty));
        PrimitiveRenderer cleared; cleared.SetModelInput(&empty,&emptyPose);
        Check(cleared.PrepareModel(context,active.CaptureModelResources()));
        Check(test.queue.WaitForIdle()); active.CommitPreparedModel(cleared); cleared.Shutdown();
        Require(test.factory.IsPersistentShaderResourceValid(firstTexture),"Clearing the main view invalidated the preview's image.");
        preview.Shutdown();
        Require(!test.factory.IsPersistentShaderResourceValid(firstTexture),"Unused shared image descriptor was not retired.");
    }
    Check(test.queue.WaitForIdle()); active.Shutdown();
    test.bindless.CollectGarbage(test.queue.GetCompletedFenceValue()); test.Validate();
}
}
