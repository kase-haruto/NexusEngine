#pragma once

#include "Runtime/Level/Level.h"
#include "Runtime/Gameplay/GameModuleLoader.h"
#include "Runtime/Scripting/CSharpRuntimeSettings.h"
#include <functional>
#include <optional>
#include <vector>
#include <thread>

namespace NexusEngine {
class GameApiBridge;
class CSharpRuntime;
/** Nativeの関数本体パッチ結果。DLL交換・Managed Reloadとは異なる契約。 */
enum class NativePatchState { Idle, Prepared, Succeeded, NotApplied, CompatibilityError, StateRestoreError };

/** Game側がLevelごとに独立したSystemと初期状態を構築する。参照を外部へ保持しないこと。 */
using GameLevelSetup = std::function<Result<void>(Level&)>;

/*-----------------------------------------------------------------------------------------
 * GameRuntime
 * - 実行用LevelとNative / Managedゲームコードの所有、再生・一時停止・停止を担当する
 * - Editor、Window、Rendererには依存しない。ゲームコードをWorldより先に停止して参照寿命を守る
 * - 最初の有効なStart要求から所有Threadを固定する。照会・破棄も同Threadで行い、初期化中の同時操作は行わない
 *---------------------------------------------------------------------------------------*/
class GameRuntime final {
public:
    GameRuntime();
    /** \brief 開始threadでゲームコード停止、Host参照解放、World token失効、Level破棄を完了する */
    ~GameRuntime();
    GameRuntime(const GameRuntime&) = delete;
    GameRuntime& operator=(const GameRuntime&) = delete;
    /** \brief 停止中に独立Native Moduleを選択する。DLLはStartまでロードしない */
    [[nodiscard]] bool SetNativeGameModule(NativeGameModuleSettings settings);
    /** \brief 依存順のDLL設定を受け取る。全Moduleは同じWorldを使い、逆順に終了する */
    [[nodiscard]] bool SetNativeGameModules(std::vector<NativeGameModuleSettings> settings);
    /** \brief 停止中にManaged bootstrapを選択する。Nativeと同じLevel / World APIを共有する */
    [[nodiscard]] bool SetCSharpRuntime(CSharpRuntimeSettings settings);
    /** \brief 停止中に構築処理を設定する。再生ごとに新しいLevelへ一度だけ呼び出す */
    [[nodiscard]] bool SetLevelSetup(GameLevelSetup setup);
    /** \brief Applicationから入力値を受け取る。Worldと同じ所有threadだけで使用する */
    void SetFrameInput(const InputFrame& input) noexcept { if(OnOwnerThread() && level_) level_->SetFrameInput(input); }
    /** \brief LoadedなLevelの所有権を受け取り、構築成功後に実行状態へ移る */
    [[nodiscard]] Result<void> Start(std::unique_ptr<Level> level);
    /** \brief 開始threadから実行Worldを停止する。callback内では解放をframe境界へ遅延する
     * \note 誤Threadの要求は状態を変えず拒否する。所有Threadで再試行し、所有者も同Threadで破棄する
     */
    void Stop() noexcept;
    void TogglePause() noexcept;
    [[nodiscard]] bool IsCSharpSuspended() const noexcept;
    [[nodiscard]] ManagedReloadState CSharpReloadState() const noexcept;
    /** \brief 一時停止中はSystemを呼び出さない。無効な経過時間は拒否する */
    [[nodiscard]] Result<void> Update(float deltaTime);
    /** \brief 所有threadのframe境界でManagedだけをReloadする。Live++やNative Moduleには転送しない */
    [[nodiscard]] Result<void> ReloadCSharp(const std::filesystem::path& assembly);
    /** \brief 同期BackendがSDK呼出しより前に取得する境界。Native / Managed双方の更新を止める */
    [[nodiscard]] Result<void> PrepareNativePatch();
    /** \brief Backendが適用結果を確定して呼ぶ。途中状態を成功と偽って再開してはならない */
    [[nodiscard]] Result<void> FinishNativePatch(bool applied);
    /** \brief SDK呼出し前に所有ThreadでWorld・Module契約を検査する。別Threadのhookからは呼ばない */
    [[nodiscard]] bool ValidateNativePatch() noexcept;
    /** \brief Backendの終了・通知契約が崩れた場合はDLLをpinし、World更新を再開しない */
    void AbandonNativePatch() noexcept;
    [[nodiscard]] NativePatchState NativePatchStatus() const noexcept { return nativePatchState_; }
    [[nodiscard]] Level* GetLevel() noexcept { return level_.get(); }
    [[nodiscard]] bool IsRunning() const noexcept { return level_ != nullptr; }
    [[nodiscard]] bool IsPaused() const noexcept { return paused_ || nativePatchPrepared_ || nativePatchFailed_ || IsCSharpSuspended(); }

private:
    /** \brief 一つのWorldのcallbackを完了させてから外側のUpdateへ戻す */
    [[nodiscard]] Result<void> UpdateWorld(float deltaTime);
    /** \brief World所有確定前の逐次設定を許可し、確定後の誤Thread操作を状態参照より先に拒否する */
    [[nodiscard]] bool OnOwnerThread() const noexcept { return thread_ == std::thread::id{} || thread_ == std::this_thread::get_id(); }
    std::thread::id thread_; //< 最初の有効なStartで固定し、Stop後も維持する。InteropなしのCpp-onlyにも同じ制約を適用する
    GameLevelSetup setup_; //< WorldごとにSystemを生成するGame側の登録処理
    std::unique_ptr<Level> level_; //< 実行中のWorldとSystemをまとめて所有する
    std::optional<NativeGameModuleSettings> nativeSettings_; //< ホストで解決した配置。RuntimeだけがDLLを所有する
    std::unique_ptr<GameApiBridge> gameApi_; //< Levelを借用する公開API接続。Moduleより長く生存する
    std::unique_ptr<GameModuleLoader> nativeModule_; //< World単位のGame stateとDLL参照
    std::vector<NativeGameModuleSettings> additionalNativeSettings_; //< 先頭以外の依存順設定。DLLを所有しない
    std::vector<std::unique_ptr<GameModuleLoader>> additionalNativeModules_; //< 同一Worldの追加DLLを依存順に所有する
    std::optional<CSharpRuntimeSettings> managedSettings_; //< 明示されたBootstrapと.NET Hosting配置
    std::unique_ptr<CSharpRuntime> csharpRuntime_; //< Editorではなく実行WorldのRuntimeが所有する
    bool managedQuarantined_ = false; //< Managed停止失敗後はProcess再起動を要求する
    bool nativeQuarantined_ = false; //< destroy失敗後の再生を拒否し、Process再起動を要求する
    bool paused_ = false; //< ユーザーのPause。Managed障害による停止は別状態から計算し、復旧時に勝手に反転させない
    bool starting_ = false; //< Setup callbackからの再入を拒否する
    bool updating_ = false; //< callback実行中にWorldを解放させない
    bool stopping_ = false; //< OnDestroyからのStop再入を拒否する
    bool pendingStop_ = false; //< callback内の停止要求は安全なframe境界へ遅延する
    NativePatchState nativePatchState_ = NativePatchState::Idle; //< UIへは結果の値だけを渡す
    bool nativePatchPrepared_ = false, nativePatchFailed_ = false; //< 失敗・適用中にSystem / Scriptを更新させない
    bool livePatchRegistered_ = false; //< Backendの非所有参照をDLL・Worldより先に解除する
};
}
