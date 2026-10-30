void TransientDescriptorTests() {
    Fixture test(true);
    DescriptorAllocator allocator;
    Check(allocator.Initialize(test.device.GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 12, true));
    TransientDescriptorArena arena;
    Check(arena.Initialize(test.device.GetDevice(), &allocator, 2, 2));
    ConstantBuffer constants;
    Check(test.factory.CreateConstantBuffer(constants, 256));
    const auto allocate = [&](uint32_t frame) {
        return arena.CreateConstantBufferView(frame, constants.GetGpuAddress(frame), 256);
    };
    std::vector<uint64_t> handles;
    for(int i = 0; i < 8; ++i) {
        auto handle = allocate(0); Check(handle);
        Require(std::find(handles.begin(), handles.end(), handle->ptr) == handles.end(), "Growth reused an in-flight descriptor.");
        handles.push_back(handle->ptr);
    }
    for(int i = 0; i < 4; ++i) {
        auto handle = allocate(1); Check(handle);
        Require(std::find(handles.begin(), handles.end(), handle->ptr) == handles.end(), "Frame arenas overlap.");
        handles.push_back(handle->ptr);
    }
    Require(!allocate(0) && !allocate(1), "Heap exhaustion was not reported.");
    arena.ResetFrame(0);
    for(size_t i = 0; i < 8; ++i) {
        auto handle = allocate(0); Check(handle);
        Require(handle->ptr == handles[i], "Reset did not reuse retained pages.");
    }
    Require(!allocate(1), "Reset changed another frame's cursor.");
    arena.ResetFrame(1);
    for(size_t i = 8; i < handles.size(); ++i) {
        auto handle = allocate(1); Check(handle);
        Require(handle->ptr == handles[i], "Second frame pages were not reused.");
    }
    arena.Shutdown();
    auto wholeHeap = allocator.Allocate(12); Check(wholeHeap);
    Check(allocator.Free(*wholeHeap));
    allocator.Shutdown(); test.Validate();
    std::cout << "PASS: transient descriptor growth, frame isolation, exhaustion, reset and release\n";
}
