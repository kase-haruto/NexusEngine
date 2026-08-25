#include "ConstantBuffer.h"

// c++
#include <cstring>
#include <limits>

// directx
#include <d3d12.h>
#include <wrl/client.h>

// engine
#include "Foundation/Memory/Alignment.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidArgument = 1;
		constexpr int32_t kCreationFailed = 2;
		constexpr int32_t kMapFailed = 3;
		constexpr int32_t kInvalidWrite = 4;
	}

	class ConstantBuffer::Impl final {
	public:
		Microsoft::WRL::ComPtr<ID3D12Resource> resource; //< 全Frame sliceを保持するUpload Resource
		uint8_t* mappedData = nullptr; //< Resource寿命中維持するCPU書き込み先
		size_t dataSize = 0; //< 呼び出し側が利用できるsliceごとの論理byte数
		size_t alignedSliceSize = 0; //< CBV要件へ切り上げたslice間隔
		uint32_t frameCount = 0; //< 同時利用可能なFrame slice数
	};

	ConstantBuffer::ConstantBuffer() noexcept = default;
	ConstantBuffer::~ConstantBuffer() noexcept { Shutdown(); }

	/////////////////////////////////////////////////////////////////////////////////////////
	// Frameごとに分離されたpersistently mapped Upload Bufferを生成する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> ConstantBuffer::Initialize(
		void* const nativeDevice,
		const size_t dataSize,
		const uint32_t frameCount) {
		if(nativeDevice == nullptr || dataSize == 0 || frameCount == 0 || impl_) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "Constant buffer arguments or state are invalid."));
		}

		const auto alignedSliceSize = TryAlignUp(dataSize, kDataAlignment);
		if(!alignedSliceSize || *alignedSliceSize > (std::numeric_limits<size_t>::max)() / frameCount) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument, "Constant buffer size exceeds the addressable range."));
		}
		const size_t resourceSize = *alignedSliceSize * frameCount;

		// 完全なResourceとmappingが得られるまでローカル所有し、失敗時に半初期化状態を残さない。
		auto implementation = std::make_unique<Impl>();
		D3D12_HEAP_PROPERTIES heapProperties = {};
		heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = resourceSize;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		auto* const device = static_cast<ID3D12Device*>(nativeDevice);
		HRESULT result = device->CreateCommittedResource(
			&heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
			IID_PPV_ARGS(&implementation->resource));
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kCreationFailed, result, "Failed to create constant buffer upload resource."));
		}

		// Upload HeapはCPU書き込み専用として永続Mapする。readRangeを空にしてCPUが読み戻さないことをDriverへ伝える。
		void* mapped = nullptr;
		const D3D12_RANGE readRange { 0, 0 };
		result = implementation->resource->Map(0, &readRange, &mapped);
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kMapFailed, result, "Failed to map constant buffer upload resource."));
		}

		implementation->mappedData = static_cast<uint8_t*>(mapped);
		implementation->dataSize = dataSize;
		implementation->alignedSliceSize = *alignedSliceSize;
		implementation->frameCount = frameCount;
		// 未使用paddingを含めて0初期化し、小さいdataを書いた際にも過去Frameの値を残さない。
		std::memset(implementation->mappedData, 0, resourceSize);
		impl_ = std::move(implementation);
		return {};
	}

	Result<void> ConstantBuffer::Write(
		const uint32_t frameIndex,
		const std::span<const uint8_t> data) {
		if(!impl_ || frameIndex >= impl_->frameCount || data.size() > impl_->dataSize) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidWrite, "Constant buffer write range is invalid."));
		}

		uint8_t* const destination = impl_->mappedData + impl_->alignedSliceSize * frameIndex;
		if(!data.empty()) {
			std::memcpy(destination, data.data(), data.size());
		}
		// 部分更新APIではなく一つの定数構造を書き換える契約なので、残りを0にして決定的な内容を保つ。
		if(data.size() < impl_->dataSize) {
			std::memset(destination + data.size(), 0, impl_->dataSize - data.size());
		}
		return {};
	}

	void ConstantBuffer::Shutdown() noexcept {
		if(!impl_) {
			return;
		}
		if(impl_->resource && impl_->mappedData != nullptr) {
			// persistent mappingはResource解放前に一度だけ解除する。
			impl_->resource->Unmap(0, nullptr);
			impl_->mappedData = nullptr;
		}
		impl_.reset();
	}

	size_t ConstantBuffer::GetDataSize() const noexcept { return impl_ ? impl_->dataSize : 0; }
	size_t ConstantBuffer::GetAlignedSliceSize() const noexcept { return impl_ ? impl_->alignedSliceSize : 0; }
	uint32_t ConstantBuffer::GetFrameCount() const noexcept { return impl_ ? impl_->frameCount : 0; }
	uint64_t ConstantBuffer::GetGpuAddress(const uint32_t frameIndex) const noexcept {
		if(!impl_ || frameIndex >= impl_->frameCount) {
			return 0;
		}
		return impl_->resource->GetGPUVirtualAddress() + impl_->alignedSliceSize * frameIndex;
	}
} // namespace NexusEngine
