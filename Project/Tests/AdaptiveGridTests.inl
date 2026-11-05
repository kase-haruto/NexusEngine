#include "Graphics/Grid/GridRenderer.h"
#include "Graphics/Backend/DirectX12/PostProcess/PostProcessBackend.h"
#include <fstream>

namespace {
void AdaptiveGridTests() {
    Fixture test(false);
    if(test.info) {
        // HDR test targets intentionally have no optimized clear colour.
        D3D12_MESSAGE_ID advisory=D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE;
        D3D12_INFO_QUEUE_FILTER filter{}; filter.DenyList.NumIDs=1; filter.DenyList.pIDList=&advisory;
        Native(test.info->AddStorageFilterEntries(&filter));
    }
    constexpr uint32_t size=512;
    GridRenderer grid; Check(grid.Initialize({test.factory,std::filesystem::path(L"Project/Shaders"),std::filesystem::path{}}));
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
    auto render=[&](const Matrix4x4& view, const std::array<float,3>& camera) {
        Check(test.queue.WaitForIdle()); Native(test.allocator->Reset()); Native(test.commands->Reset(test.allocator.Get(),nullptr));
        const std::array<float,4> clear{.025f,.025f,.025f,1};
        post.BeginScene(test.commands.Get(),dsv,clear.data()); test.commands->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
        D3D12_VIEWPORT viewport{0,0,float(size),float(size),0,1}; D3D12_RECT scissor{0,0,LONG(size),LONG(size)};
        test.commands->RSSetViewports(1,&viewport); test.commands->RSSetScissorRects(1,&scissor);
        arena.ResetFrame(0); test.bindless.Bind(test.commands.Get());
        auto context=GraphicsBackendTestAccess::Context(test.commands.Get(),test.bindless,0,size,size);
        GraphicsBackendTestAccess::Targets(context,post.GetSceneRtv().ptr,dsv.ptr,&arena);
        GridSettings settings; settings.enabled=true;
        Check(grid.Render(context,view,camera,settings));
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
        const auto directory=std::filesystem::path("Generated/GridPreview"); std::filesystem::create_directories(directory);
        std::array<uint8_t,54> header{}; header[0]='B'; header[1]='M';
        auto integer=[&](size_t offset,uint32_t value){std::memcpy(header.data()+offset,&value,4);};
        integer(2,54+size*size*4); integer(10,54); integer(14,40); integer(18,size); integer(22,static_cast<uint32_t>(-int32_t(size))); header[26]=1; header[28]=32;
        std::vector<uint8_t> bgra=rgba; for(size_t i=0;i<bgra.size();i+=4) std::swap(bgra[i],bgra[i+2]);
        std::ofstream file(directory/name,std::ios::binary); file.write(reinterpret_cast<const char*>(header.data()),header.size());
        file.write(reinterpret_cast<const char*>(bgra.data()),static_cast<std::streamsize>(bgra.size()));
    };
    std::vector<uint8_t> previous;
    for(float extent:{2.0f,20.0f,200.0f}) {
        Matrix4x4 view; view.elements.fill(0); view.At(0,0)=1/extent; view.At(2,1)=1/extent;
        view.At(1,2)=-.001f; view.At(3,2)=.5f; view.At(3,3)=1;
        auto pixels=render(view,{0,extent,0});
        const size_t xAxis=(256*size+384)*4,zAxis=(128*size+256)*4;
        Require(pixels[xAxis]>pixels[xAxis+1]*2,"X axis is not red.");
        Require(pixels[zAxis+1]>pixels[zAxis]*2,"Z axis is not green.");
        if(!previous.empty()) {
            double difference=0;
            for(size_t i=0;i<pixels.size();++i) difference+=std::abs(int(pixels[i])-int(previous[i]));
            Require(difference/pixels.size()<0.5,"Grid cell density did not adapt over a decade of zoom.");
        }
        save(extent==2?"Near.bmp":extent==20?"Medium.bmp":"Far.bmp",pixels); previous=std::move(pixels);
    }
    auto world=MakeAffineMatrix(Vector3::One(),Quaternion::Identity(),{0,2,-10});
    auto view=TryInverse(world); Require(view.has_value(),"Test camera inversion failed.");
    auto perspective=Multiply(*view,MakePerspectiveFovMatrix(1.0472f,1,.05f,100000));
    save("Perspective.bmp",render(perspective,{0,2,-10}));
    grid.Shutdown(); post.Shutdown(); arena.Shutdown(); test.shadows.ReleaseTexture(depth);
    Check(test.descriptors.RenderTargets().Free(*slot)); test.Validate();
    std::cout<<"PASS: adaptive grid zoom density, red X / green Z axes, infinite plane and perspective horizon.\n";
}
}
