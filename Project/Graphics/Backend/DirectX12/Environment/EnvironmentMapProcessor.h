#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include "Graphics/Environment/EnvironmentLightingData.h"
#include "Graphics/Descriptor/ShaderResourceRef.h"
#include "Graphics/Resource/ImmediateUploadContext.h"
namespace NexusEngine {
    class CommandQueue;
    class BindlessDescriptorTable;
    struct HdrImage;
    /*-----------------------------------------------------------------------------------------
     * EnvironmentMapProcessor
     * - HDR pixelsをcomputeでcube/irradiance/GGX prefilterへ変換しGPU生成物を所有する
     * - ファイルdecode、Scene、描画passの記録は担当しない
     *---------------------------------------------------------------------------------------*/
    class EnvironmentMapProcessor final {
    public:
        /** \brief GPU依存先を接続する。PSO/LUTは最初のBake時に一度生成する。 */
        void Initialize(void*, CommandQueue*, BindlessDescriptorTable*, const std::filesystem::path&);
        /** \brief 同期Bake後に旧環境を置換する。提出失敗の資源はShutdownまで保持する。 */
        [[nodiscard]] Result<void> Bake(const HdrImage&);
        /** \brief GPU idle後に環境、LUT、作業資源、Descriptorを解放する。 */
        void Shutdown() noexcept;
        [[nodiscard]] EnvironmentDrawData GetDrawData() const noexcept;
    private:
        using Resource = Microsoft::WRL::ComPtr<ID3D12Resource>;
        struct Map {
            std::array<Resource, 3> textures;
            std::array<ShaderResourceRef, 3> views {};
            std::vector<ShaderResourceRef> descriptors; //< transient viewsもFence確認まで保持
        };
        /** \brief stageに依存しないcompute rootと5個のPSOを作る。 */
        [[nodiscard]] Result<void> CreatePipeline();
        void Release(Map&) noexcept;
        ID3D12Device* device_ = nullptr; //< GraphicsSystemの非所有参照
        CommandQueue* queue_ = nullptr; //< Bake専用listを既存Direct Queueへ提出する
        BindlessDescriptorTable* table_ = nullptr; //< 既存共有heap。全viewのretire先
        std::filesystem::path shaders_; //< runtime shaderルート
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root_; //< Bakeの共通root
        std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 5> pipelines_; //< 初回だけcompile
        Map active_, pending_; //< 描画中の世代と完了確認前の次世代
        Resource lut_, source_, upload_; //< 共通LUTとBake中だけ保持する入力/staging
        ShaderResourceRef lutView_, lutUav_, sampler_; //< reloadで再生成しない参照
        ImmediateUploadContext uploadContext_; //< 提出失敗でもAllocator/Listを保持
        bool ready_ = false, poisoned_ = false; //< LUT生成済み / 提出完了未確認のため再Bake禁止
    };
}
