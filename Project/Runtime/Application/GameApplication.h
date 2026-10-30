#pragma once
#include "Core/Framework/FrameworkUpdateClient.h"
#include "GameRuntime.h"
#include "Graphics/Model/ModelAssetLibrary.h"
#include "Graphics/Model/ModelInstance.h"
#include "Runtime/Rendering/RenderSceneExtractor.h"

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * GameApplication
 * - 単体起動時のLevelロード、GameRuntime更新、描画入力の抽出を担当する
 * - Window/GPU資源/Editorを所有しない。CPUアセットはRenderer終了まで保持する
 *---------------------------------------------------------------------------------------*/
class GameApplication final : public IFrameworkUpdateClient {
public:
    [[nodiscard]] bool SetGameLevelSetup(GameLevelSetup setup) { return runtime_.SetLevelSetup(std::move(setup)); }
    /** \brief GPU初期化前に使用Assetを準備する。旧:startup参照だけ任意のGame側Loaderで解決する */
    [[nodiscard]] Result<void> Load(const std::filesystem::path& levelPath, const std::filesystem::path& assetRoot,
                                   ModelAssetLibrary::ReloadStartup loadStartup = {});
    [[nodiscard]] Result<void> Update(float deltaTime) override;
    [[nodiscard]] Result<void> PrepareRender(uint32_t width, uint32_t height) override;
    [[nodiscard]] const RenderScene& GetRenderScene() const noexcept { return renderScene_; }
    [[nodiscard]] const ModelAssetData& GetModelAsset() const noexcept { return *asset_; }
    [[nodiscard]] const ModelInstance& GetModelPose() const noexcept { return pose_; }
    GameRuntime& Runtime() noexcept { return runtime_; }

private:
    [[nodiscard]] Result<void> ResolveDrawRanges();
    GameRuntime runtime_; //< EditorのPlayと共通のゲーム更新基盤
    RenderSceneExtractor extractor_;
    RenderScene renderScene_; //< Rendererが参照する安定した値snapshot
    std::unique_ptr<ModelAssetLibrary> library_;
    std::shared_ptr<const ModelAssetData> asset_; //< poseとGPUアップロードより長く生存する
    ModelInstance pose_; //< 統合モデルのCPU姿勢。個体別Animationは今後の拡張
};
}
