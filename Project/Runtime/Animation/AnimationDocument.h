#pragma once
#include "Foundation/Error/Result.h"
#include "Graphics/Model/ModelAnimation.h"
#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace NexusEngine {
/** 時刻は秒、接線は毎秒の変化量。値の次元はtrack全体で固定する。 */
struct AnimationKey {
    float time = 0;
    std::vector<float> value{0};
    std::vector<float> inTangent, outTangent;
};
struct AnimationTrack {
    std::string target; //< 空は所有Entity、子は名前の相対階層パス
    std::string component = "Transform", property = "translation";
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    std::vector<AnimationKey> keys;
};
struct AnimationEvent {
    float time = 0;
    std::string name = "Event", payload;
};
/** v1の参照型と、v2の編集可能なキーを同じアセットとして扱う。 */
enum class AnimationClipKind { Legacy, Keyframe, Skeletal };
struct AnimationClipDocument {
    std::string source, clip;
    float duration = 1;
    bool loop = true;
    std::vector<AnimationTrack> tracks;
    std::vector<AnimationEvent> events;
    AnimationClipKind kind = AnimationClipKind::Legacy; //< v3は用途を明示。v1/v2は曖昧なまま保持
    bool durationExplicit = true; //< 旧source参照で省略された長さはrepositoryで補完する。再生状態ではない
    std::string retargetProfile; //< v4: 独立した.nxretargetの相対参照
    std::string skeletonSource; //< v4: authored keysの元rest/bindを持つモデル参照
};
/** \brief 明示kindまたはlegacyの内容から判定する。混在legacyはLegacyを返し変換しない。 */
AnimationClipKind EffectiveAnimationClipKind(const AnimationClipDocument& document);
struct AnimatorParameter {
    std::string name = "Speed", type = "Float";
    float value = 0;
};
struct AnimatorMotion {
    std::string asset;
    float x = 0, y = 0, speed = 1;
};
struct AnimatorState {
    std::string name = "Idle", asset;
    std::string blend = "Clip", parameterX = "Speed", parameterY = "Direction";
    std::vector<AnimatorMotion> motions;
    float speed = 1;
    bool loop = true;
    std::array<float, 2> position{80, 80};
};
struct AnimatorCondition {
    std::string parameter = "Speed", operation = ">";
    float value = .1f;
};
struct AnimatorTransition {
    std::string from = "Idle", to = "Walk";
    float duration = .2f, exitTime = 1, offset = 0;
    bool hasExitTime = false;
    std::vector<AnimatorCondition> conditions;
};
struct AnimatorDocument {
    std::string entry = "Idle";
    std::vector<AnimatorParameter> parameters;
    std::vector<AnimatorState> states;
    std::vector<AnimatorTransition> transitions;
};
struct TimelineClip {
    std::string asset;
    float start = 0, duration = 1, clipIn = 0, speed = 1, easeIn = 0, easeOut = 0;
    bool loop = false;
};
struct TimelineTrack {
    std::string name = "Animation", target, type = "Animation";
    bool muted = false;
    std::vector<TimelineClip> clips;
};
struct TimelineDocument {
    float duration = 5;
    bool loop = false;
    std::vector<TimelineTrack> tracks;
};

/** 読み込み、検証、原子的保存はUIとRuntimeで共用する。 */
Result<AnimationClipDocument> LoadAnimationClipDocument(const std::filesystem::path& file);
Result<AnimatorDocument> LoadAnimatorDocument(const std::filesystem::path& file);
Result<TimelineDocument> LoadTimelineDocument(const std::filesystem::path& file);
Result<void> SaveAnimationDocument(const std::filesystem::path& file, const AnimationClipDocument& document);
Result<void> SaveAnimationDocument(const std::filesystem::path& file, const AnimatorDocument& document);
Result<void> SaveAnimationDocument(const std::filesystem::path& file, const TimelineDocument& document);
Result<void> ValidateAnimationDocument(const AnimationClipDocument& document);
Result<void> ValidateAnimationDocument(const AnimatorDocument& document);
Result<void> ValidateAnimationDocument(const TimelineDocument& document);
std::vector<float> SampleAnimationTrack(const AnimationTrack& track, float time);
float AnimationClipTime(float time, float duration, bool loop);
Result<std::filesystem::path> ResolveAnimationPath(const std::filesystem::path& root, const std::string& reference);
}
