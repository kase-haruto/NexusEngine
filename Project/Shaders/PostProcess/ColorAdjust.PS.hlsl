#include "ScreenEffect.hlsli"
float4 PSMain(ScreenVertex input) : SV_TARGET
{
    float4 color = sourceColor.SampleLevel(linearClamp, input.uv, 0);
    float brightness = max(0, parameters[0].x);
    float saturation = max(0, parameters[0].y);
    float vignette = saturate(parameters[0].z);
    float luminance = dot(color.rgb, float3(.2126, .7152, .0722));
    float3 adjusted = lerp(luminance.xxx, color.rgb, saturation) * brightness;
    float2 centered = input.uv * 2 - 1;
    float edge = saturate(dot(centered, centered) * .5);
    return float4(adjusted * (1 - vignette * edge * edge), color.a);
}
