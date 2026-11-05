#ifndef NEXUS_SCREEN_EFFECT
#define NEXUS_SCREEN_EFFECT
Texture2D<float4> sourceColor : register(t0);
SamplerState linearClamp : register(s0);
cbuffer EffectParameters : register(b0) { float4 parameters[4]; };
struct ScreenVertex { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
#endif
