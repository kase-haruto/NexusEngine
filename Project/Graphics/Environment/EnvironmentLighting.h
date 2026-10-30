#pragma once
#include <memory>
#include "EnvironmentLightingData.h"
#include "Foundation/Error/Result.h"
namespace NexusEngine {
    class GraphicsResourceFactory;
    /*-----------------------------------------------------------------------------------------
     * EnvironmentLighting
     * - 環境設定を解決し、IBL textureとDescriptorの所有権をBackendに委譲する
     * - Scene管理、材質評価、Editor UIは担当しない
     *---------------------------------------------------------------------------------------*/
    class EnvironmentLighting final {
    public:
        EnvironmentLighting() noexcept;
        ~EnvironmentLighting() noexcept;
        /** \brief Factoryの非所有Device/Queue/Tableを接続する。まだ画像はロードしない。 */
        [[nodiscard]] Result<void> Initialize(GraphicsResourceFactory&, const std::filesystem::path& shaders);
        /** \brief path/revision変更時だけ同期Bakeする。Renderの最初、環境利用記録より前に呼ぶ。 */
        [[nodiscard]] Result<void> Prepare(const EnvironmentLightingData&);
        /** \brief 最後に成功した環境のShader転送値を返す。設定異常時は無効化する。 */
        [[nodiscard]] EnvironmentDrawData GetDrawData(const EnvironmentLightingData&) const noexcept;
        /** \brief GraphicsSystemがGPU idleを保証した後に解放する。Resizeの影響は受けない。 */
        void Shutdown() noexcept;
        /** GPUの完了後、メッシュ交換先へ環境マップとベイク状態の所有権を引き継ぐ。 */
        void Swap(EnvironmentLighting& other) noexcept;
    private:
        class Impl;
        std::unique_ptr<Impl> impl_; //< 全GPU所有物をDX12境界に閉じ込める
    };
}
