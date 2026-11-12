#pragma once
#include "NativeGameApiMetadata.h"
#include "Public/Nexus/NativeGameBindings.h"
#include "Public/Nexus/NativeGameState.h"
#include <memory>
#include <span>

namespace NexusEngine {
class Level;
/*-----------------------------------------------------------------------------------------
 * NativeGameSystemScope
 * - 一つのGame Moduleの登録窓口・非所有System参照・数値Handle失効を担当する
 * - System adapterの所有・phase順実行は既存SystemScheduler、関数契約はNativeGameApiMetadataへ委譲する
 * - Game固有Systemの型・ロジック、Managed Facadeの実装は管理しない
 *---------------------------------------------------------------------------------------*/
class NativeGameSystemScope final {
public:
    /** \brief 同じLevelへModuleの登録scopeを作る \param level SchedulerとEntityを所有するLevel \param world 公開Game APIのWorld token \param moduleId Loaderが照合済みのProject Module ID */
    NativeGameSystemScope(Level& level, uint64_t world, std::string_view moduleId = "Game");
    ~NativeGameSystemScope();
    NativeGameSystemScope(const NativeGameSystemScope&) = delete;
    NativeGameSystemScope& operator=(const NativeGameSystemScope&) = delete;
    /** \brief 呼出し中だけ借用する登録contextを値で返す \return Moduleがコピーできる数値scopeとEngine関数表 */
    [[nodiscard]] NexusNativeSystemsRegistrationV1 Context() const noexcept;
    /** \brief V1を変更せず、明示Binding用の別exportへ渡すcontextを返す */
    [[nodiscard]] NexusNativeSystemsRegistrationV2 BindingContext() const noexcept;
    /** \brief DLL pointerを含まないEngine常駐のManaged呼出し関数表を返す */
    [[nodiscard]] static NexusNativeBindingApiV1 BindingApi() noexcept;
    /** \brief System追加・署名変更を終了する。Binding差替えは別の停止・世代更新契約に従う */
    void Seal() noexcept;
    /** \brief idle境界でBindingを停止する。世代は保持しCompile失敗時にそのまま再開できる */
    [[nodiscard]] bool PreparePatch() noexcept;
    /** \brief 全既存Bindingの再登録が揃った場合だけ新世代を公開する。非適用なら旧表を維持する */
    [[nodiscard]] bool FinishPatch(bool applied) noexcept;
    /** \brief Binding停止を保ったまま、明示Restoreの間だけ既存Systemの同期借用を許す */
    [[nodiscard]] bool RestoreState(NexusRestoreNativeGameStateFn restore,void* state,uint64_t schema,std::span<const uint8_t> data) noexcept;
    /** \brief ABI違反後にDLL callbackを呼ばず切断する。呼出元はDLLとstateをProcess終了までpinする */
    void Abandon() noexcept;
    /** \brief APIを失効させ、逆登録順にSystemを停止・Schedulerから解除する \return 安全に破棄できなければfalse。呼出元はDLLをpinする */
    [[nodiscard]] bool Close() noexcept;
    /** \brief Runtime内の調査用Metadata。Game / Managedへ内部pointerを公開しない */
    [[nodiscard]] const NativeGameApiMetadata& Publications() const noexcept;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_; //< 呼出し中のscope情報だけを保持する。System本体は所有しない
};
}
