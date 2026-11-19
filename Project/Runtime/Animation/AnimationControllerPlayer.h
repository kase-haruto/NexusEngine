#pragma once
#include "AnimationDocument.h"
#include "AnimationPlayback.h"
#include <functional>
#include <memory>

namespace NexusEngine {
struct ControllerEventRange {
    std::string asset;
    double begin = 0, end = 0;
    bool loop = true;
    float weight = 1;
    uint64_t repeats = 1; //< Timeline全cycleの繰り返し。eventはframe内でまとめる
    uint32_t segment = 0;
    double timelineBegin = 0;
};
/** \brief .nxcontrollerを新Playback/Clip repositoryで実行する個体状態。Scene/Editorを所有しない。 */
class AnimationControllerPlayer final {
public:
    using ClipResolver = std::function<Result<std::shared_ptr<const AnimationClipDocument>>(const std::string&)>;
    Result<void> Update(const AnimatorDocument& document, double deltaTime, bool playing, double speed,
        const FloatCurve* curve, double curveDuration, const ClipResolver& resolve);
    void Stop();
    void Seek(double elapsed);
    Result<void> SetParameters(const std::string& json);
    void SetFloat(const std::string& name, float value);
    void SetBool(const std::string& name, bool value);
    void SetTrigger(const std::string& name);
    std::string Parameters();
    const std::vector<AnimationPoseSample>& Samples() const;
    const std::vector<ControllerEventRange>& EventRanges() const;
    const std::vector<ControllerEventRange>& MotionRanges() const;
    const std::string& State() const;
    const std::string& NextState() const;
    double Elapsed() const;
private:
    Result<std::vector<AnimationPoseSample>> Motion(const AnimatorState& state, double elapsed, double speed,
        const FloatCurve* curve, double curveDuration, const ClipResolver& resolve) const;
    std::vector<float> Weights(const AnimatorState& state) const;
    float Parameter(const std::string& name) const;
    std::vector<std::pair<std::string, float>> parameters_; //< ECS storageのnoexcept move要件を維持する
    std::string state_, next_, lastParameters_;
    double elapsed_ = 0, nextElapsed_ = 0, fadeElapsed_ = 0, fadeDuration_ = 0;
    std::vector<AnimationPoseSample> samples_, frozen_;
    std::vector<ControllerEventRange> ranges_;
    std::vector<ControllerEventRange> motionRanges_;
};
}
