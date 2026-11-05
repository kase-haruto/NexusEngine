#include "../Common/IndirectLighting.hlsli"

cbuffer Sky : register(b0) {
    row_major float4x4 inverseViewProjection;
    EnvironmentData environment;
    float4 skyParameters;
};
struct Vertex {
    float4 position : SV_POSITION;
    noperspective float3 direction : TEXCOORD0;
};
Vertex VSMain(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    float2 clip = uv * float2(2, -2) + float2(-1, 1);
    // A finite point avoids far-plane infinities. Translation has been removed on CPU.
    float4 midPoint = mul(float4(clip, 0.5, 1), inverseViewProjection);
    float4 nearPoint = mul(float4(clip, 0, 1), inverseViewProjection);
    Vertex v;
    v.position = float4(clip, 1, 1);
    v.direction = midPoint.xyz / midPoint.w - nearPoint.xyz / nearPoint.w;
    return v;
}
float4 PSMain(Vertex v) : SV_TARGET {
    float3 direction = normalize(v.direction);
    if(environment.options.y != 0)
        return float4(SampleEnvironment(direction, environment) * skyParameters.x, 1);
    // Scene-linear daylight, with a pale horizon and a muted lower hemisphere.
    float3 zenith = float3(0.065, 0.24, 0.62);
    float3 horizon = float3(0.65, 0.78, 0.93);
    float3 ground = float3(0.12, 0.14, 0.18);
    float3 color = direction.y >= 0
        ? lerp(horizon, zenith, pow(saturate(direction.y), 0.45))
        : lerp(horizon, ground, pow(saturate(-direction.y), 0.3));
    return float4(color * skyParameters.x, 1);
}
