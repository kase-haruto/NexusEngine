#pragma once

#include <memory>
#include <filesystem>

namespace NexusEngine {
    class GameRuntime;
    struct NativeGameModuleSettings;
    /** Native backendの結果。Managed AssemblyやDLL入替の状態とは混同しない。 */
    enum class NativeReloadState { Unavailable, Idle, Queued, Reloading, Succeeded, NoChanges, CompileError, CompatibilityError, StateRestoreError };
    /*-----------------------------------------------------------------------------------------
     * LiveCodingAgent
     * - Framework所有の同期Live++ AgentとNativeパッチ結果を管理する
     * - 要求・結果は値だけで共有し、SDK呼出しはFrameworkの所有Threadへ限定する
     * - Managed AssemblyやGame DLLのUnload / Reloadを実行しない
     *---------------------------------------------------------------------------------------*/
    class LiveCodingAgent final {
    public:
        explicit LiveCodingAgent(bool enabled);
        ~LiveCodingAgent();
        LiveCodingAgent(const LiveCodingAgent&) = delete;
        LiveCodingAgent& operator=(const LiveCodingAgent&) = delete;

        [[nodiscard]] bool WantsReload();
        void ScheduleReload();
        void Reload();
        /** \brief Frame境界で未対応のInterop併用を拒否し、明示再要求まで自動適用を止める */
        void RejectReload();
        /** \brief Editor共通Commandから要求だけを送る。SDK操作は次のFramework境界で行う */
        [[nodiscard]] static bool RequestReload() noexcept;
        /** \brief UIと要求経路で同じ利用可否を参照する。SDKへの要求は発行しない */
        [[nodiscard]] static bool CanReload() noexcept;
        [[nodiscard]] static NativeReloadState State() noexcept;
        /** \brief EXEのReinstancingと独立DLLの固定stateパッチを別profileとして選択する */
        [[nodiscard]] bool IsGameModuleBackend() const noexcept;
        /** \brief Frame境界でRuntimeを停止し、同期SDK結果を確認してBindingを再登録する */
        void ReloadGameModule();
        /**
         * \brief Runtime所有のDLLを非所有登録する。Agent生成前は保留し、同時に一つだけ受け付ける
         * \param runtime 登録中に存続し、停止前に解除する所有Runtime
         * \param settings DLL・Source・Projectの絶対配置と明示profile
         * \return SDK対応profileかつ登録可能ならtrue。所有権は移動しない
         */
        [[nodiscard]] static bool RegisterGameRuntime(GameRuntime& runtime, const NativeGameModuleSettings& settings) noexcept;
        /**
         * \brief DLLがロード中の間にDisable通知とObserver解除を行う
         * \param runtime 同じ所有Threadから解除する登録元
         * \return Observerを安全に解除できたか。falseでは呼出元がDLLをpinする
         */
        [[nodiscard]] static bool UnregisterGameRuntime(GameRuntime& runtime) noexcept;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
        /** \brief 保留中RuntimeをSDKへ接続する。Enableは非同期通知であり完了保証ではない */
        [[nodiscard]] bool AttachGameModule();
        /** \brief SDKのDisable通知後にhookからの借用参照を切る。DLL自体は解放しない */
        [[nodiscard]] bool DetachGameModule() noexcept;
    };
}
