#include "ShaderReflector.h"

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
#include <d3d12shader.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include <utility>

namespace NexusEngine {
	namespace {
		constexpr int32_t kReflectionCreationFailed = 1;
		constexpr int32_t kDescriptionFailed = 2;
		constexpr int32_t kUnsupportedResource = 3;

		Result<ShaderResourceType> ConvertResourceType(const D3D_SHADER_INPUT_TYPE type) {
			switch(type) {
			case D3D_SIT_CBUFFER: return ShaderResourceType::ConstantBuffer;
			case D3D_SIT_TEXTURE: return ShaderResourceType::Texture;
			case D3D_SIT_STRUCTURED: return ShaderResourceType::StructuredBuffer;
			case D3D_SIT_UAV_RWTYPED: return ShaderResourceType::RwTexture;
			case D3D_SIT_UAV_RWSTRUCTURED: return ShaderResourceType::RwStructuredBuffer;
			case D3D_SIT_SAMPLER: return ShaderResourceType::Sampler;
			default:
				return std::unexpected(Error(ErrorCategory::Graphics, kUnsupportedResource, "Shader contains an unsupported resource type."));
			}
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// DXIL Reflectionをエンジン共通Metadataへ変換する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<ShaderMetadata> ShaderReflector::Reflect(const std::span<const uint8_t> bytecode, const ShaderStage stage) const {
		using Microsoft::WRL::ComPtr;
		ComPtr<IDxcUtils> utils;
		HRESULT result = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kReflectionCreationFailed, result, "Failed to create DXC reflection utilities."));
		}

		// DXC Reflection APIは入力Bufferを参照するだけなので、Shaderが所有するbytecodeの寿命内で解析する。
		const DxcBuffer buffer { bytecode.data(), bytecode.size(), DXC_CP_ACP };
		ComPtr<ID3D12ShaderReflection> reflection;
		result = utils->CreateReflection(&buffer, IID_PPV_ARGS(&reflection));
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kReflectionCreationFailed, result, "Failed to create shader reflection."));
		}

		D3D12_SHADER_DESC shaderDesc = {};
		result = reflection->GetDesc(&shaderDesc);
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kDescriptionFailed, result, "Failed to read shader reflection description."));
		}

		ShaderMetadata metadata;
		metadata.resources.reserve(shaderDesc.BoundResources);
		for(uint32_t index = 0; index < shaderDesc.BoundResources; ++index) {
			D3D12_SHADER_INPUT_BIND_DESC bindingDesc = {};
			result = reflection->GetResourceBindingDesc(index, &bindingDesc);
			if(FAILED(result)) {
				return std::unexpected(MakeDirectXError(kDescriptionFailed, result, "Failed to read reflected resource binding."));
			}
			auto type = ConvertResourceType(bindingDesc.Type);
			if(!type) {
				return std::unexpected(std::move(type.error()));
			}
			ShaderResourceBinding binding;
			binding.name = bindingDesc.Name;
			binding.type = *type;
			binding.bindPoint = bindingDesc.BindPoint;
			binding.registerSpace = bindingDesc.Space;
			binding.bindCount = bindingDesc.BindCount;
			binding.stageMask = ToStageMask(stage);
			if(binding.type == ShaderResourceType::ConstantBuffer) {
				if(ID3D12ShaderReflectionConstantBuffer* constantBuffer = reflection->GetConstantBufferByName(bindingDesc.Name)) {
					D3D12_SHADER_BUFFER_DESC bufferDesc = {};
					if(SUCCEEDED(constantBuffer->GetDesc(&bufferDesc))) {
						binding.constantBufferSize = bufferDesc.Size;
					}
				}
			}
			metadata.resources.push_back(std::move(binding));
		}

		// Vertex入力semanticは保持するが、CPU stride/offsetはPipelineの明示Layoutへ委ねる。
		if(stage == ShaderStage::Vertex) {
			metadata.inputs.reserve(shaderDesc.InputParameters);
			for(uint32_t index = 0; index < shaderDesc.InputParameters; ++index) {
				D3D12_SIGNATURE_PARAMETER_DESC inputDesc = {};
				result = reflection->GetInputParameterDesc(index, &inputDesc);
				if(FAILED(result)) {
					return std::unexpected(MakeDirectXError(kDescriptionFailed, result, "Failed to read vertex input semantic."));
				}
				uint32_t componentCount = 0;
				for(uint32_t mask = inputDesc.Mask; mask != 0; mask >>= 1U) {
					componentCount += mask & 1U;
				}
				metadata.inputs.push_back({ inputDesc.SemanticName, inputDesc.SemanticIndex, componentCount });
			}
		}
		return metadata;
	}

} // namespace NexusEngine
