#include "ScreenEffect.hlsli"
// parameters[0].x: strength [0,1]. Display-space vignette, no extra gamma conversion.
float4 PSMain(ScreenVertex v) : SV_TARGET {
    float4 color = sourceColor.SampleLevel(linearClamp, v.uv, 0);
    float2 distanceFromCenter = (v.uv - 0.5) * 2;
    color.rgb *= 1 - saturate(parameters[0].x) * saturate(dot(distanceFromCenter, distanceFromCenter) * 0.5);
    return color;
}
