#pragma once
#include "Foundation/Reflection/ReflectionMarkers.h"
#include "Graphics/Sky/AtmosphereSettings.h"
NEXUS_REFLECT_COMPONENT(SkyAtmosphereComponent, "SkyAtmosphere", "component.skyAtmosphere", enabled, timeOfDay, sunAzimuth, sunIntensity, skyIntensity, rayleighScale, mieScale, mieAnisotropy, nightIntensity, driveSun)
NEXUS_COMPONENT_EDITOR(SkyAtmosphereComponent, "Lighting", "componentBrowser.description.atmosphere", "details.Sky")
namespace NexusEngine {
    struct SkyAtmosphereComponent : AtmosphereSettings {
        SkyAtmosphereComponent() { enabled = true; }
    };
}
