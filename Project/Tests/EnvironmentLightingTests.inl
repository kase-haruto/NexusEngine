#include "Graphics/Environment/EnvironmentLighting.h"
#include "Graphics/Resource/HdrImage.h"
#include "Graphics/Shader/ShaderCompiler.h"
namespace {
// Generated deterministic panoramas avoid network assets and allow numerical assertions.
void WriteEnvironmentFixture(const std::filesystem::path& path, bool directional, bool rle = false) {
    std::ofstream file(path,std::ios::binary);
    file << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 16 +X 32\n";
    for(uint32_t y=0;y<16;++y) {
        std::array<std::array<uint8_t,4>,32> row;
        for(uint32_t x=0;x<32;++x) {
            // Uniform radiance (2,1,.5); directional fixture has a bright +X patch.
            row[x]=directional ? std::array<uint8_t,4>{8,8,8,128} : std::array<uint8_t,4>{128,64,32,130};
            if(directional && x>=14 && x<=17 && y>=6 && y<=9) row[x]={192,96,48,132};
        }
        if(rle) {
            const uint8_t header[4]={2,2,0,32}; file.write(reinterpret_cast<const char*>(header),4);
            for(uint32_t c=0;c<4;++c) { const uint8_t count=32; file.write(reinterpret_cast<const char*>(&count),1); for(const auto& pixel:row) file.put(static_cast<char>(pixel[c])); }
        } else file.write(reinterpret_cast<const char*>(row.data()),sizeof(row));
    }
}
void EnvironmentLightingIntegration() {
    const std::filesystem::path uniform=L"Generated/IBLUniform.hdr", directional=L"Generated/IBLDirectional.hdr";
    WriteEnvironmentFixture(uniform,false); WriteEnvironmentFixture(directional,true,true);
    auto decoded=LoadHdrImage(uniform); Check(decoded);
    Require(decoded->pixels[0][0]==2 && decoded->pixels[0][1]==1 && decoded->pixels[0][2]==0.5f,"RGBE decode lost HDR values.");
    Require(!LoadHdrImage(L"Generated/NoSuchEnvironment.hdr"),"Missing HDR image accepted.");
    // Exercise actual glTF import rather than only testing the material helper formulas.
    {
        const float positions[9]={0,0,0,1,0,0,0,1,0};
        std::ofstream mesh("Generated/IBLMaterial.bin",std::ios::binary); mesh.write(reinterpret_cast<const char*>(positions),sizeof(positions)); mesh.close();
        std::array<uint8_t,58> bmp {}; bmp[0]='B'; bmp[1]='M'; bmp[2]=58; bmp[10]=54; bmp[14]=40;
        bmp[18]=1; bmp[22]=1; bmp[26]=1; bmp[28]=24; bmp[54]=255; bmp[55]=128; bmp[56]=64;
        std::ofstream image("Generated/IBLMaterial.bmp",std::ios::binary); image.write(reinterpret_cast<const char*>(bmp.data()),bmp.size()); image.close();
        std::ofstream gltf("Generated/IBLMaterial.gltf");
        gltf << R"({"asset":{"version":"2.0"},"buffers":[{"uri":"IBLMaterial.bin","byteLength":36}],
            "bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],
            "images":[{"uri":"IBLMaterial.bmp"}],"textures":[{"source":0}],
            "extensionsUsed":["KHR_materials_emissive_strength"],
            "materials":[{"emissiveFactor":[0.5,0.25,0.125],"emissiveTexture":{"index":0},
                "occlusionTexture":{"index":0,"strength":0.6},"extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":4}}}],
            "meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],"nodes":[{"mesh":0}]})";
        gltf.close(); auto imported=ModelLoader{}.LoadGltf(L"Generated/IBLMaterial.gltf"); Check(imported);
        const auto& material=imported->materials[0];
        Require(material.emissiveFactor==std::array<float,3>{2,1,.5f} && std::abs(material.occlusionStrength-.6f)<1e-6f,
            "glTF emissive factor/strength or occlusion strength import failed.");
        Require(material.emissiveImageData.size()==58 && material.occlusionImageData.size()==58,"glTF AO/emissive image import failed.");
    }
    Fixture test(false);
    EnvironmentLighting lighting; Check(lighting.Initialize(test.factory,L"Project/Shaders"));
    EnvironmentLightingData settings; settings.imagePath=uniform;
    Check(lighting.Prepare(settings)); auto original=lighting.GetDrawData(settings);
    Require(original.options[1]!=0,"IBL did not publish generated textures.");
    Check(lighting.Prepare(settings)); Require(lighting.GetDrawData(settings).textures==original.textures,"Unchanged environment was rebuilt.");
    D3D12_ROOT_PARAMETER parameters[2] {};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; parameters[0].Constants={0,0,12};
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV; parameters[1].Descriptor.ShaderRegister=0;
    D3D12_ROOT_SIGNATURE_DESC rd {2,parameters,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED|D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED};
    ComPtr<ID3DBlob> serialized,errors; Native(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors));
    ComPtr<ID3D12RootSignature> root; Native(test.device.GetDevice()->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root)));
    auto code=ShaderCompiler{}.Compile({L"Project/Tests/EnvironmentLightingTest.hlsl",L"main",L"cs_6_6"}); Check(code);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd {}; pd.pRootSignature=root.Get(); pd.CS={code->data(),code->size()};
    ComPtr<ID3D12PipelineState> pipeline; Native(test.device.GetDevice()->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pipeline)));
    D3D12_RESOURCE_DESC bd {}; bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width=64*16; bd.Height=1; bd.DepthOrArraySize=1; bd.MipLevels=1;
    bd.SampleDesc.Count=1; bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; bd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES hp {}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> output,readback; Native(test.device.GetDevice()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&output)));
    hp.Type=D3D12_HEAP_TYPE_READBACK; bd.Flags=D3D12_RESOURCE_FLAG_NONE;
    Native(test.device.GetDevice()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    auto evaluate=[&]() {
        Native(test.allocator->Reset()); Native(test.commands->Reset(test.allocator.Get(),nullptr));
        auto data=lighting.GetDrawData(settings); test.bindless.Bind(test.commands.Get());
        test.commands->SetComputeRootSignature(root.Get()); test.commands->SetPipelineState(pipeline.Get());
        test.commands->SetComputeRoot32BitConstants(0,12,&data,0); test.commands->SetComputeRootUnorderedAccessView(1,output->GetGPUVirtualAddress());
        D3D12_RESOURCE_BARRIER begin {}; begin.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        begin.Transition={output.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
        test.commands->ResourceBarrier(1,&begin); test.commands->Dispatch(1,1,1);
        D3D12_RESOURCE_BARRIER b {}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={output.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};
        test.commands->ResourceBarrier(1,&b); test.commands->CopyResource(readback.Get(),output.Get());
        b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE; b.Transition.StateAfter=D3D12_RESOURCE_STATE_COMMON; test.commands->ResourceBarrier(1,&b);
        Native(test.commands->Close()); test.queue.Execute(test.commands.Get()); Check(test.queue.WaitForIdle());
        std::array<std::array<float,4>,64> values; void* mapped=nullptr; Native(readback->Map(0,nullptr,&mapped));
        std::memcpy(values.data(),mapped,sizeof(values)); readback->Unmap(0,nullptr);
        for(auto value:values) for(float c:value) Require(std::isfinite(c),"Non-finite IBL/BRDF output.");
        return values;
    };
    auto values=evaluate();
    for(uint32_t i=0;i<21;++i) for(uint32_t c=0;c<3;++c)
        Require(std::abs(values[i][c]-decoded->pixels[0][c])<0.025f,"Uniform IBL changed radiance or irradiance pi normalization.");
    Require(values[21][0]>0.8f && values[21][0]<1.05f && values[21][1]>=0 && values[21][1]<0.02f,"BRDF LUT integration outside expected range.");
    Require(values[22][0]==0 && values[23][0]>0.01f,"Lambert/legacy geometric term incorrect.");
    Require(values[24][0]==2 && values[24][1]==3 && values[24][2]==4,"Emissive was clamped or attenuated.");
    Require(values[25][0]<1e-5f && values[26][0]>0.1f,"AO did not affect only indirect lighting.");
    Require(values[27]==values[28],"AO affected direct lighting.");
    Require(std::abs(values[29][0]-.2f)<1e-5f && std::abs(values[29][1]-.35f)<1e-5f && std::abs(values[30][0]-.6f)<1e-5f,"glTF MR/AO channel rules incorrect.");
    Require(values[31][0]==0,"Black metal received diffuse irradiance.");
    Require(values[32][0]>.8f && values[32][0]>values[32][1] && values[32][1]>values[32][2] && values[32][3]==0,
        "Fallback sky lost tinted metallic reflection or introduced metal diffuse.");
    Require(values[33][0]<values[32][0] && values[33][0]>.3f,"Fallback sky ignored roughness.");
    Require(values[34][0]==0 && values[35][0]==0,"Fallback reflection ignored AO or zero sky intensity.");
    settings.imagePath=directional; Check(lighting.Prepare(settings));
    auto next=lighting.GetDrawData(settings);
    Require(next.textures[3]==original.textures[3],"BRDF LUT was regenerated on reload.");
    for(uint32_t i=0;i<4;++i) {
        ++settings.revision; Check(lighting.Prepare(settings));
        auto current=lighting.GetDrawData(settings);
        Require(current.textures[3]==original.textures[3],"Repeated reload replaced the LUT.");
        for(uint32_t slot=0;slot<3;++slot) Require(current.textures[slot]<next.textures[0]+128,"IBL descriptor allocation grew across reloads.");
    }
    values=evaluate(); Require(values[0][0]>values[1][0]+2,"HDR cube direction convention incorrect.");
    Require(values[12][0]>values[20][0]+1,"Roughness mip chain did not blur reflection.");
    settings.rotation=3.14159265359f; auto rotated=evaluate();
    Require(rotated[1][0]>rotated[0][0]+2,"Environment yaw failed to rotate reflection.");
    settings.intensity=0.5f; auto dimmed=evaluate();
    Require(std::abs(dimmed[1][0]*2-rotated[1][0])<0.01f,"Environment intensity is not linear.");
    auto stable=lighting.GetDrawData(settings); settings.imagePath=L"Generated/NoSuchEnvironment.hdr";
    Require(!lighting.Prepare(settings),"Failed reload not reported.");
    Require(lighting.GetDrawData(settings).textures==stable.textures,"Failed reload destroyed previous valid environment.");
    Check(lighting.Prepare(settings)); settings.enabled=false;
    Require(lighting.GetDrawData(settings).options[1]==0,"IBL disable ignored.");
    Check(test.queue.WaitForIdle()); lighting.Shutdown(); test.Validate();
    std::cout << "IBL HDR/raw/RLE, cube/irradiance/prefilter/LUT, rotation/intensity, AO/emissive, reload/lifetime passed.\n";
}
}
