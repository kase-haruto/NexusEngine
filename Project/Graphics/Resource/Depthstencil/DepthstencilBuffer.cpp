#include "DepthstencilBuffer.h"
#include "DepthstencilFormatDx12.h"

// directx
#include <d3d12.h>
#include <wrl/client.h>

// engine
#include "Graphics/Descriptor/DescriptorAllocator.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidArgument = 1;
		constexpr int32_t kAlreadyInitialized = 2;
		constexpr int32_t kDescriptorAllocationFailed = 3;
		constexpr int32_t kResourceCreationFailed = 4;

	}

	class DepthStencilBuffer::Impl final {
	public:
		/**
		 * \brief 既存DSV slotへ新しいDepth Texture Viewを構築する
		 * \note Resource作成成功後だけ所有状態を入れ替える
		 */
		[[nodiscard]] Result<void> Recreate(
			ID3D12Device* const device,
			const uint32_t newWidth,
			const uint32_t newHeight) {
			if(device == nullptr || dsvAllocator == nullptr || !dsvDescriptor.IsValid() ||
			   newWidth == 0 || newHeight == 0 || format == DepthStencilFormat::None) {
				return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument,
					"Depth stencil buffer arguments are invalid."));
			}

			const DXGI_FORMAT nativeFormat = ToNativeDepthStencilFormat(format);
			D3D12_RESOURCE_DESC resourceDesc = {};
			resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			resourceDesc.Width = newWidth;
			resourceDesc.Height = newHeight;
			resourceDesc.DepthOrArraySize = 1;
			resourceDesc.MipLevels = 1;
			resourceDesc.Format = nativeFormat;
			resourceDesc.SampleDesc.Count = 1;
			resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

			const D3D12_HEAP_PROPERTIES heapProperties {
				D3D12_HEAP_TYPE_DEFAULT,
				D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
				D3D12_MEMORY_POOL_UNKNOWN,
				0,
				0
			};
			D3D12_CLEAR_VALUE clearValue = {};
			clearValue.Format = nativeFormat;
			clearValue.DepthStencil.Depth = 1.0f;
			clearValue.DepthStencil.Stencil = 0;

			// 生成に成功するまで旧Resourceを保持し、Resize失敗時の半端な状態を避ける。
			Microsoft::WRL::ComPtr<ID3D12Resource> newResource;
			const HRESULT result = device->CreateCommittedResource(
				&heapProperties,
				D3D12_HEAP_FLAG_NONE,
				&resourceDesc,
				D3D12_RESOURCE_STATE_DEPTH_WRITE,
				&clearValue,
				IID_PPV_ARGS(&newResource));
			if(FAILED(result)) {
				return std::unexpected(MakeDirectXError(
					kResourceCreationFailed, result, "Failed to create depth stencil resource."));
			}

			D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc = {};
			viewDesc.Format = nativeFormat;
			viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			device->CreateDepthStencilView(
				newResource.Get(), &viewDesc, dsvAllocator->GetCpuHandle(dsvDescriptor));

			resource = std::move(newResource);
			width = newWidth;
			height = newHeight;
			return {};
		}

		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		DescriptorAllocator* dsvAllocator = nullptr;
		DescriptorHandle dsvDescriptor;
		uint32_t width = 0;
		uint32_t height = 0;
		DepthStencilFormat format = DepthStencilFormat::None;
	};

	DepthStencilBuffer::DepthStencilBuffer() noexcept = default;
	DepthStencilBuffer::~DepthStencilBuffer() noexcept { Shutdown(); }

	/////////////////////////////////////////////////////////////////////////////////////////
	// Depth Resourceと対応するDSV slotを一体として初期化する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> DepthStencilBuffer::Initialize(
		void* const nativeDevice,
		DescriptorAllocator* const dsvAllocator,
		const DepthStencilBufferDesc& desc) {
		if(impl_ || nativeDevice == nullptr || dsvAllocator == nullptr ||
		   desc.width == 0 || desc.height == 0 || desc.format == DepthStencilFormat::None) {
			return std::unexpected(Error(ErrorCategory::Graphics,
				impl_ ? kAlreadyInitialized : kInvalidArgument,
				"Depth stencil buffer initialization state or arguments are invalid."));
		}

		auto implementation = std::make_unique<Impl>();
		implementation->dsvAllocator = dsvAllocator;
		implementation->format = desc.format;
		auto descriptor = dsvAllocator->Allocate();
		if(!descriptor) {
			return std::unexpected(Error(ErrorCategory::Graphics, kDescriptorAllocationFailed,
				"Failed to allocate a depth stencil descriptor."));
		}
		implementation->dsvDescriptor = *descriptor;
		auto result = implementation->Recreate(
			static_cast<ID3D12Device*>(nativeDevice), desc.width, desc.height);
		if(!result) {
			static_cast<void>(dsvAllocator->Free(implementation->dsvDescriptor));
			return result;
		}

		impl_ = std::move(implementation);
		return {};
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// DSV slotの識別性を維持し、TextureとViewの内容だけを再生成する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> DepthStencilBuffer::Resize(
		void* const nativeDevice,
		const uint32_t width,
		const uint32_t height) {
		if(!impl_ || nativeDevice == nullptr) {
			return std::unexpected(Error(ErrorCategory::Graphics, kInvalidArgument,
				"Depth stencil buffer is not initialized."));
		}
		if(width == impl_->width && height == impl_->height) return {};
		return impl_->Recreate(static_cast<ID3D12Device*>(nativeDevice), width, height);
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// DescriptorManagerが破棄される前にDepth ResourceとDSV slotを返却する
	/////////////////////////////////////////////////////////////////////////////////////////
	void DepthStencilBuffer::Shutdown() noexcept {
		if(!impl_) return;
		impl_->resource.Reset();
		if(impl_->dsvAllocator != nullptr && impl_->dsvDescriptor.IsValid()) {
			static_cast<void>(impl_->dsvAllocator->Free(impl_->dsvDescriptor));
		}
		impl_.reset();
	}

	bool DepthStencilBuffer::IsInitialized() const noexcept { return impl_ && impl_->resource; }
	uint32_t DepthStencilBuffer::GetWidth() const noexcept { return impl_ ? impl_->width : 0; }
	uint32_t DepthStencilBuffer::GetHeight() const noexcept { return impl_ ? impl_->height : 0; }
	DepthStencilFormat DepthStencilBuffer::GetFormat() const noexcept {
		return impl_ ? impl_->format : DepthStencilFormat::None;
	}
	uint64_t DepthStencilBuffer::GetNativeDsvHandle() const noexcept {
		return impl_ && impl_->dsvAllocator != nullptr
			? impl_->dsvAllocator->GetCpuHandle(impl_->dsvDescriptor).ptr
			: 0;
	}

} // namespace NexusEngine
