#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
#include "Graphics/Sky/AtmosphereSettings.h"
NEXUS_REFLECT_COMPONENT(SkyLightComponent, "SkyLight", "component.skyLight", enabled, useAtmosphere, intensity)
namespace NexusEngine {
    struct SkyLightComponent : SkyLightSettings {};
}
