#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <unordered_map>
#include "Graphics/RayTracing/OpticalSystem.h"
#include "Graphics/Backend/DirectX12/RayTracing/RayTracingScene.h"
#include "Graphics/Resource/ConstantBuffer.h"
#include "Graphics/Backend/DirectX12/Lighting/DDGIProcessor.h"

namespace NexusEngine {
	class CommandQueue;
	class BindlessDescriptorTable;
	class GraphicsResourceFactory;
	/*-----------------------------------------------------------------------------------------
	 * OpticalBackend
	 * - 反射・透過・拡散GIのDXR Pipeline、交差属性view、フレーム情報、放射出力を所有する
	 * - 共通RayTracingSceneを利用しScene/Editor管理とForward合成は担当しない
	 *---------------------------------------------------------------------------------------*/
	class OpticalBackend final {
	public:
		[[nodiscard]] Result<void> Initialize(void*, CommandQueue*, BindlessDescriptorTable*, GraphicsResourceFactory*,
			uint32_t frames, bool supported, const std::filesystem::path&);
		[[nodiscard]] Result<ShaderResourceRef> Render(GraphicsContext&, void*, const Matrix4x4&,
			const DirectionalLightData&, std::span<const OpticalDrawItem>, std::span<const OpticalSurfaceMaterial>, const OpticalSettings&,
			const std::array<float, 3>& ambientIrradiance, const EnvironmentDrawData& environment, const GlobalIlluminationSettings& gi, const BakedLightingDrawData& baked, const DDGISettings& ddgi);
		/** \brief 未投入記録のAS/Texture状態を復帰する */
		static void CompleteRecording(void*, bool, uint32_t) noexcept;
		void Shutdown() noexcept;
		[[nodiscard]] ShaderResourceRef GetDiffuseGlobalIllumination() const noexcept { return giSrv_; }
		[[nodiscard]] DDGIDrawData GetDDGIDrawData(float intensity) const noexcept {return ddgi_.GetDrawData(intensity);}
		[[nodiscard]] uint32_t GetDDGIFrameCount() const noexcept {return ddgi_.FrameCount();}
		[[nodiscard]] OpticalGpuTimings GetGpuTimings() const noexcept {return timings_;}
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
			std::array<float, 4> pbr {};
			std::array<uint32_t, 4> maps {};
			std::array<float, 4> emissiveOcclusion {};
			std::array<uint32_t, 4> extraMaps {};
		};
		struct Constants {
			Matrix4x4 inverseViewProjection;
			std::array<float, 4> lightDirection {}, lightColor {};
			std::array<uint32_t, 4> indices {}; //< 光学出力UAV・形状情報の位置・光学交差数・光学描画の有効状態
			std::array<float, 4> rays {}; //< Bias/TMax/padding
			std::array<float, 4> ambient {};
			EnvironmentDrawData environment;
			std::array<uint32_t, 4> giOptions {}; //< 有効・経路数・最大区間数・出力UAV
			std::array<float, 4> giParameters {}; //< 間接拡散の倍率と境界調整領域
			BakedLightingDrawData bakedLighting;
			DDGIDrawData ddgi;
			std::array<uint32_t,4> probeTrace {}; //< レイUAV・方向位相・フィードバック有効・予約
			std::array<uint32_t,4> reflectionTargets {}; //< Specular UAV, guide UAV, reserved
		};
		static_assert(sizeof(InstanceData) == 80 && sizeof(GeometryData) == 144 && sizeof(Constants) == 416);
		[[nodiscard]] Result<void> CreatePipeline();
		[[nodiscard]] Result<void> CreateReflectionFilter();
		[[nodiscard]] Result<void> InitializeTimings(uint32_t frames);
		void BeginTimings(ID3D12GraphicsCommandList*,uint32_t frame);
		void ResolveTimings(ID3D12GraphicsCommandList*,uint32_t frame);
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
		Microsoft::WRL::ComPtr<ID3D12Resource> giOutput_; //< 共通DXRパスが書き込む間接拡散の専用出力
		ShaderResourceRef giSrv_, giUav_; //< 光学出力と同じ画面寸法・Fence寿命
		Microsoft::WRL::ComPtr<ID3D12Resource> reflection_, reflectionGuide_;
		ShaderResourceRef reflectionSrv_, reflectionUav_, guideSrv_, guideUav_;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> filterRoot_;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> filterPipeline_;
		ConstantBuffer constants_;
		ConstantBuffer probeConstants_; //< 同じFrame内の追跡と光学描画でCBを上書きしない
		DDGIProcessor ddgi_; //< ASとは独立したGPUプローブ履歴の所有者
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
		Microsoft::WRL::ComPtr<ID3D12QueryHeap> timingQueries_;
		Microsoft::WRL::ComPtr<ID3D12Resource> timingReadback_;
		std::vector<uint8_t> timingReady_,timingRecorded_;
		uint64_t timestampFrequency_=0;
		OpticalGpuTimings timings_;
	};
}
