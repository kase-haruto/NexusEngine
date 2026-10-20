#pragma once

#include <cstdint>

namespace NexusEngine {

	/** API非依存の診断値。D3D12 enumの数値を外部へ転送しない。 */
	enum class RayTracingSupport : uint8_t { Unsupported, Tier1_0, Tier1_1 };
	enum class MeshShaderSupport : uint8_t { Unsupported, Tier1 };

	/*-----------------------------------------------------------------------------------------
	 * GraphicsCapabilities
	 * - 起動時にBackendが取得したGPU機能の値snapshotを保持する
	 * - Feature Support問い合わせ、Device所有、Shadow方式選択は担当しない
	 * - 将来の機能は実装時に独立したfieldを追加する。未照会の機能を対応済みと扱わない
	 *---------------------------------------------------------------------------------------*/
	struct GraphicsCapabilities {
		MeshShaderSupport meshShader = MeshShaderSupport::Unsupported; //< API 非依存 Mesh Shader Tier
		/** \brief AS/MS を利用できるか返す。非対応は従来経路を使用する */
		[[nodiscard]] constexpr bool SupportsMeshShaders() const noexcept {
			return meshShader != MeshShaderSupport::Unsupported;
		}
		RayTracingSupport rayTracing = RayTracingSupport::Unsupported; //< DXR診断用対応レベル

		/** \brief Shader実行方式に依存しないRay Tracing対応状態を返す */
		[[nodiscard]] constexpr bool SupportsRayTracing() const noexcept {
			return rayTracing != RayTracingSupport::Unsupported;
		}
		/** \brief Inline RayQueryが使用可能か返す。DispatchRaysとは必要機能が異なる */
		[[nodiscard]] constexpr bool SupportsInlineRayTracing() const noexcept {
			return rayTracing == RayTracingSupport::Tier1_1;
		}
	};
}
