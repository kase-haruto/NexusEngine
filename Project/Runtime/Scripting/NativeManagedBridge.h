#pragma once
#include "Public/Nexus/ManagedBootstrap.h"
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <thread>
#include <span>
#include <string>

namespace NexusEngine {
class DotNetHost;
/*-----------------------------------------------------------------------------------------
 * NativeManagedBridge
 * - BootstrapのStart / Stop呼出しと数値Managed session IDの寿命を管理する
 * - .NET初期化、ECS所有、GCHandle所有を行わない。Managed例外はManaged側でstatusへ変換する
 *---------------------------------------------------------------------------------------*/
class NativeManagedBridge final {
public:
    NativeManagedBridge() = default;
    ~NativeManagedBridge();
    NativeManagedBridge(const NativeManagedBridge&) = delete;
    NativeManagedBridge& operator=(const NativeManagedBridge&) = delete;
    /** \brief 全entryを解決してからセッションを開始する。contextのNativeアドレスは保持させない */
    [[nodiscard]] Result<void> Start(DotNetHost& host, const std::filesystem::path& assembly, const NexusManagedBootstrapContextV1& context);
    /** \brief V1を変更せず、Gameplay専用entryと一括Tickを解決して開始する */
    [[nodiscard]] Result<void> StartGameplay(DotNetHost& host, const std::filesystem::path& assembly, const NexusManagedGameplayContextV2& context);
    /** \brief V3で共通Property状態のcopy APIを渡す。旧entryは既存利用者向けに維持する */
    [[nodiscard]] Result<void> StartReflectedGameplay(DotNetHost& host, const std::filesystem::path& assembly, const NexusManagedGameplayContextV3& context);
    /** \brief V4で同じWorldのNative登録APIと値Eventに接続する */
    [[nodiscard]] Result<void> StartHybridGameplay(DotNetHost& host, const std::filesystem::path& assembly, const NexusManagedGameplayContextV4& context);
    /** \brief V5でEngine常駐のNative System dispatchを渡す。Game DLLのpointerは渡さない */
    [[nodiscard]] Result<void> StartBoundGameplay(DotNetHost& host, const std::filesystem::path& assembly, const NexusManagedGameplayContextV5& context);
    /** \brief 一回のInteropで既存ECSの関連付け全体を同期・更新する。bufferをManagedへ保持させない */
    [[nodiscard]] Result<void> Tick(std::span<NexusScriptBindingV1> bindings, float deltaTime, bool initializeOnly);
    /** \brief frame境界でReload段階を実行する。失敗時もWorldを破棄せず停止状態を上位へ伝える */
    [[nodiscard]] Result<void> Reload(uint32_t operation, const std::filesystem::path& assembly = {});
    [[nodiscard]] bool IsSuspended() const noexcept { return suspended_; }
    /** \brief Bootstrapが関連付けtokenによる複数Script同期を明示対応しているかを返す */
    [[nodiscard]] bool SupportsScriptAttachments() const noexcept { return scriptAttachments_; }
    /** \brief 同じthreadでManaged参照を解除する。失敗時は上位RuntimeにProcess再起動を要求する */
    [[nodiscard]] bool Stop() noexcept;
private:
    [[nodiscard]] Result<void> ReloadMono(uint32_t operation, const std::filesystem::path& assembly);
    DotNetHost* host_ = nullptr; //< 借用HostはBridgeより長寿命。Mono thunk呼出し前に現在Domainを選ぶ
    std::filesystem::path bootstrap_; //< Mono Domain差替えで同じ安定Bootstrapを再解決する
    NexusManagedGameplayContextV5 monoContext_{}; //< WorldとEngine関数表のコピー。文字列pointerは呼出し直前に設定する
    std::string monoGameAssembly_; //< 開始contextの借用文字列を所有コピーする
    bool monoSession_ = false, monoCommitted_ = false, cleanupFailed_ = false;
    NexusManagedStartFn start_ = nullptr; //< Process寿命のBootstrap entry。使用範囲はセッション中だけ
    NexusManagedStopFn stop_ = nullptr; //< 部分的な開始失敗時にも呼べる停止entry
    NexusManagedGameplayStartFn gameplayStart_ = nullptr; //< V2開始entry。V1と署名を混同しない
    NexusManagedTickFn tick_ = nullptr; //< Script全体の同期・lifecycle更新entry
    NexusManagedReloadFn reload_ = nullptr; //< 安定BootstrapのReload entry。Game delegateではない
    bool suspended_ = false; //< Unload失敗・初期化失敗後は通常Tickを再開させない
    bool scriptAttachments_ = false; //< 旧Bootstrapへ複数関連付けを渡して黙って無視させない
    uint64_t session_ = 0; //< Managed所有辞書のID。ポインタやGCHandleではない
    std::thread::id thread_; //< Managed sessionを作成したthread
};
}
