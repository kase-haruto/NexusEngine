#pragma once
#include "Runtime/Curves/FloatCurve.h"
#include <utility>

namespace NexusEngine {
/** \brief Entity側が所有する再生設定。Curveの意味とloopは利用側だけに置く。 */
struct AnimationPlaybackSettings {
    double duration = 1; //< clip長。0は静止clip
    double baseSpeed = 1; //< 負値で逆再生
    double curveDuration = 0; //< 0はclip長。正規化Xが1以降はcurveの境界規則を使用
    double initialTime = 0; //< Seekの原点。逆再生では終端などを明示できる
    bool loop = true; //< clip時刻だけをwrapし、curveの経過時間は巻き戻さない
};
/** \brief 時間速度の零点で分割した実際の移動区間。Seekの飛び区間は含めず、更新前後の経過秒を渡す。 */
std::vector<std::pair<double, double>> AnimationTraversal(double beginElapsed, double endElapsed,
    const AnimationPlaybackSettings& settings, const FloatCurve* curve = nullptr);

/** \brief 骨格/プロパティに依存しない共通clock。Curveやassetを所有しない。 */
class AnimationPlayback final {
public:
    void Play();
    void Pause();
    void Stop();
    /** \brief 非負の独立経過秒へ移動し、解析積分からclip時刻を再計算する。 */
    void Seek(double elapsed, const AnimationPlaybackSettings& settings, const FloatCurve* curve = nullptr);
    /** \brief 正のdtでのみ進める。停止中も設定・未保存curve変更を再評価する。 */
    void Update(double deltaTime, const AnimationPlaybackSettings& settings, const FloatCurve* curve = nullptr);
    double Elapsed() const noexcept;
    double Time() const noexcept;
    double UnwrappedTime() const noexcept;
    bool IsPlaying() const noexcept;
    bool IsCompleted() const noexcept;
private:
    double elapsed_ = 0; //< 速度に影響されない経過秒
    double time_ = 0, unwrapped_ = 0; //< 評価結果。共有assetには格納しない
    bool playing_ = false, completed_ = false; //< 完了とユーザーの再生要求を分離する
};
}
