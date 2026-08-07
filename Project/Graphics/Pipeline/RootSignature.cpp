#include "RootSignature.h"

#include <vector>

namespace NexusEngine {
	namespace { constexpr int32_t kSerializationFailed = 1; constexpr int32_t kCreationFailed = 2; }

	/////////////////////////////////////////////////////////////////////////////////////////
	// Reflection LayoutとBindless規則からRootSignature 1.1を生成する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> RootSignature::Initialize(ID3D12Device* const device, const PipelineLayout& layout) {
		// 従来型Reflection Bindingも併用可能にするため、Direct Heap Indexing flagとは別に
		// CBV/SRV/UAV tableとSampler tableのrangeを構築する。
		std::vector<D3D12_DESCRIPTOR_RANGE1> resourceRanges;
		std::vector<D3D12_DESCRIPTOR_RANGE1> samplerRanges;
		for(const auto& binding : layout.GetBindings()) {
			// Engine MetadataをRootSignature生成直前にD3D12 rangeへ変換し、上位へ型を漏らさない。
			D3D12_DESCRIPTOR_RANGE1 range = {};
			range.NumDescriptors = binding.resource.bindCount;
			range.BaseShaderRegister = binding.resource.bindPoint;
			range.RegisterSpace = binding.resource.registerSpace;
			range.OffsetInDescriptorsFromTableStart = binding.descriptorOffset;
			range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
			switch(binding.resource.type) {
			case ShaderResourceType::ConstantBuffer: range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV; break;
			case ShaderResourceType::Sampler: range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; break;
			case ShaderResourceType::RwTexture:
			case ShaderResourceType::RwStructuredBuffer: range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; break;
			default: range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; break;
			}
			(binding.resource.type == ShaderResourceType::Sampler ? samplerRanges : resourceRanges).push_back(range);
		}

		// RootSignatureコストを一定に保つため、個別ResourceではなくHeap種別ごとに最大一つのTableへ集約する。
		std::vector<D3D12_ROOT_PARAMETER1> parameters;
		if(!resourceRanges.empty()) {
			resourceRootIndex_ = static_cast<uint32_t>(parameters.size());
			D3D12_ROOT_PARAMETER1 parameter = {};
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.DescriptorTable = { static_cast<UINT>(resourceRanges.size()), resourceRanges.data() };
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			parameters.push_back(parameter);
		}
		if(!samplerRanges.empty()) {
			samplerRootIndex_ = static_cast<uint32_t>(parameters.size());
			D3D12_ROOT_PARAMETER1 parameter = {};
			parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameter.DescriptorTable = { static_cast<UINT>(samplerRanges.size()), samplerRanges.data() };
			parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			parameters.push_back(parameter);
		}

		// Bindless ShaderがResourceDescriptorHeap/SamplerDescriptorHeapを直接Indexできるよう、
		// RootSignature 1.1のDirect Heap Indexing flagを全Graphics Pipelineで一貫して有効化する。
		D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc = {};
		desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		desc.Desc_1_1.NumParameters = static_cast<UINT>(parameters.size());
		desc.Desc_1_1.pParameters = parameters.data();
		desc.Desc_1_1.Flags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
			D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
		// Versioned serializerを使用し、1.1 range flagsとDirect Heap Indexing flagsを保持する。
		Microsoft::WRL::ComPtr<ID3DBlob> serialized;
		Microsoft::WRL::ComPtr<ID3DBlob> errors;
		HRESULT result = D3D12SerializeVersionedRootSignature(&desc, &serialized, &errors);
		if(FAILED(result)) {
			const std::string message = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "Failed to serialize root signature.";
			return std::unexpected(MakeDirectXError(kSerializationFailed, result, message));
		}
		// Serialization成功後だけDeviceへRootSignature生成を要求し、診断BlobはErrorへ変換する。
		result = device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
		if(FAILED(result)) {
			return std::unexpected(MakeDirectXError(kCreationFailed, result, "Failed to create root signature."));
		}
		return {};
	}

	ID3D12RootSignature* RootSignature::GetNative() const noexcept { return rootSignature_.Get(); }
	bool RootSignature::HasResourceTable() const noexcept { return resourceRootIndex_ != UINT32_MAX; }
	bool RootSignature::HasSamplerTable() const noexcept { return samplerRootIndex_ != UINT32_MAX; }
	void RootSignature::BindDescriptorTables(ID3D12GraphicsCommandList* const commandList, const D3D12_GPU_DESCRIPTOR_HANDLE resources, const D3D12_GPU_DESCRIPTOR_HANDLE samplers) const noexcept {
		// Root parameter番号はRootSignature内部だけが知り、Rendererへ公開しない。
		if(HasResourceTable()) commandList->SetGraphicsRootDescriptorTable(resourceRootIndex_, resources);
		if(HasSamplerTable()) commandList->SetGraphicsRootDescriptorTable(samplerRootIndex_, samplers);
	}

} // namespace NexusEngine
