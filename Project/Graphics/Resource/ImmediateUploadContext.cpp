#include "ImmediateUploadContext.h"

// c++
#include <utility>

// engine
#include "Graphics/Core/CommandQueue.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidArgument			 = 1;
		constexpr int32_t kAllocatorCreationFailed	 = 2;
		constexpr int32_t kCommandListCreationFailed = 3;
		constexpr int32_t kCommandListCloseFailed	 = 4;
	} // namespace

	Result<void> ImmediateUploadContext::Initialize(ID3D12Device* const device) {
		if(device == nullptr || allocator_ || commandList_) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "Immediate upload context arguments or state are invalid."));
		}
		HRESULT result = device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_));
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kAllocatorCreationFailed, result, "Failed to create immediate upload command allocator."));
		}
		result = device->CreateCommandList(
			0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr,
			IID_PPV_ARGS(&commandList_));
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kCommandListCreationFailed, result, "Failed to create immediate upload command list."));
		}
		return {};
	}

	Result<void> ImmediateUploadContext::ExecuteAndWait(CommandQueue* const commandQueue) {
		if(commandQueue == nullptr || !commandList_) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "Immediate upload context is not ready for execution."));
		}
		const HRESULT result = commandList_->Close();
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kCommandListCloseFailed, result, "Failed to close immediate upload command list."));
		}

		commandQueue->Execute(commandList_.Get());
		auto signalResult = commandQueue->Signal();
		if(!signalResult) {
			return std::unexpected(std::move(signalResult.error()));
		}
		return commandQueue->Wait(*signalResult);
	}
} // namespace NexusEngine
