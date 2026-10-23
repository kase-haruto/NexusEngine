// 保存形式だけでなく、遮蔽・色の照り返し・独立UVをCPUで検証する。
void BakedLightingCpuTests() {
    {
        auto room=MakeGITestRoom(1);ModelInstance roomPose;Check(roomPose.Initialize(room));RenderScene closed;
        closed.primitives.push_back({{1},Matrix4x4::Identity(),{1,1,1,1}});closed.directionalLight.intensity=0;closed.ambientIrradiance={1,1,1};
        BakedLightingSettings quality;quality.samples=32;quality.probeCounts={3,3,3};
        auto dark=LightBaker{}.Bake(room,roomPose,closed,quality);Check(dark);
        for(const auto& p:dark->lightmap) Require(p[0]+p[1]+p[2]==0,"CPU bake leaked environment at a chart boundary.");
        // Source材質は無発光のまま、Entityの天井Slotだけを発光させて間接光を得る。
        auto& material = closed.primitives.front().material;
        material.enabled=true; material.slot=3; material.emissive={2,0,0};
        auto lit=LightBaker{}.Bake(room,roomPose,closed,quality);Check(lit);
        double red=0,green=0;
        for(const auto& p:lit->lightmap) {red+=p[0];green+=p[1];}
        Require(red>1 && green==0,"CPU bake ignored per-instance material emission.");
        Require(room.materials[3].emissiveFactor[0]==0,"CPU bake modified shared source material.");
    }
    auto asset=MakeGITestRoom(3);ModelInstance pose;Check(pose.Initialize(asset));
    RenderScene scene;scene.primitives.push_back({{1},Matrix4x4::Identity(),{1,1,1,1}});
    scene.directionalLight.intensity=0;scene.ambientIrradiance={0,0,0};
    BakedLightingSettings settings;settings.samples=128;settings.probeCounts={3,3,3};
    auto result=LightBaker{}.Bake(asset,pose,scene,settings);Check(result);
    Require(result->bindings.size()==1 && result->lightmap.size()==size_t(result->atlasSize)*result->atlasSize,"Missing atlas/binding.");
    double red=0,green=0;for(const auto& pixel:result->lightmap) {red+=pixel[0];green+=pixel[1];}
    Require(red>green+.1 && green>0,"Baked indirect transport did not transfer the red wall color.");
    auto prepared=asset;Check(GenerateLightmapGeometry(prepared));
    Require(prepared.meshes[0].vertexData.size()==12*3*sizeof(ModelVertex),"Independent lightmap vertices missing.");
    const auto path=L"Generated/BakedCpu.nxgi";Check(SaveBakedLighting(*result,path));
    auto loaded=LoadBakedLighting(path);Check(loaded);Require(loaded->lightmap==result->lightmap && loaded->probes==result->probes,"Cache roundtrip changed lighting.");
    const auto signature=LightBaker::Signature(asset,pose,scene);scene.directionalLight.intensity=1;
    Require(LightBaker::Signature(asset,pose,scene)!=signature,"Lighting change did not invalidate cache signature.");
    {std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out);file.seekp(80);char corrupt=127;file.write(&corrupt,1);}
    Require(!LoadBakedLighting(path),"Corrupted bake cache accepted.");
    {std::ofstream file(L"Generated/BakedTruncated.nxgi",std::ios::binary);file << "NXGI0001";}
    Require(!LoadBakedLighting(L"Generated/BakedTruncated.nxgi"),"Truncated bake cache accepted.");
    settings.samples=0;Require(!LightBaker{}.Bake(asset,pose,scene,settings),"Invalid bake settings accepted.");
    std::cout << "Baked CPU transport / UV / cache validation passed.\n";
}

