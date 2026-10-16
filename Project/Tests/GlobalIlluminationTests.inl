// 通常の材質・モデル転送を使い、画面外の発光面と閉じた壁を持つ検証用室内を生成する。
ModelAssetData MakeGITestRoom(uint32_t variant) {
    if(variant==5) {
        std::ofstream hdr("Generated/GIWhite.hdr",std::ios::binary); hdr << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4 +X 8\n";
        const uint8_t white[4]={128,128,128,129}; for(uint32_t i=0;i<32;++i) hdr.write(reinterpret_cast<const char*>(white),4);
    }
    ModelAssetData asset; asset.materials.resize(4);
    asset.materials[0].baseColorFactor={.5f,.5f,.5f,1};
    asset.materials[1].baseColorFactor={.9f,.02f,.02f,1};
    asset.materials[2].baseColorFactor={.35f,.35f,.35f,1};
    asset.materials[3].baseColorFactor={.5f,.5f,.5f,1};
    if(variant==2) asset.materials[1].emissiveFactor={2,0,0};
    if(variant==3) asset.materials[3].emissiveFactor={2,2,2};
    MeshAssetData mesh; mesh.vertexStride=sizeof(ModelVertex); mesh.indexFormat=IndexFormat::UInt16;
    std::vector<ModelVertex> vertices; std::vector<uint16_t> indices;
    auto quad=[&](std::array<std::array<float,3>,4> points,std::array<float,3> normal,uint32_t material) {
        uint16_t base=static_cast<uint16_t>(vertices.size()); uint32_t first=static_cast<uint32_t>(indices.size());
        for(const auto& point:points) { ModelVertex v; std::copy(point.begin(),point.end(),v.position); std::copy(normal.begin(),normal.end(),v.normal); vertices.push_back(v); }
        for(int i:{0,1,2,0,2,3}) indices.push_back(static_cast<uint16_t>(base+i));
        mesh.submeshes.push_back({first,6,0,material});
    };
    quad({{{-1,-1,.9f},{-1,1,.9f},{1,1,.9f},{1,-1,.9f}}},{0,0,-1},0);
    quad({{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1}}},{0,0,1},2);
    quad({{{-1,-1,-1},{-1,1,-1},{-1,1,.9f},{-1,-1,.9f}}},{1,0,0},1);
    quad({{{1,-1,-1},{1,-1,.9f},{1,1,.9f},{1,1,-1}}},{-1,0,0},2);
    quad({{{-1,-1,-1},{-1,-1,.9f},{1,-1,.9f},{1,-1,-1}}},{0,1,0},2);
    quad({{{-1,1,-1},{1,1,-1},{1,1,.9f},{-1,1,.9f}}},{0,-1,0},3);
    const auto* vb=reinterpret_cast<const uint8_t*>(vertices.data()); mesh.vertexData.assign(vb,vb+vertices.size()*sizeof(ModelVertex));
    const auto* ib=reinterpret_cast<const uint8_t*>(indices.data()); mesh.indexData.assign(ib,ib+indices.size()*sizeof(uint16_t));
    asset.meshes.push_back(std::move(mesh)); ModelNodeAssetData node; node.meshIndices={0}; asset.nodes.push_back(node);
    return asset;
}

// GIの正しさを明るさだけで判断せず、閉空間の遮蔽・画面外発光・色の照り返しで検証する。
std::vector<uint8_t> ValidateGIIntegration(uint32_t variant,bool unsupported,RenderScene& scene,PrimitiveRenderer& renderer,
    auto&& render,Fixture& test,ShadowTexture& depth,const auto& rtv,TransientDescriptorArena& transient,uint32_t width,uint32_t height) {
    auto total=[](const auto& pixels,uint32_t channel) { uint64_t sum=0; for(size_t i=channel;i<pixels.size();i+=4) sum+=pixels[i]; return sum; };
    auto writeImage=[&](const std::filesystem::path& path,const auto& pixels) {
        std::ofstream file(path,std::ios::binary); file << "P6\n" << width << ' ' << height << "\n255\n";
        for(size_t i=0;i<pixels.size();i+=4) file.write(reinterpret_cast<const char*>(pixels.data()+i),3);
    };
    scene.globalIllumination.enabled=false;
    const auto before=render(ShadowMode::Auto,false);
    Require(!renderer.GetOptics().GetDiffuseGlobalIllumination().IsValid(),"Disabled GI exposed stale output.");
    scene.globalIllumination.enabled=true;
    if(variant<4) scene.environment.debugMode=LightingDebugMode::DiffuseGI;
    const auto on=render(ShadowMode::Auto,false);
    std::cout << "GI readback: variant=" << variant << " RGB=" << total(on,0) << ',' << total(on,1) << ',' << total(on,2) << '\n';
    writeImage(L"Generated/GIDiagnostic.ppm",on);
    if(unsupported) Require(!renderer.GetOptics().GetDiffuseGlobalIllumination().IsValid(),"Unsupported GPU exposed GI output.");
    else {
        Require(renderer.GetOptics().GetDiffuseGlobalIllumination().IsValid(),"GI output was not generated.");
        Require(render(ShadowMode::Auto,false)==on,"Static GI sampling changed across frames.");
        if(variant==1 || variant==5) {
            Require(total(before,0)>10000,"Closed-room baseline did not expose unoccluded ambient.");
            Require(total(on,0)+total(on,1)+total(on,2)==0,"Environment leaked into a closed, unlit room.");
        }
        if(variant==2) {
            Require(total(before,0)==0 && total(on,0)>10000,"Off-screen emissive surface did not illuminate the receiver.");
            Require(total(on,1)==0 && total(on,2)==0,"Red emitter produced non-red indirect light.");
        }
        if(variant==3) {
            scene.globalIllumination.maxBounces=1;
            const auto one=render(ShadowMode::Auto,false);
            Require(total(one,0)==total(one,1),"One-segment reference contains unexplained color bleeding.");
            Require(total(on,0)>total(on,1)+1000 && total(on,0)>=total(one,0),"Additional diffuse bounce did not transfer the red wall color.");
            scene.globalIllumination.maxBounces=2;
        }
        if(variant<4 || variant==5) {
            renderer.GetMeshletSettings().enabled=true;
            Require(renderer.AreMeshShadersAvailable(),"GI mesh shader unavailable.");
            Require(render(ShadowMode::Auto,false)==on,"Raster/mesh GI mismatch.");
            renderer.GetMeshletSettings().enabled=false;
            Require(render(ShadowMode::ShadowMap,true)==on,"Shadow Map altered GI output.");
            Require(render(ShadowMode::RayTracing,true)==on,"RT shadow altered GI output.");
            scene.globalIllumination.intensity=0;
            auto zero=render(ShadowMode::Auto,false);
            Require(total(zero,0)+total(zero,1)+total(zero,2)==0,"Zero GI intensity did not remove indirect diffuse light.");
            scene.globalIllumination.intensity=1;
        } else Require(before!=on,"GI did not coexist with mirror/glass rendering.");
    }
    const auto prefix=L"Generated/GI"+std::to_wstring(variant)+(unsupported?L"Fallback":L"");
    writeImage(prefix+L"Before.ppm",before); writeImage(prefix+L"After.ppm",on);
    Check(test.queue.WaitForIdle()); renderer.Shutdown(); test.shadows.ReleaseTexture(depth);
    Check(test.descriptors.RenderTargets().Free(*rtv)); transient.Shutdown(); test.Validate();
    std::cout << "Diffuse GI integration passed; variant=" << variant << " fallback=" << unsupported << '\n';
    return on;
}
