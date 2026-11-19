#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
#include "Graphics/Sky/AtmosphereSettings.h"
NEXUS_REFLECT_COMPONENT(SkyLightComponent, "SkyLight", "component.skyLight", enabled, useAtmosphere, intensity)
NEXUS_COMPONENT_EDITOR(SkyLightComponent, "Lighting", "componentBrowser.description.skyLight", "details.Light")
namespace NexusEngine {
    struct SkyLightComponent : SkyLightSettings {};
}
