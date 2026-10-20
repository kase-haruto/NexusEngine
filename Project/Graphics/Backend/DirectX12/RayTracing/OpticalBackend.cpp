#include "OpticalBackend.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include "Graphics/Core/CommandQueue.h"
#include "Graphics/Descriptor/BindlessDescriptorTable.h"
#include "Graphics/Renderer/GraphicsContext.h"
#include "Graphics/Renderer/GraphicsResourceFactory.h"
#include "Graphics/Resource/MeshResource.h"
#include "Graphics/Model/ModelAssetData.h"
#include "Graphics/Shader/Shader.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Foundation/Logging/Logger.h"

namespace NexusEngine {
	namespace {
		Result<void> UploadBuffer(ID3D12Device* device, uint64_t size, Microsoft::WRL::ComPtr<ID3D12Resource>& buffer) {
			D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_UPLOAD;
			D3D12_RESOURCE_DESC desc {}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width = size; desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
			desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			const auto hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
				D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer));
			if(FAILED(hr)) return std::unexpected(MakeDirectXError(70, hr, "Optical upload/table buffer allocation failed."));
			return {};
		}
		void OutputBarrier(ID3D12GraphicsCommandList* commands, ID3D12Resource* output,
			D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
			if(before == after) return;
			D3D12_RESOURCE_BARRIER barrier {}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition = {output, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
			commands->ResourceBarrier(1, &barrier);
		}
	}
	Result<void> OpticalBackend::Initialize(void* device, CommandQueue* queue, BindlessDescriptorTable* bindless,
		GraphicsResourceFactory* factory, uint32_t frames, bool supported, const std::filesystem::path& shaders) {
		if(!device || !queue || !bindless || !factory || !frames)
			return std::unexpected(Error(ErrorCategory::Graphics, 70, "Optical backend dependencies missing."));
		queue_ = queue; bindless_ = bindless; supported_ = supported; shaders_ = shaders;
		if(!supported_) return {};
		const auto hr = static_cast<ID3D12Device*>(device)->QueryInterface(IID_PPV_ARGS(&device_));
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(70, hr, "Optical Device5 unavailable."));
		metadata_.resize(frames); hitTables_.resize(frames); activeGenerations_.resize(frames);
		auto result = factory->CreateConstantBuffer(constants_, sizeof(Constants));
		if(!result) return result;
		result=factory->CreateConstantBuffer(probeConstants_,sizeof(Constants));if(!result) return result;
		result=ddgi_.Initialize(device_.Get(),queue_,bindless_,*factory,shaders_);if(!result) return result;
		result=InitializeTimings(frames);if(!result) return result;
		return scene_.Initialize(device_.Get(), frames);
	}
	Result<void> OpticalBackend::InitializeTimings(uint32_t frames) {
		auto hr=queue_->GetNativeQueue()->GetTimestampFrequency(&timestampFrequency_);
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(70,hr,"GPU timestamp frequency unavailable."));
		D3D12_QUERY_HEAP_DESC query{};query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;query.Count=frames*5;
		hr=device_->CreateQueryHeap(&query,IID_PPV_ARGS(&timingQueries_));
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(70,hr,"GPU timing query allocation failed."));
		D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
		D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=frames*5*sizeof(uint64_t);
		desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		hr=device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&timingReadback_));
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(70,hr,"GPU timing readback allocation failed."));
		timingReady_.assign(frames,0);timingRecorded_.assign(frames,0);return {};
	}
	void OpticalBackend::BeginTimings(ID3D12GraphicsCommandList* commands,uint32_t frame) {
		// The frame slot is already fence-complete, like metadata_[frame]. Never wait for profiling.
		if(timingReady_[frame]) {
			const SIZE_T begin=frame*5*sizeof(uint64_t);D3D12_RANGE range{begin,begin+5*sizeof(uint64_t)};
			void* data=nullptr;
			if(SUCCEEDED(timingReadback_->Map(0,&range,&data))) {
				std::array<uint64_t,5> ticks{};std::memcpy(ticks.data(),static_cast<uint8_t*>(data)+begin,sizeof(ticks));
				D3D12_RANGE written{0,0};timingReadback_->Unmap(0,&written);
				for(size_t i=0;i<4;++i) timings_.milliseconds[i]=static_cast<float>(static_cast<double>(ticks[i+1]-ticks[i])*1000.0/static_cast<double>(timestampFrequency_));
				timings_.valid=true;
			}
		}
		timingReady_[frame]=0;timingRecorded_[frame]=0;
		commands->EndQuery(timingQueries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,frame*5);
	}
	void OpticalBackend::ResolveTimings(ID3D12GraphicsCommandList* commands,uint32_t frame) {
		commands->ResolveQueryData(timingQueries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,frame*5,5,timingReadback_.Get(),frame*5*sizeof(uint64_t));
		timingRecorded_[frame]=1;
	}
	Result<void> OpticalBackend::CreatePipeline() {
		Shader shader;
		auto result = shader.Initialize({shaders_ / L"RayTracing/Optical.hlsl", L"", L"lib_6_6"}, ShaderStage::RayTracingLibrary);
		if(!result) return result;
		for(const auto& binding : shader.GetMetadata().resources) {
			const bool valid = (binding.type == ShaderResourceType::ConstantBuffer && binding.bindPoint == 0 && binding.constantBufferSize <= sizeof(Constants)) ||
				(binding.type == ShaderResourceType::ConstantBuffer && binding.bindPoint == 1 && binding.constantBufferSize <= 16) ||
				(binding.type == ShaderResourceType::AccelerationStructure && binding.bindPoint == 0) ||
				(binding.type == ShaderResourceType::ByteAddressBuffer && binding.bindPoint == 1);
			if(!valid || binding.registerSpace != 0 || binding.bindCount != 1)
				return std::unexpected(Error(ErrorCategory::Graphics, 70, "Optical shader/root contract mismatch."));
		}
		D3D12_ROOT_PARAMETER1 params[3] {};
		for(uint32_t i = 0; i < 3; ++i) {
			params[i].ParameterType = i == 0 ? D3D12_ROOT_PARAMETER_TYPE_CBV : D3D12_ROOT_PARAMETER_TYPE_SRV;
			params[i].Descriptor.ShaderRegister = i == 0 ? 0 : i - 1;
			params[i].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
			params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}
		D3D12_VERSIONED_ROOT_SIGNATURE_DESC root {}; root.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		root.Desc_1_1.NumParameters = 3; root.Desc_1_1.pParameters = params;
		root.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
		Microsoft::WRL::ComPtr<ID3DBlob> serialized, errors;
		auto hr = D3D12SerializeVersionedRootSignature(&root, &serialized, &errors);
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(70, hr, "Optical root serialization failed."));
		hr = device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root_));
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(70, hr, "Optical root creation failed."));
		D3D12_ROOT_PARAMETER1 localParameter {}; localParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		localParameter.Constants = {1, 0, 1}; localParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		root.Desc_1_1.NumParameters = 1; root.Desc_1_1.pParameters = &localParameter;
		root.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		hr = D3D12SerializeVersionedRootSignature(&root, &serialized, &errors);
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(70, hr, "Optical local root serialization failed."));
		Microsoft::WRL::ComPtr<ID3D12RootSignature> localRoot;
		hr = device_->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&localRoot));
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(70, hr, "Optical local root creation failed."));
		const wchar_t* names[] {L"OpticalRaygen", L"OpticalMiss", L"OpticalHit",L"DDGIProbeRaygen"};
		D3D12_EXPORT_DESC exports[4] {}; for(size_t i = 0; i < 4; ++i) exports[i].Name = names[i];
		const auto bytes = shader.GetBytecode();
		D3D12_DXIL_LIBRARY_DESC library {{bytes.data(), bytes.size()}, 4, exports};
		D3D12_HIT_GROUP_DESC hit {}; hit.HitGroupExport = L"OpticalHitGroup";
		hit.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES; hit.ClosestHitShaderImport = L"OpticalHit";
		// raygenで経路を反復する。交差shaderはレイを生成しないため再帰深度は1のまま。
		D3D12_RAYTRACING_SHADER_CONFIG config {100, 8};
		D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig {1};
		ID3D12RootSignature* global = root_.Get();
		ID3D12RootSignature* local = localRoot.Get();
		const wchar_t* localExports[] {L"OpticalHitGroup"};
		D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION association {}; association.NumExports = 1; association.pExports = localExports;
		D3D12_STATE_SUBOBJECT objects[] {
			{D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &library},
			{D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hit},
			{D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &config},
			{D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &global},
			{D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipelineConfig},
			{D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &local},
			{D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &association}};
		association.pSubobjectToAssociate = &objects[5];
		D3D12_STATE_OBJECT_DESC desc {D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE, 7, objects};
		hr = device_->CreateStateObject(&desc, IID_PPV_ARGS(&pipeline_));
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(70, hr, "Optical DXR StateObject creation failed."));
		Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> props; hr = pipeline_.As(&props);
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(70, hr, "Optical StateObject properties unavailable."));
		result = UploadBuffer(device_.Get(), 256, shaderTable_); if(!result) return result;
		void* mapped = nullptr; const D3D12_RANGE noReads {0, 0};
		hr = shaderTable_->Map(0, &noReads, &mapped);
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(70, hr, "Optical ShaderTable Map failed."));
		std::memset(mapped, 0, 256);
		const wchar_t* tableNames[] {L"OpticalRaygen", L"OpticalMiss", L"OpticalHitGroup",L"DDGIProbeRaygen"};
		for(size_t i = 0; i < 4; ++i) {
			const void* identifier = props->GetShaderIdentifier(tableNames[i]);
			if(!identifier) { shaderTable_->Unmap(0, nullptr); return std::unexpected(Error(ErrorCategory::Graphics, 70, "Optical shader identifier missing.")); }
			// 32byteの識別子を64byte境界へ配置する。raygen・missのレコードは固定し、
			// hit識別子とb1の形状番号はフレーム別テーブルへ複写する。
			std::memcpy(static_cast<uint8_t*>(mapped) + i * 64, identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
			if(i == 2) std::memcpy(hitIdentifier_.data(), identifier, hitIdentifier_.size());
		}
		shaderTable_->Unmap(0, nullptr); pipelineReady_ = true; return {};
	}
	Result<void> OpticalBackend::CreateReflectionFilter() {
		if(filterPipeline_) return {};
		D3D12_ROOT_PARAMETER parameter{};
		parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		parameter.Constants = {0,0,6};
		D3D12_ROOT_SIGNATURE_DESC desc{1,&parameter,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED};
		Microsoft::WRL::ComPtr<ID3DBlob> blob,error;
		auto hr = D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error);
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(70,hr,"Reflection filter root serialization failed."));
		hr = device_->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&filterRoot_));
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(70,hr,"Reflection filter root creation failed."));
		auto code = ShaderCompiler{}.Compile({shaders_/L"RayTracing/ReflectionFilter.CS.hlsl",L"main",L"cs_6_6"});
		if(!code) return std::unexpected(std::move(code.error()));
		D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
		pipeline.pRootSignature=filterRoot_.Get(); pipeline.CS={code->data(),code->size()};
		hr = device_->CreateComputePipelineState(&pipeline,IID_PPV_ARGS(&filterPipeline_));
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(70,hr,"Reflection filter PSO creation failed."));
		return {};
	}
	void OpticalBackend::ReleaseOutput() noexcept {
		for(auto& view : {&outputSrv_, &outputUav_, &giSrv_, &giUav_, &reflectionSrv_, &reflectionUav_, &guideSrv_, &guideUav_}) {
			if(view->IsValid()) { auto result = bindless_->Retire(*view, queue_->GetCompletedFenceValue()); if(!result) NEXUS_LOG_ERROR("Optics", result.error().GetMessageText()); *view = {}; }
		}
		bindless_->CollectGarbage(queue_->GetCompletedFenceValue()); output_.Reset(); giOutput_.Reset(); reflection_.Reset(); reflectionGuide_.Reset();
	}
	Result<void> OpticalBackend::Resize(uint32_t width, uint32_t height) {
		if(output_ && outputSrv_.IsValid() && outputUav_.IsValid() && giOutput_ && giSrv_.IsValid() && giUav_.IsValid() && reflection_ && reflectionGuide_ && reflectionSrv_.IsValid() && reflectionUav_.IsValid() && guideSrv_.IsValid() && guideUav_.IsValid() && output_->GetDesc().Width == width && output_->GetDesc().Height == height) return {};
		auto wait = queue_->WaitForIdle(); if(!wait) return wait;
		ReleaseOutput();
		D3D12_RESOURCE_DESC desc {}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; desc.SampleDesc.Count = 1; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		const auto hr = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&output_));
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(70, hr, "Optical radiance texture allocation failed."));
		state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		D3D12_SHADER_RESOURCE_VIEW_DESC srv {}; srv.Format = desc.Format;
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
		auto view = bindless_->CreateShaderResourceView(output_.Get(), srv); if(!view) return std::unexpected(std::move(view.error())); outputSrv_ = *view;
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav {}; uav.Format = desc.Format; uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		view = bindless_->CreateUnorderedAccessView(output_.Get(), nullptr, uav); if(!view) return std::unexpected(std::move(view.error())); outputUav_ = *view;
		// 二つの出力で画面寸法・遷移・記録破棄時の復元を共有する。
		// 所有者も揃え、GI用のTLAS再生成やScene再走査を増やさない。
		const auto giCreated = device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&giOutput_));
		if(FAILED(giCreated)) return std::unexpected(MakeDirectXError(70, giCreated, "Diffuse GI texture allocation failed."));
		view = bindless_->CreateShaderResourceView(giOutput_.Get(), srv); if(!view) return std::unexpected(std::move(view.error())); giSrv_ = *view;
		view = bindless_->CreateUnorderedAccessView(giOutput_.Get(), nullptr, uav); if(!view) return std::unexpected(std::move(view.error())); giUav_ = *view;
		for(uint32_t index=0;index<2;++index) {
			auto& resource=index==0?reflection_:reflectionGuide_;
			desc.Format=index==0?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT;
			auto created=device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&resource));
			if(FAILED(created)) return std::unexpected(MakeDirectXError(70,created,"Reflection filter texture allocation failed."));
			srv.Format=desc.Format; uav.Format=desc.Format;
			view=bindless_->CreateShaderResourceView(resource.Get(),srv);if(!view) return std::unexpected(std::move(view.error()));
			(index==0?reflectionSrv_:guideSrv_)=*view;
			view=bindless_->CreateUnorderedAccessView(resource.Get(),nullptr,uav);if(!view) return std::unexpected(std::move(view.error()));
			(index==0?reflectionUav_:guideUav_)=*view;
		}
		return {};
	}
	Result<OpticalBackend::GeometryViews*> OpticalBackend::GetViews(const MeshResource& mesh) {
		const auto generation = mesh.GetGeometryGeneration();
		if(auto found = views_.find(generation); found != views_.end()) return &found->second;
		if(mesh.vertexBuffer_.GetStride() != sizeof(ModelVertex))
			return std::unexpected(Error(ErrorCategory::Graphics, 70, "Optical hit attributes require ModelVertex layout."));
		auto& views = views_[generation];
		views.vertices = static_cast<ID3D12Resource*>(mesh.vertexBuffer_.GetNativeResource());
		views.indices = static_cast<ID3D12Resource*>(mesh.indexBuffer_.GetNativeResource());
		D3D12_SHADER_RESOURCE_VIEW_DESC desc {}; desc.Format = DXGI_FORMAT_R32_TYPELESS;
		desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
		desc.Buffer.NumElements = static_cast<UINT>(views.vertices->GetDesc().Width / 4);
		auto view = bindless_->CreateShaderResourceView(views.vertices.Get(), desc);
		if(!view) { views_.erase(generation); return std::unexpected(std::move(view.error())); } views.vertexView = *view;
		desc.Buffer.NumElements = static_cast<UINT>(views.indices->GetDesc().Width / 4);
		view = bindless_->CreateShaderResourceView(views.indices.Get(), desc);
		if(!view) { ReleaseViews(views); views_.erase(generation); return std::unexpected(std::move(view.error())); } views.indexView = *view;
		return &views;
	}
	Result<ShaderResourceRef> OpticalBackend::Render(GraphicsContext& context, void* nativeCommands, const Matrix4x4& vp,
		const DirectionalLightData& light, std::span<const OpticalDrawItem> draws,
		std::span<const OpticalSurfaceMaterial> materials, const OpticalSettings& settings, const std::array<float, 3>& ambientIrradiance, const EnvironmentDrawData& environment, const GlobalIlluminationSettings& gi, const BakedLightingDrawData& baked, const DDGISettings& ddgi) {
		bool optical=false,localReflections=false;
		// Unused materials in the shared asset must not enable a full-screen ray/filter pass.
		for(const auto& draw:draws) {
			if(!draw.geometry.mesh || !draw.geometry.mesh->IsInitialized())
				return std::unexpected(Error(ErrorCategory::Graphics,70,"Optical mesh is invalid."));
			for(const auto& sub:draw.geometry.mesh->GetSubmeshes()) {
				if(sub.materialSlot>=materials.size()) return std::unexpected(Error(ErrorCategory::Graphics,70,"Optical material slot out of range."));
				const auto& m=materials[sub.materialSlot];
				localReflections|=settings.enabled && settings.metallicReflections && m.pbr[0]>0 && (m.pbr[1]<.5f || m.metallicRoughnessTexture.IsValid());
				optical|=m.optics.IsOptical();
			}
		}
		optical|=localReflections;
		if(((!settings.enabled || !optical) && !gi.enabled && !ddgi.enabled) || draws.empty()) return ShaderResourceRef {};
		if(!supported_) {
			if(!warned_) { NEXUS_LOG_WARNING("Optics", "DXR GI/optical rendering unavailable; using raster/IBL fallback."); warned_ = true; }
			return ShaderResourceRef {};
		}
		for(const auto& draw : draws) if(!draw.geometry.jointPalette.empty())
			return std::unexpected(Error(ErrorCategory::Graphics, 70, "Optical scene requires static/deformed geometry; VS-only skinning is unsupported."));
		for(const auto& material : materials) {
			const auto& o = material.optics;
			if(!std::isfinite(o.reflectivity) || o.reflectivity < 0 || o.reflectivity > 1 ||
				!std::isfinite(o.transmission) || o.transmission < 0 || o.transmission > 1 ||
				!std::isfinite(o.indexOfRefraction) || o.indexOfRefraction < 1 || o.indexOfRefraction > 3)
				return std::unexpected(Error(ErrorCategory::Graphics, 70, "Invalid optical material coefficients."));
			for(float value : o.absorption) if(!std::isfinite(value) || value < 0)
				return std::unexpected(Error(ErrorCategory::Graphics, 70, "Invalid absorption coefficient."));
		}
		const auto inverse = TryInverse(vp);
		if(!inverse) return std::unexpected(Error(ErrorCategory::Graphics, 70, "Optical camera matrix is singular."));
		if(!pipelineReady_) { auto result = CreatePipeline(); if(!result) return std::unexpected(std::move(result.error())); }
		const bool screenPass=(settings.enabled && optical) || gi.enabled;
		if(localReflections) {auto filter=CreateReflectionFilter();if(!filter) return std::unexpected(std::move(filter.error()));}
		if(screenPass) {auto resized = Resize(context.GetRenderWidth(), context.GetRenderHeight()); if(!resized) return std::unexpected(std::move(resized.error()));}
		if(ddgi.enabled) {auto prepared=ddgi_.Prepare(ddgi);if(!prepared) return std::unexpected(std::move(prepared.error()));}
		instances_.clear(); geometries_.clear(); accelerationDraws_.clear(); hitGroupOffsets_.clear();
		auto& active = activeGenerations_.at(context.GetFrameIndex()); active.clear();
		for(const auto& draw : draws) {
			const auto* mesh = draw.geometry.mesh;
			if(!mesh || !mesh->IsInitialized()) return std::unexpected(Error(ErrorCategory::Graphics, 70, "Optical mesh is invalid."));
			const auto worldInverse = TryInverse(draw.geometry.world);
			if(!worldInverse) return std::unexpected(Error(ErrorCategory::Graphics, 70, "Optical instance transform is singular."));
			auto views = GetViews(*mesh); if(!views) return std::unexpected(std::move(views.error()));
			active.push_back(mesh->GetGeometryGeneration());
			InstanceData instance; instance.normalTransform = Transpose(*worldInverse);
			hitGroupOffsets_.push_back(static_cast<uint32_t>(geometries_.size()));
			instance.geometryBase[0] = static_cast<uint32_t>(geometries_.size()); instances_.push_back(instance);
			accelerationDraws_.push_back(draw.geometry);
			for(const auto& submesh : mesh->GetSubmeshes()) {
				if(submesh.materialSlot >= materials.size()) return std::unexpected(Error(ErrorCategory::Graphics, 70, "Optical material slot out of range."));
				const auto& m = materials[submesh.materialSlot]; GeometryData geometry;
				geometry.indices = {(*views)->vertexView.index, (*views)->indexView.index, mesh->vertexBuffer_.GetStride(),
					mesh->indexBuffer_.GetFormat() == IndexFormat::UInt32 ? 1U : 0U, submesh.firstIndex,
					static_cast<uint32_t>(submesh.vertexOffset), m.texture.index, m.sampler.index};
				for(size_t i = 0; i < 4; ++i) geometry.color[i] = m.color[i] * draw.tint[i];
				geometry.optical = {m.optics.reflectivity, m.optics.transmission, m.optics.indexOfRefraction, 0};
				geometry.pbr = m.pbr;
				geometry.emissiveOcclusion = m.emissiveOcclusion;
				geometry.extraMaps = {m.emissiveTexture.index, m.emissiveSampler.index, m.occlusionTexture.index, m.occlusionSampler.index};
				geometry.maps = {m.normalTexture.index, m.normalSampler.index, m.metallicRoughnessTexture.index, m.metallicRoughnessSampler.index};
				std::copy(m.optics.absorption.begin(), m.optics.absorption.end(), geometry.absorption.begin()); geometries_.push_back(geometry);
			}
		}
		const size_t instanceBytes = instances_.size() * sizeof(InstanceData);
		const size_t geometryBytes = geometries_.size() * sizeof(GeometryData);
		if(instanceBytes + geometryBytes > (std::numeric_limits<uint32_t>::max)())
			return std::unexpected(Error(ErrorCategory::Graphics, 70, "Optical metadata exceeds raw-address range."));
		auto& data = metadata_.at(context.GetFrameIndex());
		if(!data || data->GetDesc().Width < instanceBytes + geometryBytes) {
			auto result = UploadBuffer(device_.Get(), instanceBytes + geometryBytes, data);
			if(!result) return std::unexpected(std::move(result.error()));
		}
		void* mapped = nullptr; const D3D12_RANGE noReads {0, 0}; const auto hr = data->Map(0, &noReads, &mapped);
		if(FAILED(hr)) return std::unexpected(MakeDirectXError(70, hr, "Optical Frame metadata Map failed."));
		std::memcpy(mapped, instances_.data(), instanceBytes);
		std::memcpy(static_cast<uint8_t*>(mapped) + instanceBytes, geometries_.data(), geometryBytes); data->Unmap(0, nullptr);
		auto& hitTable = hitTables_.at(context.GetFrameIndex());
		const uint64_t hitTableBytes = geometries_.size() * uint64_t(64);
		if(!hitTable || hitTable->GetDesc().Width < hitTableBytes) {
			auto result = UploadBuffer(device_.Get(), hitTableBytes, hitTable);
			if(!result) return std::unexpected(std::move(result.error()));
		}
		const auto tableMap = hitTable->Map(0, &noReads, &mapped);
		if(FAILED(tableMap)) return std::unexpected(MakeDirectXError(70, tableMap, "Optical HitTable Map failed."));
		for(uint32_t i = 0; i < geometries_.size(); ++i) {
			auto* record = static_cast<uint8_t*>(mapped) + static_cast<size_t>(i) * 64;
			std::memset(record, 0, 64); std::memcpy(record, hitIdentifier_.data(), hitIdentifier_.size());
			std::memcpy(record + 32, &i, sizeof(i)); // ローカルb1へ形状・材質レコードの番号を格納する
		}
		hitTable->Unmap(0, nullptr);
		// 実行中の全フレームから参照されなくなった属性viewだけを回収する。
		// 通常更新時にQueueを停止せず、形状の世代交代による使用量を制限する。
		for(auto it = views_.begin(); it != views_.end();) {
			const bool retained = std::any_of(activeGenerations_.begin(), activeGenerations_.end(), [&](const auto& frame) {
				return std::find(frame.begin(), frame.end(), it->first) != frame.end();
			});
			if(!retained) { ReleaseViews(it->second); it = views_.erase(it); } else ++it;
		}
		bindless_->CollectGarbage(queue_->GetCompletedFenceValue());
		Constants constants; constants.environment = environment; constants.inverseViewProjection = *inverse;
		constants.lightDirection = {light.direction[0], light.direction[1], light.direction[2], light.intensity};
		constants.lightColor = {light.color[0], light.color[1], light.color[2], 0};
		constants.indices = {outputUav_.index, static_cast<uint32_t>(instanceBytes), settings.maxBounces, settings.enabled ? 1U : 0U};
		constants.giOptions = {gi.enabled ? 1U : 0U, gi.samplesPerPixel, gi.maxBounces, giUav_.index};
		constants.giParameters[0] = gi.intensity;
		constants.reflectionTargets={reflectionUav_.index,guideUav_.index,localReflections?1U:0U,0};
		constants.bakedLighting = baked;
		constants.rays = {settings.rayBias, settings.rayDistance, static_cast<float>(settings.reflectionSamples), settings.metallicReflections ? 1.f : 0.f};
		std::copy(ambientIrradiance.begin(), ambientIrradiance.end(), constants.ambient.begin());
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> commands;
		const auto queried = static_cast<ID3D12GraphicsCommandList*>(nativeCommands)->QueryInterface(IID_PPV_ARGS(&commands));
		if(FAILED(queried)) return std::unexpected(MakeDirectXError(70, queried, "Optical CommandList4 unavailable."));
		// AS構築・描画より前に失敗し得る割り当てを済ませる。記録が破棄された場合は
		// 完了callbackでCPU状態を復元し、提出した資源はフレームFenceまで保持する。
		initialState_ = state_; recording_ = true;
		const uint32_t timingBase=context.GetFrameIndex()*5;
		BeginTimings(commands.Get(),context.GetFrameIndex());
		auto update = scene_.Update(commands.Get(), context.GetFrameIndex(), accelerationDraws_, hitGroupOffsets_);
		if(!update) return std::unexpected(std::move(update.error()));
		commands->EndQuery(timingQueries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,timingBase+1);
		const auto base = shaderTable_->GetGPUVirtualAddress();
		D3D12_DISPATCH_RAYS_DESC probeDispatch {};
		probeDispatch.MissShaderTable={base+64,64,64};probeDispatch.HitGroupTable={hitTable->GetGPUVirtualAddress(),hitTableBytes,64};
		if(ddgi.enabled && ddgi_.NeedsUpdate()) {
			ddgi_.Begin(commands.Get());
			constants.ddgi=ddgi_.GetDrawData();
			constants.probeTrace={ddgi_.RayUav(),ddgi_.Phase(),ddgi.feedback?1U:0U,0};
			auto write=probeConstants_.Write(context.GetFrameIndex(),{reinterpret_cast<const uint8_t*>(&constants),sizeof(constants)});
			if(!write) return std::unexpected(std::move(write.error()));
			commands->SetComputeRootSignature(root_.Get());commands->SetPipelineState1(pipeline_.Get());
			commands->SetComputeRootConstantBufferView(0,probeConstants_.GetGpuAddress(context.GetFrameIndex()));
			commands->SetComputeRootShaderResourceView(1,scene_.GetTlasAddress(context.GetFrameIndex()));
			commands->SetComputeRootShaderResourceView(2,data->GetGPUVirtualAddress());
			probeDispatch.RayGenerationShaderRecord={base+192,64};
			probeDispatch.Width=ddgi.raysPerProbe;probeDispatch.Height=ddgi.counts[0]*ddgi.counts[1]*ddgi.counts[2];probeDispatch.Depth=1;
			commands->DispatchRays(&probeDispatch);
			auto blended=ddgi_.Update(commands.Get(),context.GetFrameIndex());if(!blended) return std::unexpected(std::move(blended.error()));
		}
		commands->EndQuery(timingQueries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,timingBase+2);
		if(!screenPass) {
			commands->EndQuery(timingQueries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,timingBase+3);
			commands->EndQuery(timingQueries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,timingBase+4);
			ResolveTimings(commands.Get(),context.GetFrameIndex());return ShaderResourceRef {};
		}
		constants.ddgi=ddgi.enabled?ddgi_.GetDrawData(ddgi.intensity):DDGIDrawData{};
		auto written=constants_.Write(context.GetFrameIndex(),{reinterpret_cast<const uint8_t*>(&constants),sizeof(constants)});
		if(!written) return std::unexpected(std::move(written.error()));
		OutputBarrier(commands.Get(), output_.Get(), state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		OutputBarrier(commands.Get(), giOutput_.Get(), state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		OutputBarrier(commands.Get(), reflection_.Get(), state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		OutputBarrier(commands.Get(), reflectionGuide_.Get(), state_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		commands->SetComputeRootSignature(root_.Get()); commands->SetPipelineState1(pipeline_.Get());
		commands->SetComputeRootConstantBufferView(0, constants_.GetGpuAddress(context.GetFrameIndex()));
		commands->SetComputeRootShaderResourceView(1, scene_.GetTlasAddress(context.GetFrameIndex()));
		commands->SetComputeRootShaderResourceView(2, data->GetGPUVirtualAddress());
		D3D12_DISPATCH_RAYS_DESC dispatch {};
		dispatch.RayGenerationShaderRecord = {base, 64}; dispatch.MissShaderTable = {base + 64, 64, 64};
		dispatch.HitGroupTable = {hitTable->GetGPUVirtualAddress(), hitTableBytes, 64};
		dispatch.Width = context.GetRenderWidth(); dispatch.Height = context.GetRenderHeight(); dispatch.Depth = 1;
		commands->DispatchRays(&dispatch);
		commands->EndQuery(timingQueries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,timingBase+3);
		if(localReflections) {
		OutputBarrier(commands.Get(), reflection_.Get(), state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		OutputBarrier(commands.Get(), reflectionGuide_.Get(), state_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		D3D12_RESOURCE_BARRIER ready{};ready.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;ready.UAV.pResource=output_.Get();
		commands->ResourceBarrier(1,&ready);
		const uint32_t filterConstants[]{reflectionSrv_.index,guideSrv_.index,outputUav_.index,dispatch.Width,dispatch.Height,settings.reflectionDenoising?1U:0U};
		commands->SetComputeRootSignature(filterRoot_.Get());commands->SetPipelineState(filterPipeline_.Get());
		commands->SetComputeRoot32BitConstants(0,6,filterConstants,0);
		commands->Dispatch((dispatch.Width+7)/8,(dispatch.Height+7)/8,1);
		commands->EndQuery(timingQueries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,timingBase+4);
		ResolveTimings(commands.Get(),context.GetFrameIndex());
		OutputBarrier(commands.Get(), reflection_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		OutputBarrier(commands.Get(), reflectionGuide_.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		} else {
			commands->EndQuery(timingQueries_.Get(),D3D12_QUERY_TYPE_TIMESTAMP,timingBase+4);
			ResolveTimings(commands.Get(),context.GetFrameIndex());
			OutputBarrier(commands.Get(),reflection_.Get(),state_,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			OutputBarrier(commands.Get(),reflectionGuide_.Get(),state_,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		}
		// UAVからSRVへ遷移し、Rasterが光学出力・GIを読む前に書き込みを完了させる。
		OutputBarrier(commands.Get(), output_.Get(), state_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		OutputBarrier(commands.Get(), giOutput_.Get(), state_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE); state_ = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		return outputSrv_;
	}
	void OpticalBackend::CompleteRecording(void* owner, bool submitted, uint32_t frame) noexcept {
		auto& backend = *static_cast<OpticalBackend*>(owner);
		backend.ddgi_.CompleteRecording(submitted);
		if(backend.recording_ && frame<backend.timingReady_.size())
			backend.timingReady_[frame]=submitted?backend.timingRecorded_[frame]:0;
		if(backend.recording_ && !submitted) { backend.scene_.InvalidateRecording(frame); backend.state_ = backend.initialState_; }
		backend.recording_ = false;
	}
	void OpticalBackend::ReleaseViews(GeometryViews& views) noexcept {
		for(auto* view : {&views.vertexView, &views.indexView}) if(view->IsValid()) {
			auto result = bindless_->Retire(*view, queue_->GetCompletedFenceValue()); if(!result) NEXUS_LOG_ERROR("Optics", result.error().GetMessageText()); *view = {};
		}
	}
	void OpticalBackend::Shutdown() noexcept {
		if(queue_ && supported_) {
			auto result = queue_->WaitForIdle(); if(!result) NEXUS_LOG_ERROR("Optics", result.error().GetMessageText());
			ReleaseOutput(); for(auto& [generation, views] : views_) { (void)generation; ReleaseViews(views); }
			bindless_->CollectGarbage(queue_->GetCompletedFenceValue());
		}
		ddgi_.Shutdown();probeConstants_.Shutdown();
		views_.clear(); metadata_.clear(); hitTables_.clear(); activeGenerations_.clear(); scene_.Shutdown(); constants_.Shutdown();
		shaderTable_.Reset(); pipeline_.Reset(); root_.Reset(); filterPipeline_.Reset();filterRoot_.Reset();device_.Reset(); queue_ = nullptr; bindless_ = nullptr;
		pipelineReady_ = false;
		timingReadback_.Reset();timingQueries_.Reset();timingReady_.clear();timingRecorded_.clear();timings_={};
	}
}
