#pragma once
#include "PhysicsWorld.h"
#include "Runtime/Scene/SystemScheduler.h"

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * PhysicsUpdateSystem
 * - 既存SchedulerのPhysics段階でJoltを実行し、Transform段階へ結果を渡す
 * - solverは必要時だけ生成する。EditorのUpdate(0)は物理世界を生成・進行しない
 * - solverはLevelが所有し、Scene交換時は同じEntity IDの旧bodyも破棄される
 *---------------------------------------------------------------------------------------*/
class PhysicsUpdateSystem final : public ISceneSystem {
public:
    /** \brief Level所有のsolver slotを借用する。LevelはSystem破棄後までslotを保持する */
    explicit PhysicsUpdateSystem(std::unique_ptr<PhysicsWorld>& world,const PhysicsWorld::ModelSource& models) noexcept : world_(world),models_(models) {}
    /** \brief ゲームロジック実行後、派生Matrix確定前の段階を返す */
    SystemPhase GetPhase() const noexcept override { return SystemPhase::Physics; }
    /** \brief 親子関係を検証するための読み取り対象を返す */
    std::span<const std::type_index> GetReadComponents() const noexcept override;
    /** \brief pose・速度・一時的な力を書き戻す対象を返す */
    std::span<const std::type_index> GetWriteComponents() const noexcept override;
    /** \brief 正の時間だけsimulationを実行する。Scene/solverへのアクセスは所有threadに限定する */
    Result<void> Update(Scene& scene, float deltaTime) override;
private:
    std::unique_ptr<PhysicsWorld>& world_; //< Level所有slotの借用。System解除でLevel側の参照をdanglingにしない
    const PhysicsWorld::ModelSource& models_; //< Levelが保持するHostの同期CPU読込。Systemより長く生存する
};
}
