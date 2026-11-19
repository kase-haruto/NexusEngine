#pragma once
#include "Foundation/Error/Result.h"
#include <cstddef>
#include <vector>

namespace NexusEngine {
enum class CurveInterpolation { Constant, Linear, Cubic, Bezier };
enum class CurveTangentMode { Free, Aligned, Broken, Auto, ClampedAuto };

/** \brief 用途や単位を持たないHermite曲線の制御点。補間は次のkeyまでの区間に適用する。 */
struct CurveKey {
    double x = 0; //< 汎用入力。厳密昇順で保持する
    double y = 1; //< 汎用出力。速度などの用途側の制約を持たない
    double inTangent = 0; //< dy/dx。Bezierのweightではない
    double outTangent = 0; //< dy/dx
    CurveInterpolation interpolation = CurveInterpolation::Linear; //< 右区間の補間
    CurveTangentMode tangentMode = CurveTangentMode::Broken; //< 自動または手動接線
    double inWeight = 1.0 / 3, outWeight = 1.0 / 3; //< Bezierの水平区間比。0..1、既定はHermiteと同形
    bool operator==(const CurveKey&) const;
};

/** \brief Editor/Entity/Graphicsから独立した数値曲線。境界は端点保持、空曲線は1。 */
class FloatCurve final {
public:
    /** \brief コピーを検証して交換する。失敗時は以前の曲線を保持する。重複Xは拒否する。 */
    Result<void> SetKeys(std::vector<CurveKey> keys);
    /** \brief 既存区間の形状と接線を分割してkeyを挿入する。yを変えた分だけ新keyを移動する。 */
    Result<void> InsertKey(double x, double y);
    const std::vector<CurveKey>& Keys() const noexcept;
    /** \brief 非有限入力は既定値1とする。有限入力の範囲外は端点値を返す。 */
    double Evaluate(double x) const;
    /** \brief 区間多項式を解析積分する。逆方向は符号反転。非有限入力は0。 */
    double Integrate(double from, double to) const;
    /** \brief UIのhandleと評価に同じ接線を使う。index範囲外は0。 */
    double Tangent(size_t index, bool incoming) const;
    /** \brief 積分が単調になる区切り（key座標と零点）を返す。用途側の終端検出に使う。 */
    std::vector<double> MonotonicIntegralBreaks(double from, double to) const;
    bool operator==(const FloatCurve&) const;
private:
    std::vector<CurveKey> keys_; //< asset化した後はshared_ptr<const FloatCurve>として共有する
};
}
