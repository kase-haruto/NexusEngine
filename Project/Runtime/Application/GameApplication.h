#pragma once
#include "Core/Framework/FrameworkUpdateClient.h"
#include "GameRuntime.h"
#include "ApplicationPaths.h"
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
    /** \brief 読込前に型集合を固定する。登録関数はApplicationより長く生存する */
    explicit GameApplication(ReflectionRegistration reflection = {}, ApplicationPaths paths = {})
        : reflection_(reflection), paths_(std::move(paths)) {}
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
    /** \brief 現在のProject情報。旧ディレクトリ起動ではnullptrを返す */
    const ProjectContext* Project() const noexcept { return paths_.project.get(); }

private:
    ReflectionRegistration reflection_; //< Level読込より先に決定するGameの型集合
    ApplicationPaths paths_; //< ホストで解決した資源の基準
    [[nodiscard]] Result<void> ResolveDrawRanges();
    GameRuntime runtime_; //< EditorのPlayと共通のゲーム更新基盤
    RenderSceneExtractor extractor_;
    RenderScene renderScene_; //< Rendererが参照する安定した値snapshot
    std::unique_ptr<ModelAssetLibrary> library_;
    std::shared_ptr<const ModelAssetData> asset_; //< poseとGPUアップロードより長く生存する
    ModelInstance pose_; //< 統合モデルのbind pose。個体別再生はAnimationPlayerComponentで指定する
};
}
