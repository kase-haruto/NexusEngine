#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
#include <string>

NEXUS_REFLECT_COMPONENT(AnimatorComponent, "Animator", "component.animator", controllerAsset, playing, parameters)
namespace NexusEngine {
/** Controllerは共有資産、現在状態とパラメーターは個体ごとに所有する。 */
struct AnimatorComponent {
    std::string controllerAsset;
    bool playing = true;
    std::string parameters = "{}";
    std::string state, nextState, activeController;
    float stateTime = 0, nextTime = 0, transitionTime = 0, transitionDuration = 0;
    void SetFloat(const std::string& name, float value);
    void SetInt(const std::string& name, int value);
    void SetBool(const std::string& name, bool value);
    void SetTrigger(const std::string& name);
    void ResetTrigger(const std::string& name);
    float GetFloat(const std::string& name) const;
};
}
