#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <unordered_map>
#include "Graphics/RayTracing/OpticalSystem.h"
#include "Graphics/Backend/DirectX12/RayTracing/RayTracingScene.h"
#include "Graphics/Resource/ConstantBuffer.h"

namespace NexusEngine {
	class CommandQueue;
	class BindlessDescriptorTable;
	class GraphicsResourceFactory;
	/*-----------------------------------------------------------------------------------------
	 * OpticalBackend
	 * - 反射/透過のDXR Pipeline、hit属性view、Frame metadata、Radianceを所有する
	 * - 共通RayTracingSceneを利用しScene/Editor管理とForward合成は担当しない
	 *---------------------------------------------------------------------------------------*/
	class OpticalBackend final {
	public:
		[[nodiscard]] Result<void> Initialize(void*, CommandQueue*, BindlessDescriptorTable*, GraphicsResourceFactory*,
			uint32_t frames, bool supported, const std::filesystem::path&);
		[[nodiscard]] Result<ShaderResourceRef> Render(GraphicsContext&, void*, const Matrix4x4&,
			const DirectionalLightData&, std::span<const OpticalDrawItem>, std::span<const OpticalSurfaceMaterial>, const OpticalSettings&);
		/** \brief 未投入記録のAS/Texture状態を復帰する */
		static void CompleteRecording(void*, bool, uint32_t) noexcept;
		void Shutdown() noexcept;
	private:
		struct GeometryViews {
			Microsoft::WRL::ComPtr<ID3D12Resource> vertices, indices; //< Asset解放後もGPU入力を保持
			ShaderResourceRef vertexView, indexView;
		};
		struct InstanceData { Matrix4x4 normalTransform; std::array<uint32_t, 4> geometryBase {}; };
		struct GeometryData {
			std::array<uint32_t, 8> indices {}; //< VB/IB/stride/wideIndex/firstIndex/baseVertex/texture/sampler
			std::array<float, 4> color {};
			std::array<float, 4> optical {}; //< reflection/transmission/IOR/padding
			std::array<float, 4> absorption {};
		};
		struct Constants {
			Matrix4x4 inverseViewProjection;
			std::array<float, 4> lightDirection {}, lightColor {};
			std::array<uint32_t, 4> indices {}; //< UAV/geometryByteOffset/bounces/padding
			std::array<float, 4> rays {}; //< Bias/TMax/padding
		};
		static_assert(sizeof(InstanceData) == 80 && sizeof(GeometryData) == 80 && sizeof(Constants) == 128);
		[[nodiscard]] Result<void> CreatePipeline();
		[[nodiscard]] Result<void> Resize(uint32_t, uint32_t);
		[[nodiscard]] Result<GeometryViews*> GetViews(const MeshResource&);
		void ReleaseViews(GeometryViews&) noexcept;
		void ReleaseOutput() noexcept;
		Microsoft::WRL::ComPtr<ID3D12Device5> device_;
		CommandQueue* queue_ = nullptr; //< GraphicsSystem所有の同期先
		BindlessDescriptorTable* bindless_ = nullptr; //< 既存共有Heap、非所有
		RayTracingScene scene_;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
		Microsoft::WRL::ComPtr<ID3D12StateObject> pipeline_;
		Microsoft::WRL::ComPtr<ID3D12Resource> shaderTable_, output_;
		ShaderResourceRef outputSrv_, outputUav_;
		ConstantBuffer constants_;
		std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> metadata_; //< Fence待機済みFrameだけ書込
		std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> hitTables_; //< Geometry別local-root records、Frame独立
		std::array<uint8_t, 32> hitIdentifier_ {}; //< StateObject寿命中不変
		std::vector<uint32_t> hitGroupOffsets_; //< InstanceのShaderTable開始位置
		std::vector<std::vector<uint64_t>> activeGenerations_; //< in-flight Frameが読む属性Descriptorを保持
		std::unordered_map<uint64_t, GeometryViews> views_; //< Geometry世代の永続view
		std::vector<InstanceData> instances_;
		std::vector<GeometryData> geometries_;
		std::vector<ShadowDrawItem> accelerationDraws_;
		std::filesystem::path shaders_;
		D3D12_RESOURCE_STATES state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		D3D12_RESOURCE_STATES initialState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		bool supported_ = false, recording_ = false, warned_ = false, pipelineReady_ = false;
	};
}
