#pragma once
#include "AnimationDocument.h"
#include "Runtime/Reflection/TypeRegistry.h"
#include "Runtime/Reflection/ReflectionRegistration.h"
#include <functional>

namespace NexusEngine {
/** \brief 安定property IDの検証・数値評価・元値復元を担当する。asset I/Oやclockを所有しない。 */
class AnimationPropertyEvaluator final {
public:
    using ClipResolver = std::function<Result<const AnimationClipDocument*>(const std::string&)>;
    void Initialize(const ReflectionRegistration& reflection);
    void Restore();
    void Reset();
    Entity Target(Entity root, const std::string& path) const;
    /** \brief 不正trackは診断してskipする。strictは明示animatable metadataを要求する。 */
    Result<void> Apply(Entity entity, const std::vector<AnimationPoseSample>& samples, const ClipResolver& resolve, bool strict = false);
    /** \brief 元のvectorの他軸を維持してscalar Curveを適用する。通常trackと同じ検証・復元経路を通す。 */
    Result<void> ApplyValue(Entity entity, const std::string& target, const std::string& component,
        const std::string& property, int channel, float value);
    const std::vector<std::string>& Diagnostics() const noexcept;
    void Warn(std::string message);
private:
    struct SavedProperty {
        Entity entity; //< 世代付きhandle。component pointerを保持しない
        std::string component, property; //< 安定ID
        PropertyValue value; //< アニメーション前の値
    };
    TypeRegistry types_; //< UI非依存reflection
    bool typesReady_ = false; //< 登録はscene交換時に破棄する
    std::vector<SavedProperty> saved_; //< frame開始時に復元する値
    std::vector<std::string> diagnostics_; //< 現在frameのbinding/競合警告
};
}
