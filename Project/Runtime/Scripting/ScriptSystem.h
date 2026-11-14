#pragma once
#include "Runtime/Scene/SystemScheduler.h"
#include "Runtime/Scene/EntityId.h"
#include "Public/Nexus/ScriptApi.h"
#include <string>
#include <thread>

namespace NexusEngine {
class NativeManagedBridge;
class Level;
/*-----------------------------------------------------------------------------------------
 * ScriptSystem
 * - 既存SceneのScriptComponentを一括同期し、Simulation内でManaged lifecycleを更新する
 * - Managed callback前にECS反復を終え、Component削除後のNative参照を使用しない
 *---------------------------------------------------------------------------------------*/
class ScriptSystem final : public ISceneSystem {
public:
    ScriptSystem(Level& level, NativeManagedBridge& bridge, uint64_t world);
    /** \brief OnCreate / OnEnableを初期Transform確定前に実行し、OnUpdate(0)は呼ばない */
    [[nodiscard]] Result<void> Initialize();
    /** \brief Managed参照が解放される前に無効化する。残ったSchedulerから呼ばれても何もしない */
    void Detach() noexcept;
    /** \brief Reload transactionのScene交換後に借用を更新する \param resetInstances 新世代にはtrue、旧世代を維持するPrepare取消ではfalse */
    [[nodiscard]] bool RebindScene(bool resetInstances = true) noexcept;
    std::span<const std::type_index> GetReadComponents() const noexcept override;
    std::span<const std::type_index> GetWriteComponents() const noexcept override;
    /** \brief Scriptは公開Game APIからEntity構造も変更できるため、型一覧だけで並列安全とは扱わない */
    bool HasUnspecifiedComponentAccess() const noexcept override { return true; }
    [[nodiscard]] Result<void> Update(Scene& scene, float deltaTime) override;
    Result<void> FixedUpdate(Scene& scene, float deltaTime) override;
private:
    struct Snapshot {
        EntityId entity; std::string type; uint64_t instance, attachment; uint32_t enabled;
    };
    [[nodiscard]] Result<void> Synchronize(float deltaTime, bool initializeOnly, bool fixedOnly = false);
    Scene* scene_; //< Level所有。Detach後は参照しない
    Level* level_; //< Scene交換・Unload後に旧Sceneをdereferenceしないための所有元照合
    NativeManagedBridge* bridge_; //< CSharpRuntime所有。Stop時に先にDetachして借用を解除する
    uint64_t world_; //< 公開APIと共通のWorld token
    std::thread::id thread_; //< ECS snapshotもManaged呼出し前に所有threadを検証する
    std::vector<Snapshot> snapshots_; //< callback中に移動させない型名の所有buffer
    std::vector<NexusScriptBindingV1> bindings_; //< 一回のInterop中だけManagedへ貸すPOD配列
};
}
