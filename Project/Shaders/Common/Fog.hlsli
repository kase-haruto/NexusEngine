#ifndef NEXUS_DISTANCE_FOG_HLSLI
#define NEXUS_DISTANCE_FOG_HLSLI
// Blend linear HDR radiance before exposure, tone mapping and sRGB encoding.
float3 ApplyDistanceFog(float3 radiance, float3 worldPosition) {
    if(fogColorEnabled.w == 0) return radiance;
    float distanceToCamera = length(worldPosition - cameraPosition.xyz);
    if(fogHeight.w != 0) {
        if(distanceToCamera <= fogDistances.x || (fogLimits.y > 0 && distanceToCamera > fogLimits.y)) return radiance;
        float rayLength = max(distanceToCamera - fogDistances.x, 0);
        float startY = lerp(cameraPosition.y, worldPosition.y, fogDistances.x / max(distanceToCamera, .00001));
        float a = clamp(-fogHeight.y * (startY - fogHeight.z), -80, 20);
        float b = clamp(-fogHeight.y * (worldPosition.y - fogHeight.z), -80, 20);
        float delta = b - a;
        float column = abs(delta) < .001 ? exp((a+b)*.5) : (exp(b)-exp(a))/delta;
        float amount = min(1-exp(-min(fogHeight.x * rayLength * column, 80)), fogLimits.x);
        return lerp(radiance, fogColorEnabled.rgb, amount);
    }
    float amount = saturate((distanceToCamera - fogDistances.x) / (fogDistances.y - fogDistances.x));
    return lerp(radiance, fogColorEnabled.rgb, amount);
}
#endif
