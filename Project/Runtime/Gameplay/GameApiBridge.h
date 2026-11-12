#pragma once
#include "Public/Nexus/GameApi.h"
#include "Public/Nexus/ScriptApi.h"
#include "Public/Nexus/ScriptPropertyApi.h"
#include "Public/Nexus/GameServices.h"

namespace NexusEngine {
class Level;
/*-----------------------------------------------------------------------------------------
 * GameApiBridge
 * - Runtime所有Levelを公開Game APIのWorld Handleへ接続する
 * - Levelを所有せず、GameRuntimeがLevel破棄より先にtokenを失効させる
 *---------------------------------------------------------------------------------------*/
class GameApiBridge final {
public:
    /** \brief 現在threadに属するWorld tokenを登録する。LevelはBridgeより長く生存する */
    explicit GameApiBridge(Level& level);
    /** \brief tokenを失効させ、停止済みModuleからの古いHandleアクセスを拒否する */
    ~GameApiBridge();
    GameApiBridge(const GameApiBridge&) = delete;
    GameApiBridge& operator=(const GameApiBridge&) = delete;
    [[nodiscard]] uint64_t World() const noexcept { return world_; }
    /** \brief Engine側で固定された関数表を値コピーする。Moduleからの変更はEngineへ伝播しない */
    [[nodiscard]] static NexusGameApiV1 Api() noexcept;
    /** \brief 同じWorldのScriptComponent関連付けを操作する追加関数表を返す */
    [[nodiscard]] static NexusScriptApiV1 Scripts() noexcept;
    /** \brief 共通Componentの保存値へ限定したcopy APIを返す。所有thread検証は既存World経路を再利用する */
    [[nodiscard]] static NexusScriptPropertyApiV1 Properties() noexcept;
    /** \brief RuntimeのReload transactionだけがScene交換後の接続を更新する。callback中の使用は禁止 */
    [[nodiscard]] bool RebindScene() noexcept;
    /** \brief 同じWorldへ接続する値APIを返す。ECSやAssetの別Storageは作らない */
    [[nodiscard]] static NexusGameServicesApiV1 Services() noexcept;
    /** \brief 検証済みNative loaderだけが呼ぶ登録権限付きclient生成経路 */
    [[nodiscard]] static NexusGameStatus OpenNativeServices(uint64_t world, uint64_t* client) noexcept;
    /** \brief Physics step終了後に、明示購読clientだけへ既存Event queueで接触値を配送する */
    void PublishPhysicsContacts() noexcept;
    /** \brief Reload中はManagedのNative呼出し・イベント操作を止め、DLL内stateへの巻戻せない副作用を防ぐ */
    [[nodiscard]] bool FreezeManagedServices(bool frozen) noexcept;
    /** \brief 登録窓口が別LevelのWorld tokenを使用していないことを所有threadで検証する */
    [[nodiscard]] static bool MatchesWorld(Level& level, uint64_t world) noexcept;
    /** \brief Reloadの呼出停止を検証し、実行中callbackとして記録する。lock外でDLLを呼ぶ */
    [[nodiscard]] static NexusGameStatus BeginNativeCall(uint64_t world) noexcept;
    /** \brief callback完了後に呼出深さを戻し、Reloadの安全境界を復元する */
    static void EndNativeCall(uint64_t world) noexcept;
    /** \brief 既存Live++専用のProcess境界を取得する。Interop World存在中はパッチを拒否する */
    [[nodiscard]] static bool BeginLegacyPatch() noexcept;
    /** \brief Module-onlyパッチの対象がProcess内で唯一のInterop Worldか検査し、新規World開始を止める */
    [[nodiscard]] static bool BeginGameModulePatch(Level& level) noexcept;
    /** \brief 両Native Backendの同期パッチ境界を解放する。取得成功した所有Threadだけが解放する */
    static void EndLegacyPatch() noexcept;
    /** \brief 不完全なInterop終了後はProcess再起動まで従来パッチを禁止する */
    static void BlockLegacyPatches() noexcept;
private:
    uint64_t world_ = 0; //< 0は無効。Process内で再利用しないWorld寿命token
};
}
