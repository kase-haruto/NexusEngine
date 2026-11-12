#pragma once
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <memory>

namespace NexusEngine {
struct CSharpRuntimeSettings;
class MonoHost;
/*-----------------------------------------------------------------------------------------
 * DotNetHost
 * - 公式nethost / hostfxrで.NETを初期化し、Managed静的methodのentryを取得する
 * - CLR内部APIを呼ばない。Shutdownはhost側参照の解放であり、Process内CoreCLRをUnloadしない
 * - hostfxrもProcess寿命へ固定し、繰り返すPlayでhostpolicyとの初期化状態を保持する
 *---------------------------------------------------------------------------------------*/
class DotNetHost final {
public:
    DotNetHost();
    ~DotNetHost();
    DotNetHost(const DotNetHost&) = delete;
    DotNetHost& operator=(const DotNetHost&) = delete;
    /** \brief 明示配置のnethostとruntimeconfigで初期化し、host contextを閉じてloader delegateを保持する */
    [[nodiscard]] Result<void> Initialize(const CSharpRuntimeSettings& settings);
    /**
     * \brief BootstrapのUnmanagedCallersOnly methodをロードする
     * \param assembly 絶対パスのManaged assembly
     * \param method 安定Bootstrap型の静的method名
     * \return Process寿命のManaged entry。呼出し署名の照合はNativeManagedBridgeが担当する
     */
    [[nodiscard]] Result<void*> Resolve(const std::filesystem::path& assembly, const wchar_t* method);
    /** \brief delegateとセッション参照を解放する。pinしたhostfxrとCoreCLRはProcess終了まで生存する */
    void Shutdown() noexcept;
    /** \brief Monoを選択したHostだけに、その実在するDomain制御を公開する。Editorは所有しない */
    [[nodiscard]] MonoHost* MonoBackend() noexcept { return mono_.get(); }
    [[nodiscard]] bool IsMono() const noexcept { return mono_ != nullptr; }
    [[nodiscard]] bool IsQuarantined() const noexcept { return quarantined_; }
private:
    struct State;
    std::unique_ptr<State> state_; //< hostfxrの型をRuntime公開ヘッダから隠蔽して所有する
    std::unique_ptr<MonoHost> mono_; //< 明示Mono Rootを指定したsessionだけが使う公式embedding backend
    bool quarantined_ = false; //< Domain解放失敗後の同Host再利用を禁止する
};
}
