// Native integration tests live outside Runtime/Renderer. They exercise the real DX12
// backend without a Window/ImGui session and fail on GPU validation errors.
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <iostream>
#include <fstream>
#include "Samples/OpticalDemoScene.h"
#include "Graphics/Model/ModelLoader.h"
#include <stdexcept>

#include "Graphics/Backend/DirectX12/Shadow/ShadowBackend.h"
#include "Graphics/Core/CommandQueue.h"
#include "Graphics/Descriptor/BindlessDescriptorTable.h"
#include "Graphics/Descriptor/DescriptorManager.h"
#include "Graphics/Descriptor/TransientDescriptorArena.h"
#include "Graphics/Device/GraphicsDevice.h"
#include "Graphics/Model/ModelAssetData.h"
#include "Graphics/Renderer/GraphicsContext.h"
#include "Graphics/Renderer/GraphicsResourceFactory.h"
#include "Graphics/Renderer/PrimitiveRenderer.h"
#include "Graphics/Lighting/LightBaker.h"
#include "Graphics/Lighting/LightmapGeometry.h"
#include "Graphics/Shadow/ShadowSystem.h"
#include "Foundation/Logging/Logger.h"

namespace NexusEngine {
	class GraphicsBackendTestAccess {
	public:
		static void Connect(GraphicsResourceFactory& factory, GraphicsDevice& device, CommandQueue& queue,
			BindlessDescriptorTable& bindless, DescriptorManager& descriptors, bool forceUnsupported = false) {
			factory.ConnectBackend(device.GetDevice(), &queue, &bindless, 2, &descriptors,
				forceUnsupported ? GraphicsCapabilities {} : device.GetCapabilities());
		}
		static GraphicsContext Context(ID3D12GraphicsCommandList* commands, BindlessDescriptorTable& bindless,
			uint32_t frame, uint32_t width, uint32_t height) {
			return GraphicsContext(commands, &bindless, nullptr, frame, width, height);
		}
		static void Targets(GraphicsContext& context, uint64_t rtv, uint64_t dsv, TransientDescriptorArena* arena) {
			context.nativeRtv_ = rtv; context.nativeDsv_ = dsv; context.transientDescriptors_ = arena;
		}
		static void Submitted(GraphicsContext& context) { context.CompleteRecording(true); }
	};
}

namespace {
	using namespace NexusEngine;
	using Microsoft::WRL::ComPtr;
	void Require(bool condition, const char* message) { if(!condition) throw std::runtime_error(message); }
	template<class T> void Check(const Result<T>& result) {
		if(!result) throw std::runtime_error(result.error().GetMessageText());
	}
	void Native(HRESULT result) { if(FAILED(result)) throw std::runtime_error("Native DX12 operation failed: " + std::to_string(result)); }

	struct Fixture {
		GraphicsDevice device;
		CommandQueue queue;
		DescriptorManager descriptors;
		BindlessDescriptorTable bindless;
		GraphicsResourceFactory factory;
		ShadowBackend shadows;
		MeshResource mesh;
		ModelAssetData asset;
		ComPtr<ID3D12CommandAllocator> allocator;
		ComPtr<ID3D12GraphicsCommandList> commands;
		ComPtr<ID3D12InfoQueue> info;

