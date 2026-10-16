// 動く遮蔽物を個別Instanceとして扱うため、室内の6面を独立Nodeへ分離する。
ModelAssetData MakeDDGITestRoom(uint32_t variant) {
    auto asset=MakeGITestRoom(variant);
    auto source=std::move(asset.meshes.front());asset.meshes.clear();asset.nodes.clear();
    for(const auto& sub:source.submeshes) {
        auto mesh=source;mesh.submeshes={sub};
        ModelNodeAssetData node;node.meshIndices={static_cast<uint32_t>(asset.meshes.size())};
        asset.meshes.push_back(std::move(mesh));asset.nodes.push_back(std::move(node));
    }
    return asset;
}
std::vector<uint8_t> ValidateDDGIIntegration(uint32_t variant,bool unsupported,RenderScene& scene,PrimitiveRenderer& renderer,
    auto&& render,Fixture& test,ShadowTexture& depth,const auto& rtv,TransientDescriptorArena& transient,uint32_t width,uint32_t height) {
    auto total=[](const auto& pixels,uint32_t c) {uint64_t sum=0;for(size_t n=c;n<pixels.size();n+=4) sum+=pixels[n];return sum;};
    auto& settings=scene.ddgi;
    Require(renderer.GetOptics().GetDDGIFrameCount()==0,"Discarded recording advanced DDGI history.");
    Require(!renderer.GetOptics().IsDDGIActive(),"Discarded recording exposed uninitialized DDGI history.");
    const auto cold=render(ShadowMode::Auto,false);
    if(unsupported) {
        Require(renderer.GetOptics().GetDDGIDrawData().spacingEnabled[3]==0,"Unsupported DDGI exposed stale probe resources.");
    } else {
        Require(renderer.GetOptics().GetDDGIDrawData().spacingEnabled[3]==1,"DDGI probe history was not generated.");
        for(uint32_t i=0;i<7;++i) render(ShadowMode::Auto,false);
        const auto lit=render(ShadowMode::Auto,false);
        std::cout << "DDGI variant=" << variant << " RGB=" << total(lit,0) << ',' << total(lit,1) << ',' << total(lit,2) << '\n';
        if(variant==1 || variant==5 || variant==6) Require(total(lit,0)+total(lit,1)+total(lit,2)==0,"Unlit closed-room DDGI leaked environment/direct light.");
        else Require(total(lit,0)>10000,"DDGI did not illuminate the receiver.");
        if(variant==2) Require(total(lit,1)==0 && total(lit,2)==0,"Red emitter generated non-red DDGI.");
        if(variant==3) {
            settings.feedback=false;settings.freeze=false;render(ShadowMode::Auto,false);
            const auto one=render(ShadowMode::Auto,false);
            Require(total(one,0)==total(one,1),"Single-bounce DDGI reference has unexplained color bleeding.");
            settings.feedback=true;for(uint32_t n=0;n<12;++n) render(ShadowMode::Auto,false);
            const auto multiple=render(ShadowMode::Auto,false);
            Require(total(multiple,0)>total(multiple,1)+1000,"DDGI feedback did not propagate the red wall bounce.");
        }
        if(variant==5) {
            // 天井を移動して太陽光を入れ、AS更新・照明追従を再ベイク無しで検証する。
            scene.primitives[5].worldMatrix.At(3,0)=4;
            for(uint32_t n=0;n<16;++n) render(ShadowMode::Auto,false);
            const auto open=render(ShadowMode::Auto,false);const auto bright=total(open,0);
            Require(bright>1000,"Moving roof did not update DDGI illumination.");
            scene.primitives[5].worldMatrix=Matrix4x4::Identity();
            for(uint32_t n=0;n<24;++n) render(ShadowMode::Auto,false);
            const auto closed=render(ShadowMode::Auto,false);
            Require(total(closed,0)<bright/4,"DDGI failed to converge after closing the roof.");
            scene.primitives[5].worldMatrix.At(3,0)=4;
            scene.directionalLight.intensity=0;
            for(uint32_t n=0;n<12;++n) render(ShadowMode::Auto,false);
            const auto dark=render(ShadowMode::Auto,false);
            Require(total(dark,0)<bright/4,"Changing light intensity did not update DDGI.");
        }
        settings.freeze=true;
        const auto stable=render(ShadowMode::Auto,false);const auto frame=renderer.GetOptics().GetDDGIFrameCount();
        Require(render(ShadowMode::Auto,false)==stable && renderer.GetOptics().GetDDGIFrameCount()==frame,"Frozen DDGI changed history or pixels.");
        if(test.factory.GetCapabilities().SupportsMeshShaders()) {
            Require(renderer.AreMeshShadersAvailable(),"DDGI Mesh Shader pipeline unavailable.");
            renderer.GetMeshletSettings().enabled=true;
            Require(render(ShadowMode::Auto,false)==stable,"Raster/Mesh DDGI mismatch.");renderer.GetMeshletSettings().enabled=false;
        }
        Require(render(ShadowMode::ShadowMap,true)==stable,"Shadow Map multiplied DDGI twice.");
        Require(render(ShadowMode::RayTracing,true)==stable,"RT Shadow multiplied DDGI twice.");
        Require(!renderer.IsGlobalIlluminationActive(),"DDGI dispatched the old per-pixel RTGI.");
        settings.intensity=0;const auto zero=render(ShadowMode::Auto,false);
        if(variant!=4) Require(total(zero,0)+total(zero,1)+total(zero,2)==0,"Zero DDGI intensity left diffuse GI.");settings.intensity=1;
        ++settings.revision;render(ShadowMode::Auto,false);
        Require(renderer.GetOptics().GetDDGIFrameCount()==1,"Reset did not discard DDGI history.");
        if(variant==2) {
            settings.counts={3,3,3};settings.spacing={.75f,.75f,.75f};settings.rotateRays=true;
            render(ShadowMode::Auto,false);
            Require(renderer.GetOptics().GetDDGIFrameCount()==1,"Grid resize did not reset history.");
            const auto resized=render(ShadowMode::Auto,false);
            Require(total(resized,0)>10000 && total(resized,1)==0,"Resized DDGI grid lost lighting.");
        }
        auto write=[&](const std::filesystem::path& path,const auto& pixels) {
            std::ofstream file(path,std::ios::binary);file << "P6\n" << width << ' ' << height << "\n255\n";
            for(size_t n=0;n<pixels.size();n+=4) file.write(reinterpret_cast<const char*>(pixels.data()+n),3);
        };
        write(L"Generated/DDGI"+std::to_wstring(variant)+L".ppm",stable);
    }
    settings.enabled=false;scene.globalIllumination.enabled=false;render(ShadowMode::Auto,false);
    Require(renderer.GetOptics().GetDDGIDrawData().spacingEnabled[3]==0,"Disabled DDGI retained an active draw reference.");
    Check(test.queue.WaitForIdle());renderer.Shutdown();test.shadows.ReleaseTexture(depth);
    Check(test.descriptors.RenderTargets().Free(*rtv));transient.Shutdown();test.Validate();
    std::cout << "DDGI integration passed; variant=" << variant << " unsupported=" << unsupported << '\n';return cold;
}
