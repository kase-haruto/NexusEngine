#include "WicImageDecoder.h"

// c++
#include <limits>

// windows
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace NexusEngine {
	namespace {
		constexpr int32_t kInvalidImage = 1;
		constexpr int32_t kWicFailure = 2;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// WICを利用してencoded画像をRGBA8へdecodeする
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<DecodedImage> WicImageDecoder::Decode(const std::span<const uint8_t> encodedData) const {
		if(encodedData.empty() || encodedData.size() > (std::numeric_limits<DWORD>::max)()) {
			return std::unexpected(Error(ErrorCategory::Resource, kInvalidImage,
				"Encoded image data is empty or too large for WIC."));
		}

		const HRESULT initializeResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool ownsComInitialization = initializeResult == S_OK || initializeResult == S_FALSE;
		struct ComScope {
			bool owns = false;
			~ComScope() noexcept { if(owns) CoUninitialize(); }
		} comScope { ownsComInitialization };
		if(FAILED(initializeResult) && initializeResult != RPC_E_CHANGED_MODE) {
			return std::unexpected(MakeDirectXError(kWicFailure, initializeResult,
				"Failed to initialize COM for image decoding."));
		}

		Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
		HRESULT result = CoCreateInstance(
			CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kWicFailure, result,
				"Failed to create WIC imaging factory."));
		}
		Microsoft::WRL::ComPtr<IWICStream> stream;
		result = factory->CreateStream(&stream);
		if(FAILED(result)) return std::unexpected(MakeDirectXError(kWicFailure, result, "Failed to create WIC stream."));
		result = stream->InitializeFromMemory(
			const_cast<BYTE*>(reinterpret_cast<const BYTE*>(encodedData.data())),
			static_cast<DWORD>(encodedData.size()));
		if(FAILED(result)) return std::unexpected(MakeDirectXError(kWicFailure, result, "Failed to initialize WIC memory stream."));

		Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
		result = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
		if(FAILED(result)) return std::unexpected(MakeDirectXError(kWicFailure, result, "Failed to create WIC image decoder."));
		Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
		result = decoder->GetFrame(0, &frame);
		if(FAILED(result)) return std::unexpected(MakeDirectXError(kWicFailure, result, "Failed to read WIC image frame."));

		DecodedImage image;
		result = frame->GetSize(&image.width, &image.height);
		if(FAILED(result) || image.width == 0 || image.height == 0 ||
		   image.width > (std::numeric_limits<size_t>::max)() / 4U / image.height) {
			return std::unexpected(Error(ErrorCategory::Resource, kInvalidImage,
				"Decoded image dimensions are invalid."));
		}
		Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
		result = factory->CreateFormatConverter(&converter);
		if(FAILED(result)) return std::unexpected(MakeDirectXError(kWicFailure, result, "Failed to create WIC format converter."));
		result = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
			WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
		if(FAILED(result)) return std::unexpected(MakeDirectXError(kWicFailure, result, "Failed to convert image to RGBA8."));

		const uint32_t rowPitch = image.width * 4U;
		image.rgbaPixels.resize(static_cast<size_t>(rowPitch) * image.height);
		result = converter->CopyPixels(nullptr, rowPitch,
			static_cast<UINT>(image.rgbaPixels.size()), image.rgbaPixels.data());
		if(FAILED(result)) return std::unexpected(MakeDirectXError(kWicFailure, result, "Failed to copy decoded RGBA8 pixels."));
		return image;
	}
} // namespace NexusEngine