		explicit Fixture(bool warp, bool forceUnsupported = false, bool largeScene = false) {
			// Tests require validation even for optimized Release builds. Production Release
			// keeps its existing diagnostics policy; the native test fixture enables it explicitly.
			ComPtr<ID3D12Debug1> debug;
			Native(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
			debug->EnableDebugLayer(); debug->SetEnableGPUBasedValidation(TRUE);
			GraphicsDeviceDesc desc;
			desc.useWarpAdapter = warp; desc.enableDebugLayer = true; desc.enableGpuBasedValidation = true;
			Check(device.Initialize(desc));
			if(SUCCEEDED(device.GetDevice()->QueryInterface(IID_PPV_ARGS(&info)))) {
				Native(info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE));
				Native(info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE));
				info->ClearStoredMessages();
			}
			Check(queue.Initialize(device.GetDevice()));
			Check(descriptors.Initialize(device.GetDevice(), largeScene ? 4096U : 512U, 32, 32, largeScene ? 128U : 16U));
			Check(bindless.Initialize(device.GetDevice(), &descriptors));
			GraphicsBackendTestAccess::Connect(factory, device, queue, bindless, descriptors, forceUnsupported);
			Check(shadows.Initialize(device.GetDevice(), &queue, &descriptors, &bindless, &factory,
				2, forceUnsupported ? GraphicsCapabilities {} : device.GetCapabilities(), L"Project/Shaders"));
			Native(device.GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
			Native(device.GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)));
			Native(commands->Close());
			CreateMesh();
		}
		void CreateMesh(bool negativeBaseVertex = false) {
			// Camera looks down +Z. Receiver at z=.7 and a smaller occluder at z=.4;
			// oblique light separates visible receiver pixels from the camera silhouette.
			std::array<ModelVertex, 8> vertices;
			const float positions[8][3] { {-0.9f,-0.9f,0.7f}, {-0.9f,0.9f,0.7f}, {0.9f,0.9f,0.7f}, {0.9f,-0.9f,0.7f},
				{-0.2f,-0.2f,0.4f}, {-0.2f,0.2f,0.4f}, {0.2f,0.2f,0.4f}, {0.2f,-0.2f,0.4f} };
			for(size_t i = 0; i < vertices.size(); ++i) {
				std::copy(std::begin(positions[i]), std::end(positions[i]), vertices[i].position);
				vertices[i].normal[1] = 0; vertices[i].normal[2] = -1;
				vertices[i].texcoord[0] = (positions[i][0] + 1) * 0.5f;
				vertices[i].texcoord[1] = (1 - positions[i][1]) * 0.5f;
			}
			std::array<uint16_t, 12> indices { 0,1,2,0,2,3,4,5,6,4,6,7 };
			if(negativeBaseVertex) for(auto& index : indices) ++index;
			Check(factory.CreateMeshResource(mesh,
				{ reinterpret_cast<const uint8_t*>(vertices.data()), sizeof(vertices) }, sizeof(ModelVertex),
				{ reinterpret_cast<const uint8_t*>(indices.data()), sizeof(indices) }, IndexFormat::UInt16,
				{ {0,12,negativeBaseVertex ? -1 : 0,0} }));
			asset = {};
			MeshAssetData cpuMesh;
			const auto* vertexBytes = reinterpret_cast<const uint8_t*>(vertices.data());
			const auto* indexBytes = reinterpret_cast<const uint8_t*>(indices.data());
			cpuMesh.vertexData.assign(vertexBytes, vertexBytes + sizeof(vertices)); cpuMesh.vertexStride = sizeof(ModelVertex);
			cpuMesh.indexData.assign(indexBytes, indexBytes + sizeof(indices)); cpuMesh.indexFormat = IndexFormat::UInt16;
			cpuMesh.submeshes = { {0,12,0,0} }; asset.meshes.push_back(std::move(cpuMesh));
			ModelNodeAssetData node; node.meshIndices = {0}; asset.nodes.push_back(node); asset.materials.emplace_back();
		}
		std::vector<float> Frame(ShadowSettings settings, uint32_t frame = 0, Matrix4x4 world = {}, uint32_t width = 64) {
			Check(queue.WaitForIdle()); Native(allocator->Reset()); Native(commands->Reset(allocator.Get(), nullptr));
			bindless.Bind(commands.Get());
			auto context = GraphicsBackendTestAccess::Context(commands.Get(), bindless, frame, width, 64);
			ShadowDrawItem draw; draw.mesh = &mesh; draw.world = world; draw.instanceId = 1;
			DirectionalLightData light; light.direction = { 0.7f, 0, -1 };
			Check(shadows.Render(context, commands.Get(), Matrix4x4::Identity(), light, { &draw, 1 }, settings));
			if(!settings.enabled) {
				Native(commands->Close()); queue.Execute(commands.Get()); ShadowBackend::CompleteRecording(&shadows, true, frame); Check(queue.WaitForIdle()); return {};
			}
			// Read back actual Mask output. This proves visibility, not merely successful PSO creation.
			shadows.Transition(commands.Get(), shadows.mask, D3D12_RESOURCE_STATE_COPY_SOURCE);
			const auto desc = shadows.mask.resource->GetDesc();
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {}; uint64_t total = 0;
			device.GetDevice()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
			D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_READBACK;
			D3D12_RESOURCE_DESC buffer {}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			buffer.Width = total; buffer.Height = 1; buffer.DepthOrArraySize = 1; buffer.MipLevels = 1;
			buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			ComPtr<ID3D12Resource> readback;
			Native(device.GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
				D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
			D3D12_TEXTURE_COPY_LOCATION source {}, destination {};
			source.pResource = shadows.mask.resource.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			destination.pResource = readback.Get(); destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			destination.PlacedFootprint = footprint;
			commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
			shadows.Transition(commands.Get(), shadows.mask, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			Native(commands->Close()); queue.Execute(commands.Get()); ShadowBackend::CompleteRecording(&shadows, true, frame); Check(queue.WaitForIdle());
			void* mapped = nullptr; Native(readback->Map(0, nullptr, &mapped));
			std::vector<float> pixels(static_cast<size_t>(width) * 64);
			for(uint32_t y = 0; y < 64; ++y) {
				std::memcpy(pixels.data() + static_cast<size_t>(y) * width,
					static_cast<uint8_t*>(mapped) + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch, width * sizeof(float));
			}
			readback->Unmap(0, nullptr);
			for(float visibility : pixels) Require(std::isfinite(visibility) && visibility >= 0 && visibility <= 1, "Mask visibility is invalid.");
			return pixels;
		}
		void Validate() {
			if(!info) throw std::runtime_error("Debug InfoQueue unavailable; validation evidence is missing.");
			bool failed = false;
			for(uint64_t i = 0; i < info->GetNumStoredMessages(); ++i) {
				SIZE_T length = 0; Native(info->GetMessage(i, nullptr, &length));
				std::vector<uint8_t> storage(length);
				auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
				Native(info->GetMessage(i, message, &length));
				if(message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) {
					std::cerr << message->pDescription << '\n';
					failed = true;
				}
			}
			Require(!failed, "D3D12 validation warning/error.");
		}
		~Fixture() {
			static_cast<void>(queue.WaitForIdle()); shadows.Shutdown(); mesh.Shutdown();
			commands.Reset(); allocator.Reset(); bindless.Shutdown(); descriptors.Shutdown(); queue.Shutdown();
		}
	};
	void Visibility(const std::vector<float>& pixels) {
		Require(std::any_of(pixels.begin(), pixels.end(), [](float v) { return v < 0.5f; }), "No shadow pixels were produced.");
		Require(std::any_of(pixels.begin(), pixels.end(), [](float v) { return v > 0.99f; }), "No lit pixels were produced.");
	}
	void Run(bool warp, bool forceUnsupported = false) {
		Fixture test(warp, forceUnsupported);
		ShadowSettings settings; settings.shadowMapExtent = 2; settings.shadowMapResolution = 256;
		settings.mode = ShadowMode::ShadowMap;
		Visibility(test.Frame(settings));
		Require(test.shadows.GetStatistics().technique == ShadowTechnique::ShadowMap, "Forced Map selection failed.");
		settings.mode = ShadowMode::Auto;
		Visibility(test.Frame(settings));
		const auto capabilities = test.shadows.capabilities;
		if(capabilities.SupportsRayTracing()) {
			Require(test.shadows.GetStatistics().technique == ShadowTechnique::RayTracing, "Supported GPU failed Auto RT selection.");
			const auto baseline = test.shadows.GetStatistics();
			test.Frame(settings, 1); test.Frame(settings, 0);
			Require(test.shadows.GetStatistics().blasBuildCount == baseline.blasBuildCount, "Static BLAS was rebuilt.");
			const auto warm = test.shadows.GetStatistics();
			test.Frame(settings, 0, MakeTranslationMatrix({0.1f,0,0}));
			Require(test.shadows.GetStatistics().tlasUpdateCount == warm.tlasUpdateCount + 1, "Transform change did not update TLAS.");
			Require(test.shadows.GetStatistics().blasBuildCount == warm.blasBuildCount, "Transform change rebuilt BLAS.");
			settings.softShadow = true; settings.sampleCount = 4;
			Visibility(test.Frame(settings));
			Check(test.queue.WaitForIdle()); test.mesh.Shutdown(); test.CreateMesh();
			test.Frame(settings);
			Require(test.shadows.GetStatistics().blasBuildCount == warm.blasBuildCount + 1, "Geometry generation did not rebuild BLAS.");
		} else {
			Require(test.shadows.GetStatistics().technique == ShadowTechnique::ShadowMap, "Unsupported GPU failed Auto fallback.");
			settings.mode = ShadowMode::RayTracing;
			Visibility(test.Frame(settings));
			Require(test.shadows.GetStatistics().fallback, "Unsupported explicit RT did not report fallback.");
		}
		settings.mode = ShadowMode::ShadowMap; settings.shadowMapResolution = 512;
		Visibility(test.Frame(settings, 0, {}, 80));
		settings.enabled = false; test.Frame(settings);
		Require(test.shadows.GetStatistics().technique == ShadowTechnique::Disabled, "OFF did not disable shadow passes.");
		const auto oldMask = test.shadows.mask.srv;
		test.shadows.Shutdown();
		Require(!test.bindless.IsValid(oldMask), "Shadow Mask descriptor survived shutdown.");
		auto allResourceSlots = test.descriptors.Resources().Allocate(511);
		Check(allResourceSlots); Check(test.descriptors.Resources().Free(*allResourceSlots));
		auto allDepthSlots = test.descriptors.DepthStencils().Allocate(32);
		Check(allDepthSlots); Check(test.descriptors.DepthStencils().Free(*allDepthSlots));
		test.Validate();
		std::cout << (warp ? "WARP" : "Hardware") << (forceUnsupported ? " (forced unsupported capability)" : "")
			<< " Shadow tests passed; RayTracing=" << capabilities.SupportsRayTracing() << '\n';
	}
	#include "GlobalIlluminationTests.inl"
	#include "BakedLightingTests.inl"
	#include "DDGITests.inl"
	std::vector<uint8_t> ForwardIntegration(bool forceUnsupported, bool opticalDemo = false, bool sponza = false, uint32_t pbrVariant = 0, bool fogTest = false, bool iblTest = false, uint32_t giVariant = 0, uint32_t bakedVariant = 0, uint32_t bakedChartSize = 4,uint32_t ddgiVariant = 0) {
		Fixture test(false, forceUnsupported, sponza);
        const uint32_t width = sponza ? 384U : (opticalDemo ? 256U : 64U), height = sponza ? 216U : (opticalDemo ? 192U : 64U);
        uint32_t glassSlot = kOpticalGlassMaterial;
        if(sponza) {
            auto asset = ModelLoader{}.LoadGltf(L"Project/Resources/Assets/Models/Sponza/Sponza.gltf"); Check(asset);
            const size_t originalMeshes = asset->meshes.size(), originalNodes = asset->nodes.size();
            auto spheres = AppendSponzaOpticalSpheres(*asset); Check(spheres); glassSlot = spheres->glass;
            Require(asset->meshes.size() == originalMeshes + 2 && asset->nodes.size() == originalNodes + 2,
                "Appending spheres did not preserve Sponza geometry/hierarchy.");
            test.asset = std::move(*asset);
        }
        else if(opticalDemo) { auto asset = CreateOpticalDemoScene(); Check(asset); test.asset = std::move(*asset); }
        else test.asset.materials[0].baseColorFactor = {0.35f,0.35f,0.35f,1}; // Avoid UNORM saturation when comparing shadow lighting.
		if(giVariant > 0 && (giVariant < 4 || giVariant == 5)) test.asset = MakeGITestRoom(giVariant);
		if(bakedVariant>0 && bakedVariant<=3) test.asset=MakeGITestRoom(bakedVariant);
		if(ddgiVariant!=0 && ddgiVariant!=4) test.asset=MakeDDGITestRoom(ddgiVariant<=3?ddgiVariant:1);
		if(pbrVariant != 0) {
			auto& material = test.asset.materials[0];
			material.roughnessFactor = pbrVariant == 1 || pbrVariant == 2 ? 0.1f : 1;
			material.metallicFactor = pbrVariant == 2 || pbrVariant == 5 ? 1.0f : 0.0f;
			auto pixelImage = [](uint8_t r, uint8_t g, uint8_t b) {
				std::vector<uint8_t> bytes(58, 0);
				bytes[0] = 'B'; bytes[1] = 'M'; bytes[2] = 58; bytes[10] = 54; bytes[14] = 40;
				bytes[18] = 1; bytes[22] = 1; bytes[26] = 1; bytes[28] = 24;
				bytes[54] = b; bytes[55] = g; bytes[56] = r;
				return bytes;
			};
			if(pbrVariant == 3 || pbrVariant == 6 || pbrVariant == 7) material.normalImageData = pixelImage(230, 128, 204);
			if(pbrVariant == 4) material.normalImageData = pixelImage(128, 128, 255);
			if(pbrVariant == 5) material.metallicRoughnessImageData = pixelImage(255, 255, 0);
			if(pbrVariant == 8) {
				material.emissiveFactor = {2, 0.4f, 0.1f};
				material.emissiveImageData = pixelImage(128, 255, 255);
				material.occlusionImageData = pixelImage(0, 255, 255);
			}
			if(pbrVariant == 6 || pbrVariant == 7) {
				auto& mesh = test.asset.meshes[0];
				for(size_t offset = 0; offset < mesh.vertexData.size(); offset += sizeof(ModelVertex)) {
					ModelVertex vertex;
					std::memcpy(&vertex, mesh.vertexData.data() + offset, sizeof(vertex));
					vertex.tangent[0] = 1; vertex.tangent[3] = 1;
					std::memcpy(mesh.vertexData.data() + offset, &vertex, sizeof(vertex));
				}
			}
		}
		TransientDescriptorArena transient;
		Check(transient.Initialize(test.device.GetDevice(), &test.descriptors.Resources(), 2, sponza ? 512U : 16U));
		ModelInstance pose; Check(pose.Initialize(test.asset));
		RenderScene scene; scene.camera = RenderCamera {};
        if(opticalDemo) {
            scene.camera->viewMatrix = *TryInverse(MakeAffineMatrix(Vector3::One(),
                Quaternion::FromEulerRadians(sponza ? Vector3{0.12f,1.57079633f,0} : Vector3{0.08f,0,0}),
                sponza ? Vector3{-7.5f,1.7f,0} : Vector3{0,2,-7}));
            scene.camera->projectionMatrix = MakePerspectiveFovMatrix(1.04719755f, static_cast<float>(width)/height, 0.1f, 1000);
            scene.camera->viewProjectionMatrix = Multiply(scene.camera->viewMatrix, scene.camera->projectionMatrix);
        }
		scene.directionalLight.direction = { 0.7f, 0, -1 };
		scene.directionalLight.intensity = opticalDemo ? 10.0f : 3.0f;
		if(sponza) {
			scene.directionalLight.direction = {0.4f, 1.0f, 0.3f};
			// Open courtyard sky contributes diffuse irradiance to shaded interiors.
			scene.ambientIrradiance = {0.5f, 0.6f, 0.8f};
		}
		if(iblTest) { scene.environment.imagePath=L"Generated/IBLDirectional.hdr"; scene.directionalLight.intensity=0; scene.ambientIrradiance={0,0,0}; }
		if(giVariant != 0) {
            if(giVariant==5) scene.environment.imagePath=L"Generated/GIWhite.hdr";
            scene.globalIllumination.enabled=true; scene.globalIllumination.samplesPerPixel=32; scene.globalIllumination.maxBounces=2;
            if(giVariant<4 || giVariant==5) { scene.directionalLight.intensity=0; scene.ambientIrradiance=giVariant==1 || giVariant==5?std::array<float,3>{1,1,1}:std::array<float,3>{0,0,0}; }
        }
        scene.primitives.push_back({ {1}, Matrix4x4::Identity(), {1,1,1,1} });
		if(bakedVariant) {
			scene.bakedLighting.cachePath=L"Generated/BakedGI"+std::to_wstring(bakedVariant)+L".nxgi";
			scene.bakedLighting.samples=128;scene.bakedLighting.probeCounts={4,4,4};
			scene.bakedLighting.chartSize=bakedChartSize;
			scene.directionalLight.intensity=0;scene.ambientIrradiance=bakedVariant==1?std::array<float,3>{1,1,1}:std::array<float,3>{0,0,0};
			if(bakedVariant==4) scene.ambientIrradiance={1,1,1};
			// ベイクがRTGIを確実に抑止することも検証する。
			scene.globalIllumination.enabled=false;
		}
		PrimitiveRenderer renderer;
		if(ddgiVariant) {
			scene.globalIllumination.enabled=true; // DDGIが旧RTGIを抑止することも検証する。
			scene.ddgi.enabled=true;scene.ddgi.raysPerProbe=128;scene.ddgi.hysteresis=.8f;scene.ddgi.maxDistance=20;
			scene.ddgi.origin={-.75f,-.75f,-.75f};scene.ddgi.spacing={.5f,.5f,.5f};scene.ddgi.counts={4,4,4};
			scene.ambientIrradiance=ddgiVariant==1 || ddgiVariant==6?std::array<float,3>{1,1,1}:std::array<float,3>{0,0,0};
			if(ddgiVariant==6) scene.ddgi.origin[1]=-1; // 床面上の格子点からの光漏れと再配置を検証する。
			scene.directionalLight.intensity=ddgiVariant==5?3.0f:0;
			scene.directionalLight.direction={0,1,0};scene.environment.debugMode=LightingDebugMode::DiffuseGI;
			if(ddgiVariant==4) {scene.ddgi.origin={-4,-.1f,-2};scene.ddgi.spacing={2,2,2};scene.ddgi.counts={5,3,4};scene.ambientIrradiance={.5f,.6f,.8f};scene.environment.debugMode=LightingDebugMode::Final;}
			else {scene.primitives.clear();for(uint32_t node=0;node<test.asset.nodes.size();++node) scene.primitives.push_back({{uint64_t(node)+1},Matrix4x4::Identity(),{1,1,1,1},node});}
		}
		renderer.SetModelInput(&test.asset, &pose); renderer.SetRenderScene(&scene);
		Check(renderer.Initialize({ test.factory, L"Project/Shaders", L"Project/Resources/Assets" }));
		if(pbrVariant == 7) {
			Require(renderer.AreMeshShadersAvailable(), "Mesh Shader PBR pipeline failed to initialize.");
			renderer.GetMeshletSettings().enabled = true;
		}
		ShadowTexture depth; Check(test.shadows.CreateTexture(depth, width, height, true));
		auto rtv = test.descriptors.RenderTargets().Allocate(); Check(rtv);
		D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC colorDesc {};
		colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; colorDesc.Width = width; colorDesc.Height = height;
		colorDesc.DepthOrArraySize = 1; colorDesc.MipLevels = 1; colorDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		colorDesc.SampleDesc.Count = 1; colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		D3D12_CLEAR_VALUE clear {}; clear.Format = colorDesc.Format;
		ComPtr<ID3D12Resource> color;
		Native(test.device.GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &colorDesc,
			D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&color)));
		const auto rtvHandle = test.descriptors.RenderTargets().GetCpuHandle(*rtv);
		const auto dsvHandle = test.descriptors.DepthStencils().GetCpuHandle(depth.dsv);
		test.device.GetDevice()->CreateRenderTargetView(color.Get(), nullptr, rtvHandle);
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {}; uint64_t total = 0;
		test.device.GetDevice()->GetCopyableFootprints(&colorDesc, 0, 1, 0, &footprint, nullptr, nullptr, &total);
		heap.Type = D3D12_HEAP_TYPE_READBACK;
		D3D12_RESOURCE_DESC buffer {}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = total;
		buffer.Height = 1; buffer.DepthOrArraySize = 1; buffer.MipLevels = 1; buffer.SampleDesc.Count = 1;
		buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ComPtr<ID3D12Resource> readback;
		Native(test.device.GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
		uint32_t renderFrame = 0;
		auto render = [&](ShadowMode mode, bool enabled) {
			ShadowSettings settings; settings.mode = mode; settings.enabled = enabled;
			settings.shadowMapExtent = 2; settings.shadowMapResolution = 256;
			// Small low-resolution test volume needs enough comparison bias for texel quantization.
			settings.shadowMapBias = 0.005f;
			Check(renderer.GetShadows().SetSettings(settings));
			Check(test.queue.WaitForIdle()); Native(test.allocator->Reset()); Native(test.commands->Reset(test.allocator.Get(), nullptr));
			const uint32_t frame = renderFrame++ % 2;
			transient.ResetFrame(frame); test.bindless.Bind(test.commands.Get());
			auto context = GraphicsBackendTestAccess::Context(test.commands.Get(), test.bindless, frame, width, height);
			GraphicsBackendTestAccess::Targets(context, rtvHandle.ptr, dsvHandle.ptr, &transient);
			context.RestoreMainRenderTargets();
			test.commands->ClearRenderTargetView(rtvHandle, clear.Color, 0, nullptr);
			test.commands->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
			renderer.Render(context);
			D3D12_RESOURCE_BARRIER barrier {}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = color.Get(); barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
			test.commands->ResourceBarrier(1, &barrier);
			D3D12_TEXTURE_COPY_LOCATION source {}, destination {};
			source.pResource = color.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			destination.pResource = readback.Get(); destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; destination.PlacedFootprint = footprint;
			test.commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
			std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter); test.commands->ResourceBarrier(1, &barrier);
			Native(test.commands->Close()); test.queue.Execute(test.commands.Get()); GraphicsBackendTestAccess::Submitted(context);
			Check(test.queue.WaitForIdle());
			void* mapped = nullptr; Native(readback->Map(0, nullptr, &mapped));
			std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
			// Keep the existing lighting comparisons in linear 8-bit space, while the
			// actual renderer target now retains HDR radiance in half floats.
			for(size_t y = 0; y < height; ++y) for(size_t x = 0; x < width; ++x) for(size_t channel = 0; channel < 4; ++channel) {
				uint16_t half = 0;
				std::memcpy(&half, static_cast<uint8_t*>(mapped) + footprint.Offset + y * footprint.Footprint.RowPitch + x * 8 + channel * 2, 2);
				const int exponent = (half >> 10) & 31;
				const float value = exponent == 0 ? std::ldexp(static_cast<float>(half & 1023), -24)
					: exponent == 31 ? 1.0f : std::ldexp(1.0f + static_cast<float>(half & 1023) / 1024.0f, exponent - 15);
				pixels[(y * width + x) * 4 + channel] = static_cast<uint8_t>(std::lround(std::clamp((half & 0x8000) ? -value : value, 0.0f, 1.0f) * 255));
			}
			readback->Unmap(0, nullptr); return pixels;
		};
		// Record an entire first shadow frame, then discard it. Context destruction must
		// restore CPU resource states and invalidate BLAS/TLAS builds that never reached GPU.
		{
			ShadowSettings settings; settings.shadowMapExtent = 2; settings.shadowMapResolution = 256;
			Check(renderer.GetShadows().SetSettings(settings));
			Check(test.queue.WaitForIdle()); Native(test.allocator->Reset()); Native(test.commands->Reset(test.allocator.Get(), nullptr));
			transient.ResetFrame(0); test.bindless.Bind(test.commands.Get());
			auto context = GraphicsBackendTestAccess::Context(test.commands.Get(), test.bindless, 0, width, height);
			GraphicsBackendTestAccess::Targets(context, rtvHandle.ptr, dsvHandle.ptr, &transient);
			context.RestoreMainRenderTargets(); renderer.Render(context); Native(test.commands->Close());
			// No Execute/Submitted call: the following actual frames must recover correctly.
		}
		if(giVariant != 0) {
            return ValidateGIIntegration(giVariant,forceUnsupported,scene,renderer,render,test,depth,rtv,transient,width,height);
        }
		if(bakedVariant) return ValidateBakedIntegration(bakedVariant,scene,renderer,render,test,depth,rtv,transient,width,height);
		if(ddgiVariant) return ValidateDDGIIntegration(ddgiVariant,forceUnsupported,scene,renderer,render,test,depth,rtv,transient,width,height);
        if(iblTest) {
            auto visible = [](const auto& pixels) { size_t count=0; for(size_t i=0;i<pixels.size();i+=4) if(pixels[i]+pixels[i+1]+pixels[i+2]>3) ++count; return count; };
            auto writeImage = [&](const std::filesystem::path& path,const auto& pixels) {
                std::ofstream file(path,std::ios::binary); file << "P6\n" << width << ' ' << height << "\n255\n";
                for(size_t i=0;i<pixels.size();i+=4) file.write(reinterpret_cast<const char*>(pixels.data()+i),3);
            };
            auto on=render(ShadowMode::Auto,false);
            Require(visible(on)>100,"Environment-only raster/optical lighting produced no visible geometry.");
            if(!opticalDemo) {
                Require(render(ShadowMode::ShadowMap,true)==on,"Shadow Map incorrectly attenuated environment lighting.");
                Require(render(ShadowMode::RayTracing,true)==on,"RT shadow incorrectly attenuated environment lighting.");
                renderer.GetMeshletSettings().enabled=true;
                Require(renderer.AreMeshShadersAvailable(),"IBL Mesh Shader pipeline unavailable.");
                auto mesh=render(ShadowMode::Auto,false); Require(mesh==on,"Raster/Mesh Shader IBL mismatch.");
                renderer.GetMeshletSettings().enabled=false;
                scene.environment.debugMode=LightingDebugMode::Direct;
                Require(visible(render(ShadowMode::Auto,false))==0,"Direct-only debug contains indirect light.");
                scene.environment.debugMode=LightingDebugMode::Indirect;
                auto indirect=render(ShadowMode::Auto,false);
                if(pbrVariant == 8) Require(visible(indirect)==0,"AO texture did not occlude indirect lighting.");
                else Require(indirect==on,"Indirect-only debug does not match unlit environment scene.");
                for(uint32_t mode=3;mode<=10;++mode) { scene.environment.debugMode=static_cast<LightingDebugMode>(mode); render(ShadowMode::Auto,false); }
                scene.environment.debugMode=LightingDebugMode::Final;
                scene.environment.intensity=0;
                auto zeroIntensity=render(ShadowMode::Auto,false);
                if(pbrVariant == 8) Require(zeroIntensity==on,"IBL intensity attenuated emissive.");
                else Require(visible(zeroIntensity)==0,"Zero IBL intensity retained illumination.");
                scene.environment.intensity=1;
            }
            scene.environment.enabled=false;
            auto off=render(ShadowMode::Auto,false);
            if(!opticalDemo && pbrVariant != 8) Require(visible(off)==0,"Disabled IBL retained indirect lighting with zero fallback.");
            if(pbrVariant == 8) {
                Require(off==on,"AO/emissive results depend on environment enable.");
                size_t referencePixels=0;
                for(size_t i=0;i<on.size();i+=4) if(on[i+3]==255) {
                    Require(std::abs(int(on[i])-110)<=1 && std::abs(int(on[i+1])-102)<=1 && std::abs(int(on[i+2])-26)<=1,
                        "Emissive texture sRGB decode or factor multiplication incorrect."); ++referencePixels;
                }
                Require(referencePixels>100,"Emissive raster test had no geometry.");
            }
            scene.environment.enabled=true;
            ++scene.environment.revision;
            auto reloaded=render(ShadowMode::Auto,false); Require(reloaded==on,"Same HDR reload changed rendering.");
            const std::wstring prefix=opticalDemo?L"Generated/IBLSponza":pbrVariant==8?L"Generated/IBLMaterial":
                pbrVariant==3?L"Generated/IBLNormal":pbrVariant==2?L"Generated/IBLMetal":pbrVariant==1?L"Generated/IBLSmooth":L"Generated/IBLRaster";
            writeImage(prefix+L"Before.ppm",off); writeImage(prefix+L"After.ppm",on);
            Check(test.queue.WaitForIdle()); renderer.Shutdown(); test.shadows.ReleaseTexture(depth);
            Check(test.descriptors.RenderTargets().Free(*rtv)); transient.Shutdown(); test.Validate();
            std::cout << "IBL raster/mesh/optical integration passed; optical=" << opticalDemo << '\n';
            return on;
        }
        if(opticalDemo) {
            OpticalSettings settings; settings.enabled = false; Check(renderer.GetOptics().SetSettings(settings));
            const auto off = render(ShadowMode::Auto, false);
            settings.enabled = true; Check(renderer.GetOptics().SetSettings(settings));
            const auto on = render(ShadowMode::Auto, false);
            settings.maxBounces = 1; Check(renderer.GetOptics().SetSettings(settings));
            const auto one = render(ShadowMode::Auto, false);
            settings.maxBounces = 6; Check(renderer.GetOptics().SetSettings(settings));
            const auto originalGlass = renderer.GetOpticalMaterials()[glassSlot].optics;
            auto glass = originalGlass; glass.indexOfRefraction = 1;
            Check(renderer.SetOpticalMaterial(glassSlot, glass)); const auto straight = render(ShadowMode::Auto, false);
            glass = originalGlass; glass.absorption = {0,0,0};
            Check(renderer.SetOpticalMaterial(glassSlot, glass)); const auto clearGlass = render(ShadowMode::Auto, false);
            Check(renderer.SetOpticalMaterial(glassSlot, originalGlass));
            size_t reflectionChanged = 0, transmissionChanged = 0, glassDepthChanged = 0, coloredMirror = 0;
            size_t refractionChanged = 0, absorptionChanged = 0;
            for(uint32_t y = sponza ? 96U : 78U; y < (sponza ? 156U : 126U); ++y)
            for(uint32_t x = sponza ? 124U : 72U; x < (sponza ? 260U : 184U); ++x) {
                const size_t i = (static_cast<size_t>(y) * width + x) * 4;
                const int difference = std::abs(int(on[i])-int(off[i])) + std::abs(int(on[i+1])-int(off[i+1])) + std::abs(int(on[i+2])-int(off[i+2]));
                if(x < (sponza ? 184U : 120U)) {
                    if(difference > 40) ++reflectionChanged;
                    if(int(on[i]) > int(on[i+1]) * 2 + 20 || int(on[i+1]) > int(on[i]) * 2 + 20) ++coloredMirror;
                } else if(x > (sponza ? 200U : 136U)) {
                    if(difference > 40) ++transmissionChanged;
                    if(int(on[i]) + int(on[i+1]) + int(on[i+2]) > int(one[i]) + int(one[i+1]) + int(one[i+2]) + 60) ++glassDepthChanged;
                    if(std::abs(int(on[i])-int(straight[i])) + std::abs(int(on[i+1])-int(straight[i+1])) + std::abs(int(on[i+2])-int(straight[i+2])) > 40) ++refractionChanged;
                    if(int(clearGlass[i]) > int(on[i]) + 10) ++absorptionChanged;
                }
            }
            std::cout << "Optical readback: mirror=" << reflectionChanged << " glass=" << transmissionChanged << " depth=" << glassDepthChanged << " colored=" << coloredMirror << '\n';
            std::cout << "Glass physical response: IOR=" << refractionChanged << " Beer absorption=" << absorptionChanged << '\n';
            if(forceUnsupported) Require(on == off, "Unsupported optical capability changed raster fallback.");
            // PBR removes the old unit ambient term and shadows secondary surfaces;
            // depth/IOR/absorption changes consequently occupy fewer bright pixels.
            else Require(reflectionChanged > 100 && transmissionChanged > 100 && glassDepthChanged > 20 && (sponza || coloredMirror > 20),
                "Reflection/transmission spheres did not produce colored secondary-ray output.");
            if(!forceUnsupported) Require(refractionChanged > 20 && absorptionChanged > 20,
                "Glass IOR/absorption changes did not affect transmitted color.");
            if(fogTest) {
                scene.fog.enabled = true; scene.fog.startDistance = 0; scene.fog.endDistance = 0.1f;
                scene.fog.color = {0.2f, 0.4f, 0.6f};
                const auto foggedOptics = render(ShadowMode::Auto, false);
                for(size_t i = 0; i < on.size(); i += 4) if(on[i + 3] == 255) {
                    for(size_t channel = 0; channel < 3; ++channel)
                        Require(std::abs(int(foggedOptics[i + channel]) - int(std::lround(scene.fog.color[channel] * 255))) <= 1,
                            "Optical radiance bypassed distance fog.");
                }
                scene.fog.enabled = false;
                std::cout << "Distance fog applied to optical reflection/transmission output.\n";
            }
            // Standalone BMP artifact from final Forward output, with bottom-up BGRA rows.
            std::vector<uint8_t> bmp(54 + on.size(), 0);
            auto word = [&](size_t offset, uint32_t value) { for(size_t b=0;b<4;++b) bmp[offset+b]=static_cast<uint8_t>(value>>(b*8)); };
            bmp[0]='B'; bmp[1]='M'; word(2, static_cast<uint32_t>(bmp.size())); word(10,54); word(14,40); word(18,width); word(22,height); bmp[26]=1; bmp[28]=32;
            for(uint32_t y=0;y<height;++y) for(uint32_t x=0;x<width;++x) {
                const size_t src=(static_cast<size_t>(y)*width+x)*4, dst=54+(static_cast<size_t>(height-1-y)*width+x)*4;
                bmp[dst]=on[src+2]; bmp[dst+1]=on[src+1]; bmp[dst+2]=on[src]; bmp[dst+3]=255;
            }
            if(!forceUnsupported) { std::ofstream image(sponza ? "Generated/SponzaOpticalDemo.bmp" : "Generated/OpticalDemo.bmp", std::ios::binary); image.write(reinterpret_cast<const char*>(bmp.data()), static_cast<std::streamsize>(bmp.size())); Require(bool(image), "Failed to write optical demo image."); }
            renderer.Shutdown(); test.shadows.ReleaseTexture(depth); Check(test.descriptors.RenderTargets().Free(*rtv)); transient.Shutdown(); test.Validate();
            test.bindless.CollectGarbage(test.queue.GetCompletedFenceValue());
            auto allResources = test.descriptors.Resources().Allocate(sponza ? 4095U : 511U); Check(allResources); Check(test.descriptors.Resources().Free(*allResources));
            auto allSamplers = test.descriptors.Samplers().Allocate(sponza ? 127U : 15U); Check(allSamplers); Check(test.descriptors.Samplers().Free(*allSamplers));
            std::cout << "Optical Sphere Forward integration passed; forcedUnsupported=" << forceUnsupported << " Sponza=" << sponza << '\n'; return on;
        }
        const auto off = render(ShadowMode::Auto, false);
		if(fogTest) {
			scene.fog.enabled = true; scene.fog.color = {0.2f, 0.4f, 0.6f};
			scene.fog.startDistance = 0; scene.fog.endDistance = 0.1f;
			const auto fullFog = render(ShadowMode::Auto, false);
			size_t covered = 0;
			for(size_t i = 0; i < off.size(); i += 4) {
				Require(fullFog[i + 3] == off[i + 3], "Distance fog modified alpha/coverage.");
				if(off[i + 3] == 255) {
					++covered;
					for(size_t channel = 0; channel < 3; ++channel)
						Require(std::abs(int(fullFog[i + channel]) - int(std::lround(scene.fog.color[channel] * 255))) <= 1,
							"Far geometry did not reach linear HDR fog colour.");
				} else Require(fullFog[i] == off[i], "Fog modified the background.");
			}
			Require(covered > 100, "Fog test did not render geometry.");
			scene.fog.startDistance = 8; scene.fog.endDistance = 40;
			Require(render(ShadowMode::Auto, false) == off, "Fog modified geometry before start distance.");
			scene.fog.startDistance = 0; scene.fog.endDistance = 2;
			const auto mixedFog = render(ShadowMode::Auto, false);
			// Identity clip transform: this receiver pixel has world position
			// (2*(50.5/64)-1, 1-2*(32.5/64), .7). Test radial world distance.
			const float x = 2 * (50.5f / 64) - 1, y = 1 - 2 * (32.5f / 64);
			const float amount = std::sqrt(x * x + y * y + 0.7f * 0.7f) / 2;
			const size_t pixel = (32 * 64 + 50) * 4;
			for(size_t channel = 0; channel < 3; ++channel) {
				const float expected = static_cast<float>(off[pixel + channel]) * (1 - amount) + scene.fog.color[channel] * 255 * amount;
				Require(std::abs(int(mixedFog[pixel + channel]) - int(std::lround(expected))) <= 2,
					"Fog interpolation did not use Euclidean camera distance.");
			}
			scene.fog.endDistance = scene.fog.startDistance;
			Require(render(ShadowMode::Auto, false) == off, "Invalid fog range was not safely disabled.");
			scene.fog.endDistance = 2; scene.fog.enabled = false;
			Require(render(ShadowMode::Auto, false) == off, "Disabling fog did not restore lighting.");
			std::cout << "Distance fog start/end/interpolation/alpha/disable passed; mesh=" << (pbrVariant == 7) << '\n';
		}
		for(const auto mode : { ShadowMode::ShadowMap, ShadowMode::Auto, ShadowMode::RayTracing }) {
			if(pbrVariant) break;
			const auto on = render(mode, true);
			bool shadowed = false, lit = false;
			for(size_t i = 0; i < on.size(); i += 4) {
				if(off[i] > on[i] + 5) shadowed = true;
				if(off[i + 3] == 255 && std::abs(static_cast<int>(off[i]) - static_cast<int>(on[i])) <= 1 &&
					std::abs(static_cast<int>(off[i + 2]) - static_cast<int>(on[i + 2])) <= 1) lit = true;
			}
			if(!shadowed || !lit) {
				const auto [offMin, offMax] = std::minmax_element(off.begin(), off.end());
				const auto [onMin, onMax] = std::minmax_element(on.begin(), on.end());
				std::cerr << "Forward pixel diagnostic: off=" << static_cast<int>(*offMin) << ':' << static_cast<int>(*offMax)
					<< " on=" << static_cast<int>(*onMin) << ':' << static_cast<int>(*onMax)
					<< " shadowed=" << shadowed << " lit=" << lit << '\n';
				test.Validate();
			}
			Require(shadowed && lit, "Forward Lighting did not consume both shadow and lit visibility.");
			Require(renderer.GetShadows().GetStatistics().technique ==
				(mode == ShadowMode::ShadowMap || forceUnsupported ? ShadowTechnique::ShadowMap : ShadowTechnique::RayTracing),
				"Forward integration selected an incorrect technique.");
		}
		renderer.Shutdown(); test.shadows.ReleaseTexture(depth);
		Check(test.descriptors.RenderTargets().Free(*rtv)); transient.Shutdown();
		test.Validate();
		std::cout << "Forward Scene integration passed; forcedUnsupported=" << forceUnsupported << '\n';
		return off;
	}
	void OpticalResourceLifecycle() {
		Fixture test(false);
		if(!test.device.GetCapabilities().SupportsRayTracing()) return;
		// Two submeshes in one BLAS require distinct local-root material records.
		test.mesh.Shutdown(); const auto& cpu = test.asset.meshes[0];
		Check(test.factory.CreateMeshResource(test.mesh, cpu.vertexData, cpu.vertexStride, cpu.indexData,
			cpu.indexFormat, {{0,6,0,0},{6,6,0,1}}));
		OpticalSystem optics; Check(optics.Initialize(test.factory, L"Project/Shaders"));
		std::array<OpticalSurfaceMaterial, 2> materials;
		materials[0].optics.reflectivity = 1; materials[1].optics.transmission = 1;
		OpticalDrawItem draw; draw.geometry.mesh = &test.mesh;
		ShaderResourceRef previous;
		for(uint32_t frame = 0; frame < 4; ++frame) {
			Check(test.queue.WaitForIdle()); Native(test.allocator->Reset()); Native(test.commands->Reset(test.allocator.Get(), nullptr));
			test.bindless.Bind(test.commands.Get());
			auto context = GraphicsBackendTestAccess::Context(test.commands.Get(), test.bindless, frame % 2, frame == 0 ? 64U : 80U, 64);
			auto output = optics.Render(context, Matrix4x4::Identity(), {}, {&draw,1}, materials); Check(output);
			Require(test.bindless.IsValid(*output), "Optical output descriptor is invalid.");
			if(frame == 1) Require(!test.bindless.IsValid(previous), "Optical resize retained an old output descriptor.");
			previous = *output;
			Native(test.commands->Close()); test.queue.Execute(test.commands.Get()); GraphicsBackendTestAccess::Submitted(context);
		}
		Check(test.queue.WaitForIdle()); optics.Shutdown(); test.Validate();
		auto allResources = test.descriptors.Resources().Allocate(511); Check(allResources); Check(test.descriptors.Resources().Free(*allResources));
		std::cout << "Optical Resize/Frame slices/submesh tables/descriptor lifetime passed.\n";
	}
	void AccelerationFailureFallback() {
		Fixture test(false);
		if(!test.device.GetCapabilities().SupportsRayTracing()) return;
		// Signed base vertices are legal raster input but intentionally unsupported by
		// this BLAS builder. Exercise a real AS error while preserving visible geometry.
		Check(test.queue.WaitForIdle()); test.mesh.Shutdown(); test.CreateMesh(true);
		ShadowSettings settings; settings.mode = ShadowMode::RayTracing;
		settings.shadowMapResolution = 256; settings.shadowMapExtent = 2; settings.shadowMapBias = 0.005f;
		for(uint32_t frame = 0; frame < 2; ++frame) {
			const auto pixels = test.Frame(settings, frame);
			Require(test.shadows.GetStatistics().technique == ShadowTechnique::ShadowMap &&
				test.shadows.GetStatistics().fallback, "AS failure did not select fallback.");
			Require(*std::min_element(pixels.begin(), pixels.end()) < 0.5f &&
				*std::max_element(pixels.begin(), pixels.end()) > 0.9f, "AS failure fallback lost shadow/lit output.");
		}
		test.Validate(); std::cout << "Acceleration failure fallback passed (expected AS error logged).\n";
	}
	void InFlightGeometryLifetime() {
		Fixture test(false);
		if(!test.device.GetCapabilities().SupportsRayTracing()) return;
		RayTracingScene acceleration; Check(acceleration.Initialize(test.device.GetDevice(), 2));
		std::array<ComPtr<ID3D12CommandAllocator>, 2> allocators;
		std::array<ComPtr<ID3D12GraphicsCommandList4>, 2> lists;
		ShadowDrawItem instance; instance.mesh = &test.mesh;
		for(uint32_t frame = 0; frame < 2; ++frame) {
			Native(test.device.GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[frame])));
			Native(test.device.GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[frame].Get(), nullptr, IID_PPV_ARGS(&lists[frame])));
			instance.world = MakeTranslationMatrix({ static_cast<float>(frame) * 0.1f, 0, 0 });
			Check(acceleration.Update(lists[frame].Get(), frame, { &instance, 1 }));
			Native(lists[frame]->Close()); test.queue.Execute(lists[frame].Get());
		}
		// Asset ownership is removed without a CPU Fence wait. AS cache/Frame references
		// must retain the input Buffers and BLAS while both submitted slots can still execute.
		test.mesh.Shutdown();
		Check(test.queue.WaitForIdle());
		Native(allocators[0]->Reset()); Native(lists[0]->Reset(allocators[0].Get(), nullptr));
		Check(acceleration.Update(lists[0].Get(), 0, {}));
		Require(acceleration.GetTlasAddress(0) == 0 && acceleration.GetStatistics().blasCount == 0,
			"Empty scene retained active TLAS/cache entries.");
		Native(lists[0]->Close()); test.queue.Execute(lists[0].Get()); Check(test.queue.WaitForIdle());
		acceleration.Shutdown(); test.Validate();
		std::cout << "In-flight Geometry lifetime and instance removal passed.\n";
	}
}

