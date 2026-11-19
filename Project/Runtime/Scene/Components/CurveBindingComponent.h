#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
#include <string>

NEXUS_REFLECT_COMPONENT(CurveBindingComponent, "CurveBinding", "component.curveBinding", curveAsset, target, targetComponent, targetProperty, channel, duration, elapsed, playbackSpeed, loop, playing, manual, input, outputScale, outputBias)
NEXUS_COMPONENT_EDITOR(CurveBindingComponent, "Animation", "componentBrowser.description.curveBinding", "details.Animation")

namespace NexusEngine {
/** \brief Curveの出力を数値propertyへ接続する。assetと独立した時計をEntityが所有する。 */
struct CurveBindingComponent {
    std::string curveAsset, target;
    std::string targetComponent = "Camera", targetProperty = "fov";
    int channel = 0;
    float duration = 1, elapsed = 0, playbackSpeed = 1;
    bool loop = true, playing = true, manual = false;
    float input = 0, outputScale = 1, outputBias = 0;
    bool IsValid() const;
    void Play();
    void Pause();
    void Stop();
    void Seek(float seconds);
};
}
