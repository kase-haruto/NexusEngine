#ifndef NEXUS_DISTANCE_FOG_HLSLI
#define NEXUS_DISTANCE_FOG_HLSLI
// Blend linear HDR radiance before exposure, tone mapping and sRGB encoding.
float3 ApplyDistanceFog(float3 radiance, float3 worldPosition) {
    if(fogColorEnabled.w == 0) return radiance;
    float distanceToCamera = length(worldPosition - cameraPosition.xyz);
    float amount = saturate((distanceToCamera - fogDistances.x) / (fogDistances.y - fogDistances.x));
    return lerp(radiance, fogColorEnabled.rgb, amount);
}
#endif