#include "ColorManagementTests.inl"
#include "EnvironmentLightingTests.inl"

int main(int argc, char** argv) {
	Logger::Get().InitializeDefaultSinks();

	try {
		if(argc==2 && std::strcmp(argv[1],"--ddgi")==0) {
			DDGISettings invalid;invalid.spacing[0]=0;Require(!invalid.IsValid(),"Zero DDGI spacing accepted.");
			invalid={};invalid.hysteresis=1;Require(!invalid.IsValid(),"Non-convergent DDGI hysteresis accepted.");
			invalid={};invalid.raysPerProbe=100000;Require(!invalid.IsValid(),"DDGI ray budget ignored.");
			for(uint32_t variant:{1U,2U,3U,5U,6U}) ForwardIntegration(false,false,false,0,false,false,0,0,4,variant);
			ForwardIntegration(true,false,false,0,false,false,0,0,4,2);
			ForwardIntegration(false,true,false,0,false,false,0,0,4,4);
			return 0;
		}
		if(argc == 2 && std::strcmp(argv[1], "--baked-quality") == 0) {
			// 異なるアトラス解像度でも、同じ独立UVの生成規則を利用できることを検証する。
			ForwardIntegration(false,false,false,0,false,false,0,2,16);
			ForwardIntegration(false,false,false,0,false,false,0,3,16);
			return 0;
		}
		if(argc == 2 && std::strcmp(argv[1], "--baked") == 0) {
			BakedLightingCpuTests();
			for(uint32_t variant=1;variant<=3;++variant) ForwardIntegration(false,false,false,0,false,false,0,variant);
			ForwardIntegration(true,false,false,0,false,false,0,2);
			ForwardIntegration(false,true,false,0,false,false,0,4);
			return 0;
		}
		if(argc == 2 && std::strcmp(argv[1], "--gi") == 0) {
			GlobalIlluminationSettings invalid; invalid.samplesPerPixel=0; Require(!invalid.IsValid(),"Invalid GI sample count accepted.");
			invalid={}; invalid.maxBounces=5; Require(!invalid.IsValid(),"Invalid GI bounce count accepted.");
			invalid={}; invalid.intensity=std::numeric_limits<float>::infinity(); Require(!invalid.IsValid(),"Non-finite GI intensity accepted.");
			for(uint32_t variant=1;variant<=3;++variant) ForwardIntegration(false,false,false,0,false,false,variant);
			ForwardIntegration(false,false,false,0,false,false,5);
			ForwardIntegration(true,false,false,0,false,false,1);
			ForwardIntegration(false,true,false,0,false,false,4);
			return 0;
		}
		if(argc == 2 && std::strcmp(argv[1], "--ibl") == 0) {
			EnvironmentLightingIntegration();
			const auto diffuse = ForwardIntegration(false, false, false, 0, false, true);
			const auto smooth = ForwardIntegration(false, false, false, 1, false, true);
			const auto metal = ForwardIntegration(false, false, false, 2, false, true);
			const auto normal = ForwardIntegration(false, false, false, 3, false, true);
			Require(diffuse != smooth && smooth != metal && diffuse != normal, "IBL ignored roughness, metallicity or normal mapping.");
			ForwardIntegration(false, false, false, 8, false, true);
			ForwardIntegration(false, true, true, 0, false, true);
			return 0;
		}
		if(argc == 2 && std::strcmp(argv[1], "--fog") == 0) {
			FogSettings invalidFog; invalidFog.endDistance = invalidFog.startDistance;
			Require(!invalidFog.IsValid(), "Invalid fog range accepted.");
			invalidFog = {}; invalidFog.color[0] = std::numeric_limits<float>::infinity();
			Require(!invalidFog.IsValid(), "Non-finite fog colour accepted.");
			ForwardIntegration(false, false, false, 0, true);
			ForwardIntegration(false, false, false, 7, true);
			ForwardIntegration(false, true, false, 0, true);
			return 0;
		}
		if(argc == 2 && std::strcmp(argv[1], "--color") == 0) { ColorManagementIntegration(); return 0; }
		ColorManagementIntegration();
		if(argc == 2 && std::strcmp(argv[1], "--sponza") == 0) { ForwardIntegration(false, true, true); return 0; }
		ShadowSystem settingsApi;
		ShadowSettings invalid; invalid.sampleCount = 0;
		Require(!settingsApi.SetSettings(invalid), "Invalid sample count accepted.");
		invalid = {}; invalid.rayTMax = invalid.rayTMin;
		Require(!settingsApi.SetSettings(invalid), "Invalid ray interval accepted.");
		Run(false); Run(true); Run(true, true); const auto diffuse = ForwardIntegration(false); ForwardIntegration(true);
		const auto smooth = ForwardIntegration(false, false, false, 1);
		const auto metal = ForwardIntegration(false, false, false, 2);
		const auto normal = ForwardIntegration(false, false, false, 3);
		const auto flat = ForwardIntegration(false, false, false, 4);
		const auto mapped = ForwardIntegration(false, false, false, 5);
		const auto tangent = ForwardIntegration(false, false, false, 6);
		const auto meshShader = ForwardIntegration(false, false, false, 7);
		auto changedPixels = [](const auto& a, const auto& b, int tolerance) {
			size_t count = 0;
			for(size_t i = 0; i < a.size(); i += 4)
				if(std::abs(int(a[i]) - int(b[i])) > tolerance) ++count;
			return count;
		};
		std::cout << "PBR changed pixels: roughness=" << changedPixels(diffuse, smooth, 2)
			<< " metallic=" << changedPixels(smooth, metal, 2) << " normal=" << changedPixels(diffuse, normal, 3)
			<< " flat=" << changedPixels(diffuse, flat, 1) << " channels=" << changedPixels(diffuse, mapped, 0) << '\n';
		Require(changedPixels(diffuse, smooth, 2) > 100, "Roughness did not change GGX reflection.");
		Require(changedPixels(smooth, metal, 2) > 100, "Metallicity did not change diffuse/specular balance.");
		Require(changedPixels(diffuse, normal, 3) > 100, "Normal map did not change lighting.");
		Require(changedPixels(diffuse, flat, 1) == 0, "Flat normal map changed surface orientation.");
		Require(mapped == diffuse, "Metallic-roughness texture channels or linear decoding are incorrect.");
		Require(changedPixels(normal, tangent, 1) == 0, "Tangent and UV-derived normal bases disagree.");
		Require(changedPixels(tangent, meshShader, 1) == 0, "Mesh Shader and VS PBR output disagree.");
		std::cout << "PBR roughness, metallicity, normal maps and texture channels passed.\n";
		AccelerationFailureFallback(); InFlightGeometryLifetime(); ForwardIntegration(false, true); ForwardIntegration(true, true);
		OpticalResourceLifecycle(); ForwardIntegration(false, true, true); return 0;
	}
	catch(const std::exception& error) { std::cerr << "FAILED: " << error.what() << '\n'; return 1; }
}
