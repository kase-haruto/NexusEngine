#pragma once
#include <filesystem>
#include <memory>
#include <span>
#include "Graphics/Lighting/GlobalIlluminationSettings.h"
#include "Graphics/Lighting/BakedLightingData.h"
#include "Graphics/Lighting/DDGISettings.h"
#include "Graphics/Environment/EnvironmentLightingData.h"
#include "Graphics/Shadow/ShadowTypes.h"
#include "Graphics/Material/OpticalMaterial.h"
#include "Graphics/Descriptor/ShaderResourceRef.h"
#include "Foundation/Error/Result.h"

namespace NexusEngine {
	class GraphicsContext;
	class GraphicsResourceFactory;
	struct OpticalSurfaceMaterial {
		std::array<float, 4> color {1, 1, 1, 1};
		OpticalMaterial optics;
		ShaderResourceRef texture, sampler; //< Renderer所有のMaterial資源への非所有参照
		std::array<float, 4> pbr {0, 1, 1, 0};
		ShaderResourceRef normalTexture, normalSampler, metallicRoughnessTexture, metallicRoughnessSampler;
		std::array<float, 4> emissiveOcclusion {0, 0, 0, 1};
		ShaderResourceRef emissiveTexture, emissiveSampler, occlusionTexture, occlusionSampler;
	};
	struct OpticalDrawItem {
		ShadowDrawItem geometry;
		std::array<float, 4> tint {1, 1, 1, 1};
	};
	struct OpticalSettings {
		bool enabled = true;
		uint32_t maxBounces = 6; //< 入射/出射/内部反射を含む最大交差回数、1〜12
		float rayBias = 0.001f;
		float rayDistance = 1000;
	};
	/*-----------------------------------------------------------------------------------------
	 * OpticalSystem
	 * - 反射・屈折と拡散GIが共有するDXRパスの設定・GPU資源を所有するAPI非依存境界
	 * - Material/Geometry snapshotを利用し、Scene管理とEditor UIは担当しない
	 *---------------------------------------------------------------------------------------*/
	class OpticalSystem final {
	public:
		OpticalSystem() noexcept;
		~OpticalSystem() noexcept;
		/** \brief Device/共有Descriptor/Frame同期へ接続する。非DXR環境では空結果を返す */
		[[nodiscard]] Result<void> Initialize(GraphicsResourceFactory&, const std::filesystem::path& shaders);
		/** \brief 有限値/範囲を検証して品質設定を変更する */
		[[nodiscard]] Result<void> SetSettings(const OpticalSettings&);
		[[nodiscard]] const OpticalSettings& GetSettings() const noexcept { return settings_; }
		/** \brief Camera rayから光学Materialの最終色を評価。alpha=1のpixelだけForwardへ合成する */
		[[nodiscard]] Result<ShaderResourceRef> Render(GraphicsContext&, const Matrix4x4& viewProjection,
			const DirectionalLightData&, std::span<const OpticalDrawItem>, std::span<const OpticalSurfaceMaterial>,
			const std::array<float, 3>& ambientIrradiance = {0.03f, 0.03f, 0.03f}, const EnvironmentDrawData& environment = {}, const GlobalIlluminationSettings& gi = {}, const BakedLightingDrawData& baked = {}, const DDGISettings& ddgi = {});
		/** \brief GPU idleを保証してすべての所有資源を解放する */
		void Shutdown() noexcept;
		/** \brief 最新Renderで生成した間接拡散出力。無効/非対応/失敗時はnull参照。 */
		[[nodiscard]] ShaderResourceRef GetDiffuseGlobalIllumination() const noexcept { return diffuseGI_; }
		/** \brief Renderer内でのみ使う最新DDGI参照。無効・非対応・失敗時はゼロ。 */
		[[nodiscard]] DDGIDrawData GetDDGIDrawData() const noexcept;
		/** \brief Editor表示用の有効状態。Descriptorを渡さずに取得する。 */
		[[nodiscard]] bool IsDDGIActive() const noexcept { return GetDDGIDrawData().spacingEnabled[3]!=0; }
		/** \brief 任意パスを省略するFrameの非所有出力を無効化する。GPUのプローブ履歴は維持する。 */
		void ClearFrameOutputs() noexcept { diffuseGI_={}; ddgi_={}; }
		/** \brief EditorへGPU参照を公開せず、提出済みのプローブ更新回数を取得する。 */
		[[nodiscard]] uint32_t GetDDGIFrameCount() const noexcept;
	private:
		class Impl;
		std::unique_ptr<Impl> impl_;
		OpticalSettings settings_;
		ShaderResourceRef diffuseGI_; //< Frameの記録が利用するBackend所有viewへの非所有参照
		DDGIDrawData ddgi_; //< DDGIProcessor所有履歴への描画用の非所有参照
	};
}
