#include "Graphics/Sky/SkyRenderer.h"
#include "Graphics/Backend/DirectX12/PostProcess/PostProcessBackend.h"
#include <fstream>

namespace {
void SkyRenderingTests() {
    Fixture test(false);
    if(test.info) {
        // Test arbitrary foreground depths as well as HDR targets without optimized clear colours.
        D3D12_MESSAGE_ID advisories[]={D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
            D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE};
        D3D12_INFO_QUEUE_FILTER filter{}; filter.DenyList.NumIDs=2; filter.DenyList.pIDList=advisories;
        Native(test.info->AddStorageFilterEntries(&filter));
    }
    constexpr uint32_t size=512;
    SkyRenderer sky; Check(sky.Initialize({test.factory,std::filesystem::path(L"Project/Shaders"),std::filesystem::path{}}));
    PostProcessBackend post; Check(post.Initialize(test.device.GetDevice(),L"Project/Shaders",size,size));
    ShadowTexture depth; Check(test.shadows.CreateTexture(depth,size,size,true));
    auto dsv=test.descriptors.DepthStencils().GetCpuHandle(depth.dsv);
    auto slot=test.descriptors.RenderTargets().Allocate(); Check(slot);
    auto rtv=test.descriptors.RenderTargets().GetCpuHandle(*slot);
    D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=desc.Height=size; desc.DepthOrArraySize=desc.MipLevels=1; desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Resource> output;
    Native(test.device.GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&output)));
    test.device.GetDevice()->CreateRenderTargetView(output.Get(),nullptr,rtv);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; uint64_t bytes=0;
    test.device.GetDevice()->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
    heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width=bytes;
    buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1; buffer.SampleDesc.Count=1; buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    Native(test.device.GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    TransientDescriptorArena arena; Check(arena.Initialize(test.device.GetDevice(),&test.descriptors.Resources(),2,8));
    auto render=[&](const Matrix4x4& view, const Matrix4x4& projection, const EnvironmentDrawData& environment, float depthValue = 1.0f) {
        Check(test.queue.WaitForIdle()); Native(test.allocator->Reset()); Native(test.commands->Reset(test.allocator.Get(),nullptr));
        const std::array<float,4> clear{.025f,.025f,.025f,1};
        post.BeginScene(test.commands.Get(),dsv,clear.data()); test.commands->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,depthValue,0,0,nullptr);
        D3D12_VIEWPORT viewport{0,0,float(size),float(size),0,1}; D3D12_RECT scissor{0,0,LONG(size),LONG(size)};
        test.commands->RSSetViewports(1,&viewport); test.commands->RSSetScissorRects(1,&scissor);
        arena.ResetFrame(0); test.bindless.Bind(test.commands.Get());
        auto context=GraphicsBackendTestAccess::Context(test.commands.Get(),test.bindless,0,size,size);
        GraphicsBackendTestAccess::Targets(context,post.GetSceneRtv().ptr,dsv.ptr,&arena);

        Check(sky.Render(context,view,projection,environment));
        PostProcessSettings postSettings; postSettings.bloomEnabled=false; postSettings.antiAliasingEnabled=false; postSettings.filmicEnabled=false;
        post.Resolve(test.commands.Get(),rtv,postSettings);
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={output.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};
        test.commands->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION source{},destination{};
        source.pResource=output.Get(); source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.pResource=readback.Get(); destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; destination.PlacedFootprint=footprint;
        test.commands->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter); test.commands->ResourceBarrier(1,&barrier);
        Native(test.commands->Close()); test.queue.Execute(test.commands.Get()); Check(test.queue.WaitForIdle());
        void* mapped=nullptr; Native(readback->Map(0,nullptr,&mapped)); std::vector<uint8_t> pixels(size*size*4);
        for(uint32_t row=0;row<size;++row) std::memcpy(pixels.data()+row*size*4,static_cast<uint8_t*>(mapped)+footprint.Offset+row*footprint.Footprint.RowPitch,size*4);
        readback->Unmap(0,nullptr); return pixels;
    };
    auto save=[&](const char* name,const std::vector<uint8_t>& rgba) {
        const auto directory=std::filesystem::path("Generated/SkyPreview"); std::filesystem::create_directories(directory);
        std::array<uint8_t,54> header{}; header[0]='B'; header[1]='M';
        auto integer=[&](size_t offset,uint32_t value){std::memcpy(header.data()+offset,&value,4);};
        integer(2,54+size*size*4); integer(10,54); integer(14,40); integer(18,size); integer(22,static_cast<uint32_t>(-int32_t(size))); header[26]=1; header[28]=32;
        std::vector<uint8_t> bgra=rgba; for(size_t i=0;i<bgra.size();i+=4) std::swap(bgra[i],bgra[i+2]);
        std::ofstream file(directory/name,std::ios::binary); file.write(reinterpret_cast<const char*>(header.data()),header.size());
        file.write(reinterpret_cast<const char*>(bgra.data()),static_cast<std::streamsize>(bgra.size()));
    };
    auto projection=MakePerspectiveFovMatrix(1.0472f,1,.05f,100000);
    auto view=Matrix4x4{};
    auto pixels=render(view,projection,{});
    const size_t top=(32*size+size/2)*4, bottom=((size-32)*size+size/2)*4;
    Require(pixels[top+2]>pixels[top] && pixels[top+2]>pixels[top+1],"Default sky is not blue.");
    Require(pixels[top]!=pixels[bottom] || pixels[top+2]!=pixels[bottom+2],"Sky has no vertical gradient.");
    save("Default.bmp",pixels);
    view.At(3,0)=100000; view.At(3,1)=-5000; view.At(3,2)=20000;
    Require(render(view,projection,{})==pixels,"Camera translation moved the infinite sky.");
    auto world=MakeAffineMatrix(Vector3::One(),Quaternion::FromEulerRadians({0.8f,0,0}),Vector3::Zero());
    auto rotated=TryInverse(world); Require(rotated.has_value(),"Test view inversion failed.");
    Require(render(*rotated,projection,{})!=pixels,"Camera pitch did not change the sky.");
    auto occluded=render(view,projection,{},0.5f);
    Require(occluded[top]==occluded[bottom] && occluded[top+2]==occluded[bottom+2],"Sky overwrote foreground depth.");
    Require(occluded[top]!=pixels[top] || occluded[top+2]!=pixels[top+2],"Depth test did not reject sky.");
    EnvironmentLighting lighting; Check(lighting.Initialize(test.factory,L"Project/Shaders"));
    const auto hdr=std::filesystem::path(L"Generated/SkyPreview/Uniform.hdr");
    WriteEnvironmentFixture(hdr,false);
    EnvironmentLightingData settings; settings.imagePath=hdr; settings.intensity=0.25f;
    Check(lighting.Prepare(settings));
    auto environment=lighting.GetDrawData(settings);
    auto mapped=render(view,projection,environment);
    Require(mapped[top]>mapped[top+1] && mapped[top+1]>mapped[top+2],"HDR background did not sample its environment.");
    Require(mapped[top]==mapped[bottom] && mapped[top+2]==mapped[bottom+2],"Uniform HDR background is not uniform.");
    save("HDR.bmp",mapped);
    environment.parameters[0]=0.0625f;
    auto dim=render(view,projection,environment);
    Require(dim[top]<mapped[top],"HDR background ignores intensity.");
    settings = {};
    settings.atmosphere.enabled = true;
    settings.atmosphere.sunAzimuth = -90;
    auto noon = render(Matrix4x4{}, projection, lighting.GetDrawData(settings));
    save("Noon.bmp", noon);
    Require(noon[top+2] > noon[top], "Rayleigh daylight is not blue.");
    settings.atmosphere.timeOfDay = 18;
    auto sunset = render(Matrix4x4{}, projection, lighting.GetDrawData(settings));
    save("Sunset.bmp", sunset);
    const size_t horizon = (size/2*size+size/2)*4;
    Require(sunset[horizon] > sunset[horizon+2], "Sunset did not redden the horizon.");
    settings.atmosphere.timeOfDay = 0;
    auto night = render(Matrix4x4{}, projection, lighting.GetDrawData(settings));
    save("Night.bmp", night);
    Require(night[top+2] < noon[top+2] && night[horizon] < sunset[horizon], "Night did not darken the sky.");
    Require(render(view, projection, lighting.GetDrawData(settings)) == night, "Atmosphere moved with camera translation.");
    lighting.Shutdown();
    sky.Shutdown(); post.Shutdown(); arena.Shutdown(); test.shadows.ReleaseTexture(depth);
    Check(test.descriptors.RenderTargets().Free(*slot)); test.Validate();
    std::cout<<"PASS: default sky, translation invariance, camera pitch, foreground depth, HDR background and intensity.\n";
}
}
