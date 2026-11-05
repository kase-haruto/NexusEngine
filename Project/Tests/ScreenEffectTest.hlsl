#include "PostProcess/ScreenEffect.hlsli"
float4 PSMain(ScreenVertex v) : SV_TARGET {
    float4 value = sourceColor.SampleLevel(linearClamp, v.uv, 0);
    return float4(value.rgb * parameters[0].x + parameters[0].y, value.a);
}
Texture2D<float4> invalidSource : register(t1);
float4 InvalidBinding(ScreenVertex v) : SV_TARGET { return invalidSource.Load(int3(v.position.xy, 0)); }
