#pragma once
#include "BakedLightingData.h"
#include <memory>
#include <string>
namespace NexusEngine {
	class GraphicsResourceFactory;
	class ModelInstance;
	struct ModelAssetData;
	struct RenderScene;
	/*-----------------------------------------------------------------------------------------
	 * BakedLighting
	 * - 保存済みGIの読み込みとGPUテクスチャ・Descriptorの寿命を管理する
	 * - ベイク計算、Scene管理、Editor UIは担当しない
	 *---------------------------------------------------------------------------------------*/
	class BakedLighting final {
	public:
		BakedLighting() noexcept;
		~BakedLighting() noexcept;
		/** \brief 共有Factoryへ接続する。ShutdownまでFactoryが生存すること。 */
		void Initialize(GraphicsResourceFactory&);
		/** \brief パスまたはrevision変更時だけ読み込み・照合・同期転送する。失敗時は旧成果物を保持する。 */
		[[nodiscard]] Result<void> Prepare(const ModelAssetData&, const ModelInstance&, const RenderScene&);
		/** \brief 共通プローブの描画定数を返す。未ロード・無効時はゼロ。 */
		[[nodiscard]] BakedLightingDrawData GetDrawData(const BakedLightingSettings&) const noexcept;
		/** \brief 静的インスタンスがベイク時の位置と一致する場合だけライトマップを選択する。 */
		[[nodiscard]] BakedLightingDrawData GetDrawData(const BakedLightingSettings&, uint64_t object, uint32_t node, uint32_t mesh, const Matrix4x4&, bool staticLighting) const noexcept;
		/** \brief Editor用の状態文字列。GPU参照を公開しない。 */
		[[nodiscard]] const std::string& GetStatus() const noexcept;
		/** \brief GraphicsSystemのGPU idle後に資源とDescriptorを解放する。 */
		void Shutdown() noexcept;

	private:
		class Impl;
		std::unique_ptr<Impl> impl_; //< リソースと照合情報の唯一の所有者
	};
} // namespace NexusEngine
