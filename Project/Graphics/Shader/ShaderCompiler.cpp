#include "ShaderCompiler.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <ObjIdl.h>
#include <OleAuto.h>
#include <Unknwn.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include "Foundation/Logging/Logger.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kDxcCreationFailed = 1;
		constexpr int32_t kSourceLoadFailed = 2;
		constexpr int32_t kCompileFailed = 3;
		constexpr int32_t kObjectAcquisitionFailed = 4;
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// DXCでHLSLファイルをDXILへCompileする
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<std::vector<uint8_t>> ShaderCompiler::Compile(const ShaderCompileDesc& desc) const {
		using Microsoft::WRL::ComPtr;

		// Compilerとファイル読込UtilityはCompile処理だけで使用し、Shaderへ所有権を持ち越さない。
		ComPtr<IDxcUtils> utils;
		ComPtr<IDxcCompiler3> compiler;
		HRESULT result = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kDxcCreationFailed, result, "Failed to create DXC utilities."));
		}
		result = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kDxcCreationFailed, result, "Failed to create DXC compiler."));
		}

		ComPtr<IDxcBlobEncoding> source;
		result = utils->LoadFile(desc.sourcePath.c_str(), nullptr, &source);
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kSourceLoadFailed, result, "Failed to load HLSL source: " + desc.sourcePath.string()));
		}

		const DxcBuffer sourceBuffer { source->GetBufferPointer(), source->GetBufferSize(), DXC_CP_UTF8 };
		std::vector<LPCWSTR> arguments = {
			desc.sourcePath.c_str(), L"-E", desc.entryPoint.c_str(), L"-T", desc.targetProfile.c_str(),
			L"-HV", L"2021", L"-Werror"
		};
#if defined(_DEBUG) || defined(NEXUS_DEVELOP)
		arguments.push_back(L"-Zi");
		arguments.push_back(L"-Qembed_debug");
		arguments.push_back(L"-Od");
#else
		arguments.push_back(L"-O3");
#endif

		// 標準Include HandlerによりShaderファイルからの相対#includeをDXCへ解決させる。
		ComPtr<IDxcIncludeHandler> includeHandler;
		result = utils->CreateDefaultIncludeHandler(&includeHandler);
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kDxcCreationFailed, result, "Failed to create DXC include handler."));
		}
		ComPtr<IDxcResult> compileResult;
		result = compiler->Compile(&sourceBuffer, arguments.data(), static_cast<uint32_t>(arguments.size()), includeHandler.Get(), IID_PPV_ARGS(&compileResult));
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kCompileFailed, result, "DXC invocation failed."));
		}

		ComPtr<IDxcBlobUtf8> diagnostics;
		static_cast<void>(compileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&diagnostics), nullptr));
		if(diagnostics && diagnostics->GetStringLength() > 0) {
			NEXUS_LOG_WARNING("Shader", diagnostics->GetStringPointer());
		}
		HRESULT compileStatus = S_OK;
		result = compileResult->GetStatus(&compileStatus);
		if(FAILED(result) || FAILED(compileStatus)) {
			return std::unexpected(MakeDirectXError(kCompileFailed, FAILED(result) ? result : compileStatus, "HLSL compilation failed: " + desc.sourcePath.string()));
		}

		ComPtr<IDxcBlob> object;
		result = compileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
		if(FAILED(result) || !object) {
			return std::unexpected(MakeDirectXError(kObjectAcquisitionFailed, result, "Failed to acquire compiled DXIL."));
		}
		const auto* begin = static_cast<const uint8_t*>(object->GetBufferPointer());
		return std::vector<uint8_t>(begin, begin + object->GetBufferSize());
	}

} // namespace NexusEngine