std::vector<uint8_t> ValidateBakedIntegration(uint32_t variant,RenderScene& scene,PrimitiveRenderer& renderer,
    auto&& render,Fixture& test,ShadowTexture& depth,const auto& rtv,TransientDescriptorArena& transient,uint32_t width,uint32_t height) {
    auto total=[](const auto& pixels,uint32_t channel) {uint64_t sum=0;for(size_t i=channel;i<pixels.size();i+=4) sum+=pixels[i];return sum;};
    auto& settings=scene.bakedLighting;settings.enabled=false;
    const auto before=render(ShadowMode::Auto,false);
    Check(renderer.BakeLighting(settings));settings.enabled=true;++settings.revision;
    scene.globalIllumination.enabled=true;
    scene.environment.debugMode=LightingDebugMode::DiffuseGI;
    const auto on=render(ShadowMode::Auto,false);
    Require(renderer.GetBakedLightingStatus().starts_with("Loaded:"),"Baked GPU resources not loaded.");
    Require(!renderer.IsGlobalIlluminationActive(),"Baked mode dispatched realtime GI.");
    Require(render(ShadowMode::Auto,false)==on,"Baked lighting changed across frames.");
    if(variant==4) {
        // 最終合成でDXR交差面もプローブを参照でき、鏡・ガラスの既存経路が維持されることを確認する。
        scene.environment.debugMode=LightingDebugMode::Final;
        const auto optical=render(ShadowMode::Auto,false);
        Require(total(optical,0)>10000 && render(ShadowMode::Auto,false)==optical,"Baked probes broke mirror/glass final rendering.");
        scene.environment.debugMode=LightingDebugMode::DiffuseGI;
    }
    std::cout << "Baked readback variant=" << variant << " RGB=" << total(on,0) << ',' << total(on,1) << ',' << total(on,2) << '\n';
    if(variant==1) Require(total(on,0)+total(on,1)+total(on,2)==0,"Environment leaked through baked closed room.");
    else Require(total(on,0)>10000,"Baked indirect light missing.");
    if(variant==2) Require(total(on,1)==0 && total(on,2)==0,"Red bake produced non-red indirect light.");
    if(variant==3) Require(total(on,0)>total(on,1)+500,"Baked light did not transfer wall color.");
    if(test.factory.GetCapabilities().SupportsMeshShaders()) {
        Require(renderer.AreMeshShadersAvailable(),"Baked Mesh Shader pipeline failed to initialize.");
        renderer.GetMeshletSettings().enabled=true;
        Require(render(ShadowMode::Auto,false)==on,"Baked Raster/Mesh Shader mismatch.");
        renderer.GetMeshletSettings().enabled=false;
    }
    Require(render(ShadowMode::ShadowMap,true)==on,"Shadow Map multiplied baked lighting.");
    Require(render(ShadowMode::RayTracing,true)==on,"RT Shadow multiplied baked lighting.");
    if(variant<=3) {
        // 同じ受光形状を動的扱いにして、ライトマップではなくプローブ補間の経路を通す。
        scene.primitives[0].staticLighting=false;
        const auto probes=render(ShadowMode::Auto,false);
        if(variant==1) Require(total(probes,0)+total(probes,1)+total(probes,2)==0,"Probe lighting leaked through closed room.");
        else Require(total(probes,0)>1000,"Moving receiver did not sample light probes.");
        Require(render(ShadowMode::Auto,false)==probes,"Probe lighting changed across frames.");
        scene.primitives[0].staticLighting=true;
    }
    settings.intensity=0;const auto zero=render(ShadowMode::Auto,false);
    Require(total(zero,0)+total(zero,1)+total(zero,2)==0,"Zero baked intensity left GI enabled.");settings.intensity=1;
    // 失敗した再ロードでは直前の有効な資源を保ち、次の正常ロードで置換する。
    const auto originalPath=settings.cachePath;settings.cachePath=L"Generated/NoSuchBake.nxgi";++settings.revision;
    Require(render(ShadowMode::Auto,false)==on,"Failed reload discarded valid baked lighting.");
    settings.cachePath=originalPath;++settings.revision;
    Require(render(ShadowMode::Auto,false)==on,"Reload changed baked lighting.");
    scene.directionalLight.intensity+=1;++settings.revision;
    Require(render(ShadowMode::Auto,false)==on && renderer.GetBakedLightingStatus().find("stale")!=std::string::npos,"Stale cache was not diagnosed.");
    scene.directionalLight.intensity-=1;
    auto write=[&](const std::filesystem::path& path,const auto& pixels) {
        std::ofstream file(path,std::ios::binary);file << "P6\n" << width << ' ' << height << "\n255\n";
        for(size_t p=0;p<pixels.size();p+=4) file.write(reinterpret_cast<const char*>(pixels.data()+p),3);
    };
    const auto prefix=L"Generated/BakedGI"+std::to_wstring(variant);
    write(prefix+L"Before.ppm",before);write(prefix+L"After.ppm",on);
    Check(test.queue.WaitForIdle());renderer.Shutdown();test.shadows.ReleaseTexture(depth);
    Check(test.descriptors.RenderTargets().Free(*rtv));transient.Shutdown();test.Validate();
    std::cout << "Baked GI / probe integration passed; variant=" << variant << '\n';return on;
}
