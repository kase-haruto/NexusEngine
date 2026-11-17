#pragma once
#include "AnimationDocument.h"
#include "Runtime/Reflection/TypeRegistry.h"
#include "Runtime/Reflection/ReflectionRegistration.h"
#include <functional>
#include <memory>

namespace NexusEngine {
class Scene;
struct AnimatorComponent;
struct TimelinePlayerComponent;
/** Level所有の評価器。イベントの通知は更新時のみ行い、描画サンプルからは発火しない。 */
class AnimationRuntime final {
public:
    struct Event {
        Entity entity;
        std::string asset, name, payload;
    };
    void SetRoot(std::filesystem::path root);
    const std::filesystem::path& Root() const noexcept;
    void Invalidate();
    void SetEnabled(bool enabled);
    void ResetScene() noexcept;
    void OverrideClip(const std::string& reference, const AnimationClipDocument& clip);
    void OverrideController(const std::string& reference, const AnimatorDocument& controller);
    void OverrideTimeline(const std::string& reference, const TimelineDocument& timeline);
    Result<void> Update(Scene& scene, float deltaTime, const ReflectionRegistration& reflection);
    Result<void> Sample(Scene& scene, Entity entity, const std::vector<AnimationPoseSample>& samples,
        const ReflectionRegistration& reflection);
    const std::vector<Event>& Events() const noexcept;
    void SetEventHandler(std::function<void(const Event&)> handler);
    Result<const AnimationClipDocument*> Clip(const std::string& reference);
    Result<const AnimatorDocument*> Controller(const std::string& reference);
    Result<const TimelineDocument*> Timeline(const std::string& reference);
    Entity Target(Entity root, const std::string& path) const;
private:
    Result<void> EvaluateAnimator(Entity entity, AnimatorComponent& animator, float deltaTime);
    Result<void> EvaluateTimeline(Entity entity, TimelinePlayerComponent& timeline, float deltaTime);
    std::vector<AnimationPoseSample> StateSamples(const AnimatorState& state, float time,
        const std::map<std::string, float>& parameters);
    Result<void> Apply(Entity entity, const std::vector<AnimationPoseSample>& samples);
    void CollectEvents(Entity entity, const std::vector<AnimationPoseSample>& samples, float deltaTime);
    void RestoreProperties();
    void InitializeTypes(const ReflectionRegistration& reflection);
    struct SavedProperty {
        Entity entity;
        std::string component, property;
        PropertyValue value;
    };
    std::filesystem::path root_;
    std::map<std::string, AnimationClipDocument> clips_;
    std::map<std::string, AnimatorDocument> controllers_;
    std::map<std::string, TimelineDocument> timelines_;
    TypeRegistry types_;
    bool typesReady_ = false, enabled_ = true;
    Scene* scene_ = nullptr;
    std::vector<SavedProperty> saved_;
    std::vector<Event> events_;
    std::map<std::tuple<uint32_t, uint32_t, std::string>, float> previousTimes_;
    std::function<void(const Event&)> handler_;
};
}
