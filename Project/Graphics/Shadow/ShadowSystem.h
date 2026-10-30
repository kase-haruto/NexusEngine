#pragma once

#include <filesystem>
#include <memory>
#include <span>

#include "Foundation/Error/Result.h"
#include "Graphics/Device/GraphicsCapabilities.h"
#include "ShadowTypes.h"

namespace NexusEngine {
	class GraphicsContext;
	class GraphicsResourceFactory;

	/*-----------------------------------------------------------------------------------------
	 * ShadowSystem
	 * - Shadow設定、方式選択、共通Shadow Mask出力のBackend非依存窓口
	 * - GPU ResourceとDXRはPImplのBackendへ委譲する
	 * - Scene管理、Lighting Draw、Frame Fence待機、Editor表示は担当しない
	 *---------------------------------------------------------------------------------------*/
	class ShadowSystem final {
	public:
		ShadowSystem() noexcept;
		~ShadowSystem() noexcept;
		ShadowSystem(const ShadowSystem&) = delete;
		ShadowSystem& operator=(const ShadowSystem&) = delete;
		/** \brief 設定を検証して次Frameへ反映する。範囲外/NaNは変更せずエラーを返す */
		[[nodiscard]] Result<void> SetSettings(const ShadowSettings& settings);
		[[nodiscard]] const ShadowSettings& GetSettings() const noexcept { return settings_; }
		[[nodiscard]] GraphicsCapabilities GetCapabilities() const noexcept;
		[[nodiscard]] ShadowStatistics GetStatistics() const noexcept;
		/**
		 * \brief Camera surface depthと選択方式の影を描き、共通Maskの参照を返す
		 * \param context Fence待機済みの描画Context
		 * \param viewProjection Surface位置復元用Camera行列
		 * \param light API非依存Directional Light値
		 * \param draws Lightingと同じGeometry/Transformのsnapshot
		 * \return Shadow Mask参照。OFFでは無効参照（Lightingはvisibility=1）
		 */
		[[nodiscard]] Result<ShaderResourceRef> Render(GraphicsContext& context, const Matrix4x4& viewProjection,
			const DirectionalLightData& light, std::span<const ShadowDrawItem> draws);
		/** \brief GPU idleを保証してから呼ぶ。ResourceとDescriptorを解放する */
		void Shutdown() noexcept;
		/** GPUの完了後、既存の影パイプラインと設定を交換先へ引き継ぐ。 */
		void Swap(ShadowSystem& other) noexcept;
		/** GPUの完了後、変更のない形状のASを再利用し、未使用形状を解放する。 */
		void RetainGeometry(std::span<const uint64_t> generations);
	private:
		friend class GraphicsResourceFactory;
		[[nodiscard]] Result<void> Initialize(GraphicsResourceFactory& resources, const std::filesystem::path& shaders);
		class Impl;
		std::unique_ptr<Impl> impl_; //< Backend具体実装の唯一の所有者
		ShadowSettings settings_; //< Editorを参照しないRuntime設定
	};
}
