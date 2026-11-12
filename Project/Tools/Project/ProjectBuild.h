#pragma once
#include "Runtime/Project/ProjectContext.h"
#include "Tools/Engine/EngineRegistry.h"
#include "Platform/Process/Process.h"
#include <stop_token>

namespace NexusEngine {
enum class ProjectHostKind { Editor, Runtime };
enum class ProjectBuildProfile { Standard, NativeLivePatch };
/*-----------------------------------------------------------------------------------------
 * ProjectBuildOptions
 * - 共通Build経路へ渡す開発profile。言語やProjectの恒久設定ではない
 * - SDK配置はマシン固有の値で、Project定義・build stampへ保存しない
 *---------------------------------------------------------------------------------------*/
struct ProjectBuildOptions {
    ProjectBuildProfile profile = ProjectBuildProfile::Standard; //< 通常配布と開発パッチを区別する
    std::filesystem::path livePPDirectory; //< 専用profileでだけ使用する明示的な絶対配置
    bool rebuild = false; //< ユーザーが明示した場合だけRebuildを要求する
    bool rebuildManaged = false; //< Managed再構築だけを要求し、Nativeの差分Buildと共通stamp検証は維持する
};
/*-----------------------------------------------------------------------------------------
 * ProjectBuildTargets
 * - 共通ProjectのBuild入力・配布先を値で表現し、Module本体は所有しない
 *---------------------------------------------------------------------------------------*/
struct ProjectBuildTargets {
    std::vector<std::filesystem::path> runtimeDependencies; //< ゲーム側が明示した追加配布DLL。Engine依存とは別にhashを記録する
    /** 追加ModuleのID・言語・入力Project・出力だけを保持する。ロードと寿命はRuntimeが担当する。 */
    struct Module { std::string id; ProjectModuleLanguage language; std::filesystem::path project, output; };
    std::vector<Module> modules; //< 先頭Module以外の明示的なBuild / 配置対応
    std::filesystem::path editorProject, runtimeProject, editorExecutable, runtimeExecutable;
    std::filesystem::path nativeModule; //< 空なら従来の静的ホスト。存在する場合はstampとDLL内容も照合する
    std::filesystem::path managedBootstrap; //< Bootstrap、runtimeconfig、deps、nethostの配置照合基準
    std::filesystem::path gameScripts; //< 任意のGameScripts.dll。空ならBootstrapのみの互換構成
    std::string managedRuntime = "CoreCLR"; //< 旧BuildTargetsはCoreCLR互換。新規Mono Projectは明示して保存する
};
struct ProjectLaunchOptions {
    bool checkOnly = false; //< GPUを使わない既存CPU検証
    bool hidden = false; //< ウィンドウとコンソールを表示しない
    uint32_t frames = 0; //< 0は通常実行。有限frameは明示的な検証用
    std::filesystem::path log; //< 任意の新規絶対パス。既存ログは置換しない
    ProjectBuildOptions build{}; //< Build / Verify / Launchが同じ成果物を選択する
};
/*-----------------------------------------------------------------------------------------
 * ProjectBuild
 * - 選択EngineでProject自身のホストをビルドし、成果物の結び付きを検証する
 * - Engineを勝手に選択せず、EditorやGraphicsの内部を操作しない
 *---------------------------------------------------------------------------------------*/
class ProjectBuild final {
public:
    /** \brief 共通Module構成とprofileを検証し、既存BuildTargetsから絶対配置を計画する。ファイルを書き換えない */
    [[nodiscard]] static Result<ProjectBuildTargets> Targets(const ProjectContext& project, const std::string& configuration,
        ProjectBuildProfile profile = ProjectBuildProfile::Standard);
    /** \brief 明示要求されたBuild / Rebuildだけを実行し、同じprofileのstampを検証する
     * \param options SDKの絶対配置とprofile。Project / Worldの所有権は移動しない
     * \return 検証済みホスト配置。失敗時に別profileへfallbackしない
     */
    [[nodiscard]] static Result<std::filesystem::path> Build(const ProjectContext& project, const EngineInstallation& engine,
        ProjectHostKind kind, const std::filesystem::path& log, std::stop_token stop = {}, const ProjectBuildOptions& options = {});
    /** \brief 要求したprofile・配置・identity・hashを照合し、開発用成果物の通常版への取り違えを拒否する */
    [[nodiscard]] static Result<void> Verify(const ProjectContext& project, const EngineInstallation& engine, const std::filesystem::path& executable,
                                            ProjectHostKind kind = ProjectHostKind::Runtime, ProjectBuildProfile profile = ProjectBuildProfile::Standard);
    /** \brief 実行中Editorと同じprofileでEngineの結び付きを検証する。選択したEngineを変更しない */
    [[nodiscard]] static Result<EngineInstallation> Binding(const ProjectContext& project, const std::filesystem::path& executable,
        const std::filesystem::path& explicitManifest = {}, ProjectBuildProfile profile = ProjectBuildProfile::Standard);
    [[nodiscard]] static Result<Process> Launch(const ProjectContext& project, const EngineInstallation& engine, ProjectHostKind kind,
        bool checkOnly = false);
    /** \brief 非表示・有限frame等の明示オプションで同じProject起動経路を利用する */
    [[nodiscard]] static Result<Process> Launch(const ProjectContext& project, const EngineInstallation& engine, ProjectHostKind kind,
        const ProjectLaunchOptions& options);
};
}
