#include "DDGIProcessor.h"
#include "Foundation/Logging/Logger.h"
#include "Graphics/Core/CommandQueue.h"
#include "Graphics/Descriptor/BindlessDescriptorTable.h"
#include "Graphics/Renderer/GraphicsResourceFactory.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include <algorithm>
namespace NexusEngine {
	namespace {
		void Barrier(ID3D12GraphicsCommandList* commands, ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
			if(from == to) return;
			D3D12_RESOURCE_BARRIER b{};
			b.Type		 = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
			commands->ResourceBarrier(1, &b);
		}
	} // namespace
	Result<void> DDGIProcessor::Initialize(ID3D12Device* device, CommandQueue* queue, BindlessDescriptorTable* table, GraphicsResourceFactory& factory, const std::filesystem::path& shaders) {
		device_	 = device;
		queue_	 = queue;
		table_	 = table;
		shaders_ = shaders;
		return factory.CreateConstantBuffer(constants_, sizeof(UpdateConstants));
	}
	Result<void> DDGIProcessor::CreatePipeline() {
		if(pipeline_) return {};
		D3D12_ROOT_PARAMETER p{};
		p.ParameterType				= D3D12_ROOT_PARAMETER_TYPE_CBV;
		p.Descriptor.ShaderRegister = 0;
		D3D12_ROOT_SIGNATURE_DESC		 desc{1, &p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED};
		Microsoft::WRL::ComPtr<ID3DBlob> blob, error;
		auto							 hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(92, hr, "DDGI root serialization failed."));
		hr = device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_));
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(92, hr, "DDGI root creation failed."));
		auto code = ShaderCompiler{}.Compile({shaders_ / L"Lighting/DDGIUpdate.hlsl", L"DDGIUpdate", L"cs_6_6"});
		if(!code) return std::unexpected(std::move(code.error()));
		D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
		pipeline.pRootSignature = root_.Get();
		pipeline.CS				= {code->data(), code->size()};
		hr						= device_->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&pipeline_));
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(92, hr, "DDGI compute PSO creation failed."));
		return {};
	}
	void DDGIProcessor::Release() noexcept {
		auto retire = [&](ShaderResourceRef& ref) {
			if(ref.IsValid()) {
				auto result = table_->Retire(ref, queue_->GetCompletedFenceValue());
				if(!result) NEXUS_LOG_ERROR("DDGI", result.error().GetMessageText());
				ref = {};
			}
		};
		for(auto& h : history_) {
			for(auto& ref : h.srv) retire(ref);
			for(auto& ref : h.uav) retire(ref);
			h = {};
		}
		retire(raySrv_);
		retire(rayUav_);
		retire(sampler_);
		rays_.Reset();
		if(table_) table_->CollectGarbage(queue_->GetCompletedFenceValue());
		allocated_ = ready_ = false;
		read_ = frameNumber_ = 0;
		rayState_			 = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}
	Result<void> DDGIProcessor::Prepare(const DDGISettings& settings) {
		if(!settings.IsValid()) return std::unexpected(Error(ErrorCategory::Graphics, 92, "Invalid DDGI settings."));
		auto pipeline = CreatePipeline();
		if(!pipeline) return pipeline;
		const bool resize = !allocated_ || settings.counts != settings_.counts || settings.raysPerProbe != settings_.raysPerProbe;
		const bool reset  = resize || settings.origin != settings_.origin || settings.spacing != settings_.spacing || settings.revision != settings_.revision ||
							settings.maxDistance != settings_.maxDistance || settings.relocation != settings_.relocation || settings.rotateRays != settings_.rotateRays || settings.feedback != settings_.feedback;
		if(resize) {
			// 寸法変更だけはQueue完了後に旧世代を解放する。通常フレームの更新はGPU内で完結する。
			auto idle = queue_->WaitForIdle();
			if(!idle) return idle;
			Release();
			const uint32_t probes  = settings.counts[0] * settings.counts[1] * settings.counts[2];
			auto		   texture = [&](Resource& resource, ShaderResourceRef& srv, ShaderResourceRef& uav, uint32_t width, uint32_t height, DXGI_FORMAT format) -> Result<void> {
				D3D12_HEAP_PROPERTIES heap{};
				heap.Type = D3D12_HEAP_TYPE_DEFAULT;
				D3D12_RESOURCE_DESC desc{};
				desc.Dimension		  = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				desc.Width			  = width;
				desc.Height			  = height;
				desc.DepthOrArraySize = desc.MipLevels = 1;
				desc.Format							   = format;
				desc.SampleDesc.Count				   = 1;
				desc.Flags							   = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
				auto hr								   = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&resource));
				if(FAILED(hr)) return std::unexpected(MakeDirectXError(92, hr, "DDGI texture allocation failed."));
				D3D12_SHADER_RESOURCE_VIEW_DESC s{};
				s.Format				  = format;
				s.ViewDimension			  = D3D12_SRV_DIMENSION_TEXTURE2D;
				s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				s.Texture2D.MipLevels	  = 1;
				auto ref				  = table_->CreateShaderResourceView(resource.Get(), s);
				if(!ref) return std::unexpected(std::move(ref.error()));
				srv = *ref;
				D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
				u.Format		= format;
				u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
				ref				= table_->CreateUnorderedAccessView(resource.Get(), nullptr, u);
				if(!ref) return std::unexpected(std::move(ref.error()));
				uav = *ref;
				return {};
			};
			for(auto& h : history_) {
				const uint32_t width = settings.counts[0] * settings.counts[1] * 10, height = settings.counts[2] * 10;
				auto		   result = texture(h.textures[0], h.srv[0], h.uav[0], width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
				if(!result) return result;
				result = texture(h.textures[1], h.srv[1], h.uav[1], width, height, DXGI_FORMAT_R32G32_FLOAT);
				if(!result) return result;
				result = texture(h.textures[2], h.srv[2], h.uav[2], probes, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
				if(!result) return result;
			}
			auto result = texture(rays_, raySrv_, rayUav_, settings.raysPerProbe, probes, DXGI_FORMAT_R32G32B32A32_FLOAT);
			if(!result) return result;
			D3D12_SAMPLER_DESC sample{};
			sample.Filter	= D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			sample.AddressU = sample.AddressV = sample.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			sample.MaxLOD										= D3D12_FLOAT32_MAX;
			sample.MaxAnisotropy								= 1;
			sample.ComparisonFunc								= D3D12_COMPARISON_FUNC_NEVER;
			auto ref											= table_->CreateSampler(sample);
			if(!ref) return std::unexpected(std::move(ref.error()));
			sampler_   = *ref;
			allocated_ = true;
		}
		settings_ = settings;
		if(reset) {
			ready_		 = false;
			frameNumber_ = 0;
		}
		return {};
	}
	void DDGIProcessor::Begin(ID3D12GraphicsCommandList* commands) {
		initialRead_   = read_;
		initialReady_  = ready_;
		initialFrame_  = frameNumber_;
		recording_	   = true;
		initialStates_ = {history_[0].state, history_[1].state, rayState_};
		for(auto& h : history_) {
			for(auto& texture : h.textures) Barrier(commands, texture.Get(), h.state, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
			h.state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
		}
		Barrier(commands, rays_.Get(), rayState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		rayState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}
	DDGIDrawData DDGIProcessor::GetDrawData(float intensity) const noexcept {
		DDGIDrawData data;
		if(!allocated_) return data;
		const auto& refs	 = history_[read_].srv;
		data.textures		 = {refs[0].index, refs[1].index, refs[2].index, sampler_.index};
		data.originIntensity = {settings_.origin[0], settings_.origin[1], settings_.origin[2], intensity};
		data.spacingEnabled	 = {settings_.spacing[0], settings_.spacing[1], settings_.spacing[2], ready_ ? 1.0f : 0};
		data.counts			 = {settings_.counts[0], settings_.counts[1], settings_.counts[2], settings_.raysPerProbe};
		data.sampling		 = {settings_.normalBias, settings_.viewBias, settings_.maxDistance, 32};
		return data;
	}
	Result<void> DDGIProcessor::Update(ID3D12GraphicsCommandList* commands, uint32_t frame) {
		const uint32_t	write = 1 - read_;
		auto&			next  = history_[write];
		UpdateConstants constants;
		constants.previous = GetDrawData();
		constants.outputs  = {raySrv_.index, next.uav[0].index, next.uav[1].index, next.uav[2].index};
		constants.filter   = {settings_.hysteresis, 32, settings_.maxDistance, (std::min)({settings_.spacing[0], settings_.spacing[1], settings_.spacing[2]})};
		constants.options  = {ready_ ? 0U : 1U, Phase(), settings_.relocation ? 1U : 0U, settings_.feedback ? 1U : 0U};
		auto result		   = constants_.Write(frame, {reinterpret_cast<const uint8_t*>(&constants), sizeof(constants)});
		if(!result) return result;
		// TraceRayのUAV書き込みを確定してComputeへ渡し、次世代だけを書き換える。
		Barrier(commands, rays_.Get(), rayState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		rayState_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		for(auto& texture : next.textures) Barrier(commands, texture.Get(), next.state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		next.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		commands->SetComputeRootSignature(root_.Get());
		commands->SetPipelineState(pipeline_.Get());
		commands->SetComputeRootConstantBufferView(0, constants_.GetGpuAddress(frame));
		commands->Dispatch(settings_.counts[0] * settings_.counts[1] * settings_.counts[2], 1, 1);
		for(auto& texture : next.textures) Barrier(commands, texture.Get(), next.state, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
		next.state = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
		read_	   = write;
		ready_	   = true;
		++frameNumber_;
		return {};
	}
	void DDGIProcessor::CompleteRecording(bool submitted) noexcept {
		if(recording_ && !submitted) {
			read_			  = initialRead_;
			ready_			  = initialReady_;
			frameNumber_	  = initialFrame_;
			history_[0].state = initialStates_[0];
			history_[1].state = initialStates_[1];
			rayState_		  = initialStates_[2];
		}
		recording_ = false;
	}
	void DDGIProcessor::Shutdown() noexcept {
		Release();
		constants_.Shutdown();
		pipeline_.Reset();
		root_.Reset();
		device_ = nullptr;
		queue_	= nullptr;
		table_	= nullptr;
	}
} // namespace NexusEngine
