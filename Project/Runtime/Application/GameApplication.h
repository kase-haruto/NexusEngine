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
    /** \brief DLLの所有権をRuntimeへ集約する。Applicationは選択情報だけを転送する */
    [[nodiscard]] bool SetNativeGameModule(NativeGameModuleSettings settings) { return runtime_.SetNativeGameModule(std::move(settings)); }
    /** \brief 同じRuntimeへ依存順の複数DLLを接続する */
    [[nodiscard]] bool SetNativeGameModules(std::vector<NativeGameModuleSettings> settings) { return runtime_.SetNativeGameModules(std::move(settings)); }
    /** \brief Managed sessionの所有権を共通Runtimeへ集約する */
    [[nodiscard]] bool SetCSharpRuntime(CSharpRuntimeSettings settings) { return runtime_.SetCSharpRuntime(std::move(settings)); }
    /** \brief GPU初期化前に使用Assetを準備する。旧:startup参照だけ任意のGame側Loaderで解決する */
    [[nodiscard]] Result<void> Load(const std::filesystem::path& levelPath, const std::filesystem::path& assetRoot,
                                   ModelAssetLibrary::ReloadStartup loadStartup = {});
    [[nodiscard]] Result<void> Update(float deltaTime) override;
    [[nodiscard]] Result<void> PrepareRender(uint32_t width, uint32_t height) override;
    [[nodiscard]] const RenderScene& GetRenderScene() const noexcept { return renderScene_; }
    [[nodiscard]] const ModelAssetData& GetModelAsset() const noexcept { return *asset_; }
    [[nodiscard]] const ModelInstance& GetModelPose() const noexcept { return pose_; }
    GameRuntime& Runtime() noexcept { return runtime_; }
    void SetFrameInput(const InputFrame& input) override { runtime_.SetFrameInput(input); }
    /** \brief 既存GPU準備・commit機構を借用し、Runtimeの動的model/texture要求を処理する */
    Result<void> AttachGraphics(GraphicsSystem& graphics,IGraphicsRenderer* renderer) override;
    void DetachGraphics() noexcept override { graphics_=nullptr; renderer_=nullptr; }
    /** Hostだけが既存GPU準備/commitを接続する。CPU RuntimeやGame Public APIへRenderer実装を依存させない。 */
    using AssetPublisher=std::function<Result<void>(GraphicsSystem&,IGraphicsRenderer&,const std::shared_ptr<const ModelAssetData>&,
        const ModelInstance&,const std::function<void()>&)>;
    void SetAssetPublisher(AssetPublisher publisher) { assetPublisher_=std::move(publisher); }
    /** \brief 現在のProject情報。旧ディレクトリ起動ではnullptrを返す */
    const ProjectContext* Project() const noexcept { return paths_.project.get(); }

private:
    ReflectionRegistration reflection_; //< Level読込より先に決定するGameの型集合
    ApplicationPaths paths_; //< ホストで解決した資源の基準
    [[nodiscard]] Result<void> ResolveDrawRanges();
    /** \brief 候補CPU/GPU資源を完成させてからframe境界で交換し、旧資源の寿命を守る */
    [[nodiscard]] Result<void> PrepareRequestedAssets();
    GraphicsSystem* graphics_=nullptr; //< Framework所有。Attach/Detachの間だけ借用する
    IGraphicsRenderer* renderer_=nullptr; //< Host所有。CPU Runtimeは具体Rendererの実装へリンクしない
    AssetPublisher assetPublisher_; //< Host所有GPU機構への同期callback。Game Moduleには公開しない
    GameRuntime runtime_; //< EditorのPlayと共通のゲーム更新基盤
    RenderSceneExtractor extractor_;
    RenderScene renderScene_; //< Rendererが参照する安定した値snapshot
    std::unique_ptr<ModelAssetLibrary> library_;
    std::shared_ptr<const ModelAssetData> asset_; //< poseとGPUアップロードより長く生存する
    ModelInstance pose_; //< 統合モデルのbind pose。個体別再生はAnimationPlayerComponentで指定する
};
}
