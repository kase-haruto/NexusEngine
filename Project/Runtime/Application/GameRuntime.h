#pragma once

#include "Runtime/Level/Level.h"
#include <functional>

namespace NexusEngine {

/** Game側がLevelごとに独立したSystemと初期状態を構築する。参照を外部へ保持しないこと。 */
using GameLevelSetup = std::function<Result<void>(Level&)>;

/*-----------------------------------------------------------------------------------------
 * GameRuntime
 * - 実行用Levelの所有、ゲームSystemの構築、再生・一時停止・停止を担当する
 * - Editor、Window、Rendererには依存しない。Systemの寿命は実行用Levelに従う
 *---------------------------------------------------------------------------------------*/
class GameRuntime final {
public:
    /** \brief 停止中に構築処理を設定する。再生ごとに新しいLevelへ一度だけ呼び出す */
    [[nodiscard]] bool SetLevelSetup(GameLevelSetup setup);
    /** \brief LoadedなLevelの所有権を受け取り、構築成功後に実行状態へ移る */
    [[nodiscard]] Result<void> Start(std::unique_ptr<Level> level);
    /** \brief 実行中のSystemとWorldを破棄する。複数回呼出し可能 */
    void Stop() noexcept;
    void TogglePause() noexcept { if(level_) paused_ = !paused_; }
    /** \brief 一時停止中はSystemを呼び出さない。無効な経過時間は拒否する */
    [[nodiscard]] Result<void> Update(float deltaTime);
    [[nodiscard]] Level* GetLevel() noexcept { return level_.get(); }
    [[nodiscard]] bool IsRunning() const noexcept { return level_ != nullptr; }
    [[nodiscard]] bool IsPaused() const noexcept { return paused_; }

private:
    GameLevelSetup setup_; //< WorldごとにSystemを生成するGame側の登録処理
    std::unique_ptr<Level> level_; //< 実行中のWorldとSystemをまとめて所有する
    bool paused_ = false; //< Pause中は副作用のあるUpdate(0)も実行しない
    bool starting_ = false; //< Setup callbackからの再入を拒否する
};
}
