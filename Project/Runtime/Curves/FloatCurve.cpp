#include "FloatCurve.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace NexusEngine {
bool CurveKey::operator==(const CurveKey&) const = default;
bool FloatCurve::operator==(const FloatCurve&) const = default;
namespace {
Error InvalidCurve(const char* reason) {
    return Error(ErrorCategory::Resource, 392, reason);
}
double Secant(const CurveKey& a, const CurveKey& b) {
    return (b.y - a.y) / (b.x - a.x);
}
std::array<double, 4> Polynomial(const FloatCurve& curve, size_t index) {
    const auto& a = curve.Keys()[index];
    const auto& b = curve.Keys()[index + 1];
    if(a.interpolation == CurveInterpolation::Constant) {
        return {a.y, 0, 0, 0};
    }
    if(a.interpolation == CurveInterpolation::Linear) {
        return {a.y, b.y - a.y, 0, 0};
    }
    // 正規化区間uで計算し、接線のdy/dxを区間幅でdy/duに変換する。
    const double width = b.x - a.x;
    const double first = width * curve.Tangent(index, false);
    const double last = width * curve.Tangent(index + 1, true);
    if(a.interpolation == CurveInterpolation::Bezier) {
        const double controlFirst = a.y + first * a.outWeight;
        const double controlLast = b.y - last * b.inWeight;
        return {a.y, 3 * (controlFirst - a.y), 3 * (a.y - 2 * controlFirst + controlLast),
            -a.y + 3 * controlFirst - 3 * controlLast + b.y};
    }
    return {a.y, first, 3 * (b.y - a.y) - 2 * first - last,
        2 * (a.y - b.y) + first + last};
}
double Primitive(const std::array<double, 4>& p, double u) {
    return u * (p[0] + u * (p[1] / 2 + u * (p[2] / 3 + u * p[3] / 4)));
}
double Value(const std::array<double, 4>& p, double u) {
    return p[0] + u * (p[1] + u * (p[2] + u * p[3]));
}
std::array<double, 4> BezierX(const CurveKey& first, const CurveKey& last) {
    const double a = first.outWeight, b = 1 - last.inWeight;
    return {0, 3 * a, 3 * (b - 2 * a), 1 + 3 * (a - b)};
}
double BezierParameter(const std::array<double, 4>& x, double position) {
    // control Xを[0,1]に制限して単調性を維持する。水平接線/垂直接線でも収束する。
    if(position <= 0 || position >= 1) {
        return std::clamp(position, 0.0, 1.0);
    }
    double left = 0, right = 1;
    for(int iteration = 0; iteration < 64; ++iteration) {
        const double middle = (left + right) / 2;
        if(Value(x, middle) < position) {
            left = middle;
        } else {
            right = middle;
        }
    }
    return (left + right) / 2;
}
double BezierPrimitive(const std::array<double, 4>& x, const std::array<double, 4>& y, double parameter) {
    // y(t) * dx/dtの5次多項式を解析積分する。速度曲線も逐次積分へ退行させない。
    std::array<double, 6> product{};
    for(size_t a = 0; a < y.size(); ++a) {
        for(size_t b = 1; b < x.size(); ++b) {
            product[a + b - 1] += y[a] * x[b] * static_cast<double>(b);
        }
    }
    double power = parameter, result = 0;
    for(size_t index = 0; index < product.size(); ++index) {
        result += product[index] * power / static_cast<double>(index + 1);
        power *= parameter;
    }
    return result;
}
}
Result<void> FloatCurve::SetKeys(std::vector<CurveKey> keys) {
    if(keys.size() > 100000) {
        return std::unexpected(InvalidCurve("Curve: too many keys"));
    }
    // 極端な座標でも係数・積分をdoubleで計算できる範囲に制限する。
    // これは用途のclampではなく、ファイル形式の数値表現範囲である。
    for(size_t index = 0; index < keys.size(); ++index) {
        const auto& key = keys[index];
        for(double value : {key.x, key.y, key.inTangent, key.outTangent}) {
            if(!std::isfinite(value) || std::abs(value) > 1e30) {
                return std::unexpected(InvalidCurve("Curve: nonfinite or unrepresentable key"));
            }
        }
        if(!std::isfinite(key.inWeight) || !std::isfinite(key.outWeight) || key.inWeight < 0 || key.inWeight > 1 ||
            key.outWeight < 0 || key.outWeight > 1) {
            return std::unexpected(InvalidCurve("Curve: tangent weights must be in [0,1]"));
        }
        if(static_cast<int>(key.interpolation) < 0 || static_cast<int>(key.interpolation) > 3 ||
            static_cast<int>(key.tangentMode) < 0 || static_cast<int>(key.tangentMode) > 4) {
            return std::unexpected(InvalidCurve("Curve: unknown interpolation or tangent mode"));
        }
        if(index > 0 && (key.x <= keys[index - 1].x || key.x - keys[index - 1].x < 1e-12)) {
            return std::unexpected(InvalidCurve("Curve: keys require increasing X separated by at least 1e-12"));
        }
    }
    keys_ = std::move(keys);
    return {};
}
const std::vector<CurveKey>& FloatCurve::Keys() const noexcept {
    return keys_;
}
Result<void> FloatCurve::InsertKey(double x, double y) {
    auto keys = keys_;
    const auto found = std::lower_bound(keys.begin(), keys.end(), x, [](const auto& key, double input) { return key.x < input; });
    const size_t index = static_cast<size_t>(found - keys.begin());
    if(!std::isfinite(x) || !std::isfinite(y) || (found != keys.end() && std::abs(found->x - x) < 1e-12) ||
        (found != keys.begin() && std::abs((found - 1)->x - x) < 1e-12)) {
        return std::unexpected(InvalidCurve("Curve: insertion requires finite values and unique X"));
    }
    for(size_t key = 0; key < keys.size(); ++key) {
        keys[key].inTangent = Tangent(key, true);
        keys[key].outTangent = Tangent(key, false);
        keys[key].tangentMode = CurveTangentMode::Broken;
    }
    CurveKey inserted{x, y};
    if(index > 0 && index < keys.size()) {
        auto& first = keys[index - 1];
        auto& last = keys[index];
        inserted.interpolation = first.interpolation;
        const double width = last.x - first.x;
        if(first.interpolation == CurveInterpolation::Bezier) {
            const double parameter = BezierParameter(BezierX(first, last), (x - first.x) / width);
            using Point = std::array<double, 2>;
            const auto mix = [parameter](Point a, Point b) {
                return Point{a[0] + (b[0] - a[0]) * parameter, a[1] + (b[1] - a[1]) * parameter};
            };
            const Point a{first.x, first.y}, b{first.x + width * first.outWeight, first.y + width * first.outWeight * first.outTangent};
            const Point c{last.x - width * last.inWeight, last.y - width * last.inWeight * last.inTangent}, d{last.x, last.y};
            const auto ab = mix(a, b), bc = mix(b, c), cd = mix(c, d);
            const auto left = mix(ab, bc), right = mix(bc, cd), point = mix(left, right);
            const auto slope = [](Point startPoint, Point endPoint, double fallback) {
                return std::abs(endPoint[0] - startPoint[0]) > 1e-30 ?
                    (endPoint[1] - startPoint[1]) / (endPoint[0] - startPoint[0]) : fallback;
            };
            first.outTangent = slope(a, ab, first.outTangent);
            last.inTangent = slope(cd, d, last.inTangent);
            inserted.inTangent = slope(left, point, 0);
            inserted.outTangent = slope(point, right, inserted.inTangent);
            first.outWeight = std::clamp((ab[0] - a[0]) / (x - first.x), 0.0, 1.0);
            inserted.inWeight = std::clamp((x - left[0]) / (x - first.x), 0.0, 1.0);
            inserted.outWeight = std::clamp((right[0] - x) / (last.x - x), 0.0, 1.0);
            last.inWeight = std::clamp((last.x - cd[0]) / (last.x - x), 0.0, 1.0);
        } else {
            const auto polynomial = Polynomial(*this, index - 1);
            const double u = (x - first.x) / width;
            inserted.inTangent = inserted.outTangent = (polynomial[1] + u * (2 * polynomial[2] + u * 3 * polynomial[3])) / width;
        }
    }
    keys.insert(keys.begin() + static_cast<std::ptrdiff_t>(index), inserted);
    return SetKeys(std::move(keys));
}
double FloatCurve::Tangent(size_t index, bool incoming) const {
    if(index >= keys_.size() || keys_.size() < 2) {
        return 0;
    }
    const auto& key = keys_[index];
    if(key.tangentMode == CurveTangentMode::Aligned) {
        return key.outTangent;
    }
    if(key.tangentMode != CurveTangentMode::Auto && key.tangentMode != CurveTangentMode::ClampedAuto) {
        return incoming ? key.inTangent : key.outTangent;
    }
    if(index == 0) {
        return Secant(key, keys_[1]);
    }
    if(index + 1 == keys_.size()) {
        return Secant(keys_[index - 1], key);
    }
    const double before = Secant(keys_[index - 1], key);
    const double after = Secant(key, keys_[index + 1]);
    if(key.tangentMode == CurveTangentMode::ClampedAuto) {
        // 符号反転する極値では水平にする。同符号では単調Hermiteの十分条件で抑える。
        if(before * after <= 0) {
            return 0;
        }
        const double average = (before + after) / 2;
        double limit = 3 * (std::min)(std::abs(before), std::abs(after));
        if(keys_[index - 1].interpolation == CurveInterpolation::Bezier && key.inWeight > 0) {
            limit = (std::min)(limit, std::abs(before) / key.inWeight);
        }
        if(key.interpolation == CurveInterpolation::Bezier && key.outWeight > 0) {
            limit = (std::min)(limit, std::abs(after) / key.outWeight);
        }
        return std::copysign((std::min)(std::abs(average), limit), average);
    }
    return (keys_[index + 1].y - keys_[index - 1].y) / (keys_[index + 1].x - keys_[index - 1].x);
}
double FloatCurve::Evaluate(double x) const {
    if(!std::isfinite(x) || keys_.empty()) {
        return 1;
    }
    if(x <= keys_.front().x) {
        return keys_.front().y;
    }
    if(x >= keys_.back().x) {
        return keys_.back().y;
    }
    const auto right = std::upper_bound(keys_.begin(), keys_.end(), x,
        [](double value, const CurveKey& key) { return value < key.x; });
    const size_t index = static_cast<size_t>(right - keys_.begin() - 1);
    const double u = (x - keys_[index].x) / (right->x - keys_[index].x);
    const auto p = Polynomial(*this, index);
    if(keys_[index].interpolation == CurveInterpolation::Bezier) {
        return Value(p, BezierParameter(BezierX(keys_[index], *right), u));
    }
    return p[0] + u * (p[1] + u * (p[2] + u * p[3]));
}
double FloatCurve::Integrate(double from, double to) const {
    if(!std::isfinite(from) || !std::isfinite(to)) {
        return 0;
    }
    if(to < from) {
        return -Integrate(to, from);
    }
    if(keys_.empty()) {
        return to - from;
    }
    double sum = 0;
    if(from < keys_.front().x) {
        const double end = (std::min)(to, keys_.front().x);
        sum += (end - from) * keys_.front().y;
    }
    // 積分を更新履歴から独立させる。可変dtでもSeekでも同じ区間を評価する。
    const auto first = std::upper_bound(keys_.begin(), keys_.end(), from, [](double x, const CurveKey& key) {
        return x < key.x;
    });
    const size_t startIndex = first == keys_.begin() ? 0 : static_cast<size_t>(first - keys_.begin() - 1);
    for(size_t index = startIndex; index + 1 < keys_.size() && keys_[index].x < to; ++index) {
        const double left = (std::max)(from, keys_[index].x);
        const double right = (std::min)(to, keys_[index + 1].x);
        if(right > left) {
            const double width = keys_[index + 1].x - keys_[index].x;
            const auto p = Polynomial(*this, index);
            if(keys_[index].interpolation == CurveInterpolation::Bezier) {
                const auto x = BezierX(keys_[index], keys_[index + 1]);
                const double begin = BezierParameter(x, (left - keys_[index].x) / width);
                const double end = BezierParameter(x, (right - keys_[index].x) / width);
                sum += width * (BezierPrimitive(x, p, end) - BezierPrimitive(x, p, begin));
            } else {
                sum += width * (Primitive(p, (right - keys_[index].x) / width) -
                    Primitive(p, (left - keys_[index].x) / width));
            }
        }
    }
    if(to > keys_.back().x) {
        sum += (to - (std::max)(from, keys_.back().x)) * keys_.back().y;
    }
    return sum;
}
std::vector<double> FloatCurve::MonotonicIntegralBreaks(double from, double to) const {
    std::vector<double> result;
    if(!std::isfinite(from) || !std::isfinite(to) || to <= from) {
        return result;
    }
    result.push_back(from);
    for(size_t index = 0; index < keys_.size(); ++index) {
        if(keys_[index].x > from && keys_[index].x < to) {
            result.push_back(keys_[index].x);
        }
        if(index + 1 >= keys_.size() || keys_[index + 1].x <= from || keys_[index].x >= to) {
            continue;
        }
        const auto p = Polynomial(*this, index);
        std::vector<double> cuts{0, 1};
        // 三次式の極値を二次方程式で求め、単調区間ごとに零点を分離する。
        const double a = 3 * p[3];
        const double b = 2 * p[2];
        const double c = p[1];
        if(a == 0) {
            if(b != 0) {
                cuts.push_back(-c / b);
            }
        } else {
            const double discriminant = b * b - 4 * a * c;
            if(discriminant >= 0) {
                const double root = std::sqrt(discriminant);
                cuts.push_back((-b - root) / (2 * a));
                cuts.push_back((-b + root) / (2 * a));
            }
        }
        std::erase_if(cuts, [](double u) { return u < 0 || u > 1 || !std::isfinite(u); });
        std::ranges::sort(cuts);
        const double width = keys_[index + 1].x - keys_[index].x;
        for(size_t cut = 1; cut < cuts.size(); ++cut) {
            double left = cuts[cut - 1];
            double right = cuts[cut];
            const double first = Value(p, left);
            const double last = Value(p, right);
            if(first * last > 0 || (first == 0 && last == 0)) {
                continue;
            }
            if(first == 0) {
                right = left;
            } else if(last != 0) {
                for(int iteration = 0; iteration < 64; ++iteration) {
                    const double middle = (left + right) / 2;
                    if((Value(p, middle) < 0) == (first < 0)) {
                        left = middle;
                    } else {
                        right = middle;
                    }
                }
            }
            const double fraction = keys_[index].interpolation == CurveInterpolation::Bezier ?
                Value(BezierX(keys_[index], keys_[index + 1]), right) : right;
            const double x = keys_[index].x + fraction * width;
            if(x > from && x < to) {
                result.push_back(x);
            }
        }
    }
    result.push_back(to);
    std::ranges::sort(result);
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
}
