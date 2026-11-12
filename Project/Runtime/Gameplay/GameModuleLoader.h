#pragma once
#include "Public/Nexus/GameModule.h"
#include "Public/Nexus/GameServices.h"
#include "Public/Nexus/NativeGamePatch.h"
#include "Public/Nexus/NativeGameState.h"
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <string>
#include <thread>
#include <memory>
#include <vector>

namespace NexusEngine {
class Level;
class NativeGameSystemScope;
class GameRuntime;
/*-----------------------------------------------------------------------------------------
 * NativeGameModuleSettings
 * - ホストが解決したDLL配置と期待する識別情報を値として保持する
 * - ProjectのSource配置やLevelデータからDLLパスを推測しない
 *---------------------------------------------------------------------------------------*/
struct NativeGameModuleSettings {
    std::filesystem::path file; //< ホストで解決した絶対パス
    std::string engineId, engineVersion; //< 選択Engineとの完全一致を要求する
    std::string projectId, moduleId; //< 別Project / ModuleのDLL混入を拒否する
    std::filesystem::path patchSourceDirectory{}; //< 明示指定時だけ関数本体パッチ契約を検証する。空なら従来どおり
    bool livePatch = false; //< 開発用Module-only Backendへの明示opt-in。Source検証だけの利用と区別する
    std::filesystem::path patchProjectDirectory{}; //< BackendがProjectのBuild設定変更を検査する絶対Root
    // ホストが安定Engine関数だけを注入する。RuntimeからSDK Backendへの逆依存を作らない。
    bool (*registerPatchRuntime)(GameRuntime&, const NativeGameModuleSettings&) noexcept = nullptr; //< 登録中もRuntime所有権を移さない
    bool (*unregisterPatchRuntime)(GameRuntime&) noexcept = nullptr; //< DLL・World破棄前に非所有参照を解除する
    std::filesystem::path sourceDirectory{}; //< MetadataのSource対応だけに使うModule相対パスの基準。配布時は存在不要
    std::string componentMetadata{}; //< inspection済みJSON。ロード前後・Native patch時に同じ宣言か照合する
    bool componentMetadataChecked = false; //< 旧Loader単体利用と「exportなしを検査済み」を区別する
};

/*-----------------------------------------------------------------------------------------
 * GameModuleLoader
 * - Native Game DLLのABI検証、state生成・更新・破棄とUnloadを担当する
 * - Levelを所有しない。callbackはGameRuntimeを実行するthreadからだけ呼び出す
 * - Loaderの所有者も操作終了まで生存し、破棄は同じ所有Threadで行う
 *---------------------------------------------------------------------------------------*/
class GameModuleLoader final {
public:
    GameModuleLoader();
    ~GameModuleLoader();
    GameModuleLoader(const GameModuleLoader&) = delete;
    GameModuleLoader& operator=(const GameModuleLoader&) = delete;
    /**
     * \brief DLLをロードし、metadata検証後に一つのWorldのstateを生成する
     * \param settings ロード対象と期待する識別情報
     * \param context 呼出し中だけ借用するGame APIとWorld Handle
     * \param level 任意System exportが使用する既存Schedulerの所有Level。旧DLLでは省略可能
     * \return 失敗時は生成済みstateを同じDLLで破棄してからUnloadする
     */
    [[nodiscard]] Result<void> Start(const NativeGameModuleSettings& settings, const NexusGameModuleContextV1& context, Level* level = nullptr);
    /** \brief 既存WorldのSimulation前にゲームロジックを更新する。失敗後はRuntime側で停止する */
    [[nodiscard]] Result<void> Update(float deltaTime);
    /** \brief Level遷移中だけコードimageを保持し、Live++適用済み関数をUnloadで失わないようにする */
    [[nodiscard]] Result<std::shared_ptr<void>> RetainCode();
    /** \brief 所有ThreadでstateをDLL内破棄してUnloadする。callback中は外側の操作終了まで停止を遅延する
     * \note Loader自体の破棄はcallback終了後に行う。誤Threadの要求は拒否し、所有Threadで再試行する
     */
    void Stop() noexcept;
    [[nodiscard]] bool IsQuarantined() const noexcept { return quarantined_; }
    /** \brief DLLを保持したままSystem / Bindingを停止する。Sourceと固定state契約が一致する場合だけ許可 */
    [[nodiscard]] Result<void> PreparePatch();
    /** \brief Backendが実際に適用した場合だけ再登録する。falseはCompile失敗・非適用で旧表を再開する */
    [[nodiscard]] Result<void> FinishPatch(bool applied);
    /** \brief SDK呼出し前に所有Threadで契約を再検査する。hookの入力検査は値snapshotを使用する */
    [[nodiscard]] bool ValidatePreparedPatch() noexcept;
    /** \brief header内容と翻訳単位一覧を値copyする。SDK hookはWorldへ触れず同じ入力規則で検査する
     * \param root Native Sourceの絶対directory
     * \return Loader / DLL / Worldを借用しないsnapshot。不正入力・I/O失敗は呼出元で例外を捕捉する
     */
    [[nodiscard]] static std::vector<std::pair<std::filesystem::path, std::string>> ReadPatchSourceSnapshot(const std::filesystem::path& root);
    /** \brief 適用結果を確定できない場合はDLL stateを破棄せずpinする。通常再開・再利用は行わない */
    void AbandonPatch() noexcept;
private:
    struct OperationGuard; //< DLL callbackのstackが戻るまでstateとコードを保持する所有Thread限定guard
    void* moduleHandle_ = nullptr; //< Windows DLL参照を所有。公開ヘッダへHMODULEを漏らさない
    void* state_ = nullptr; //< DLL所有の不透明state。Engineは参照解除以外の操作をしない
    NexusGameModuleV1 module_{}; //< DLL内の関数・静的文字列。Unload前に破棄する
    std::string path_; //< エラーとログ用にコピーしたUTF-8配置
    std::thread::id thread_; //< create / update / destroyを直列実行する所有thread
    bool quarantined_ = false; //< 破棄契約違反後は同じRuntimeからの再ロードを拒否する
    uint64_t servicesClient_ = 0; //< 任意exportの登録寿命。DLL state破棄より先に失効させる
    std::unique_ptr<NativeGameSystemScope> systems_; //< 非所有System追跡とAPI契約。SchedulerのadapterをDLLより先に解除する
    NativeGameModuleSettings identity_; //< 再取得したModuleの識別情報を起動時と照合する
    NexusNativePatchContractV1 patchContract_{}; //< stateを作成したDLLの固定layout契約の値コピー
    uint64_t nativeStateSchema_=0; //< 任意の値snapshot schema。0なら従来の固定state保持だけを使う
    std::vector<uint8_t> nativeState_; //< Engine所有copy。DLL/Managed pointerやallocatorを含めない
    std::vector<std::pair<std::filesystem::path, std::string>> patchSources_; //< ヘッダー内容とCPPファイル一覧。関数本体は保持しない
    bool patchPrepared_ = false, patchFailed_ = false, unsafePatch_ = false; //< 適用失敗後に旧stateへ新ABIのdestroyを呼ばない
    bool operationActive_ = false, stopRequested_ = false, stopping_ = false; //< 再入Update / patchを拒否し、停止要求とdestroy再入を分離する
};
}
