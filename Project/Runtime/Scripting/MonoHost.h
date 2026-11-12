#pragma once
#include "Foundation/Error/Result.h"
#include <filesystem>
#include <memory>
#include <string>
#include <cstdint>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * MonoHost
 * - Mono公式embedding APIでProcess寿命のrootとsession寿命の子Domainを管理する
 * - Game Worldを所有せず、Managed cleanup後のDomain unload成功をNative側で検査する
 * - 旧Domainの関数pointerはNativeManagedBridgeが解除してからunloadする
 *---------------------------------------------------------------------------------------*/
class MonoHost final {
public:
    MonoHost();
    ~MonoHost();
    MonoHost(const MonoHost&) = delete;
    MonoHost& operator=(const MonoHost&) = delete;
    /** \brief 明示Mono RootのSDKを起動し、所有Thread上に子Domainを生成する */
    [[nodiscard]] Result<void> Initialize(const std::filesystem::path& root, const std::filesystem::path& bootstrap);
    /** \brief Domain存続中にManaged側がrootするCdecl delegate thunkを取得する */
    [[nodiscard]] Result<void*> Resolve(const std::filesystem::path& assembly, const wchar_t* method);
    /** \brief 同じThread上の複数Hostでもcallback先Domainを明示する。delegate呼出し直前に使う */
    [[nodiscard]] Result<void> Enter();
    /** \brief 既存sessionの公開状態を保存し、別Domainで候補を検証してそのDomainを解放する */
    [[nodiscard]] Result<void> PrepareReload(uint64_t session, const std::filesystem::path& assembly);
    /** \brief 旧Domainの終了callbackによるWorld書込を禁止し、次のinstance counterを値で取得する */
    [[nodiscard]] Result<void> BeginRetirement(uint64_t session);
    /** \brief 呼出側が旧delegateを解除した後、旧Domainを解放して新Domainを生成する */
    [[nodiscard]] Result<void> ActivatePrepared();
    /** \brief 候補sessionを初期化専用状態へ切り替える */
    [[nodiscard]] Result<void> MarkActivation(uint64_t session, bool pending);
    /** \brief 参照を持たない候補shadowを削除する。稼働Domainは停止しない */
    void CancelPrepared() noexcept;
    /** \brief 検証済み候補のshadow内Assembly位置を返す。Native DTOへの一時コピー用 */
    [[nodiscard]] const std::filesystem::path& PreparedAssembly() const noexcept;
    /** \brief 子Domainのunload結果を確認する。Mono root / DLLはProcess終了まで保持する */
    [[nodiscard]] bool Shutdown() noexcept;
    [[nodiscard]] bool IsQuarantined() const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_; //< 子Domain、所有shadow、候補snapshotだけを所有する
};
}
