#include "ScreenEffect.hlsli"
// Display-space FXAA as a graph pass. It can run at any display-colour edge.
float4 PSMain(ScreenVertex v) : SV_TARGET {
    uint w, h; sourceColor.GetDimensions(w, h);
    float2 texel = 1.0 / float2(w, h);
    float3 center = sourceColor.SampleLevel(linearClamp, v.uv, 0).rgb;
    float3 nw = sourceColor.SampleLevel(linearClamp, v.uv + float2(-1,-1) * texel, 0).rgb;
    float3 ne = sourceColor.SampleLevel(linearClamp, v.uv + float2(1,-1) * texel, 0).rgb;
    float3 sw = sourceColor.SampleLevel(linearClamp, v.uv + float2(-1,1) * texel, 0).rgb;
    float3 se = sourceColor.SampleLevel(linearClamp, v.uv + float2(1,1) * texel, 0).rgb;
    const float3 weights = float3(0.299,0.587,0.114);
    float m = dot(center,weights), a = dot(nw,weights), b = dot(ne,weights), c = dot(sw,weights), d = dot(se,weights);
    float lo = min(m,min(min(a,b),min(c,d))), hi = max(m,max(max(a,b),max(c,d)));
    if(hi-lo < max(0.0312,hi*0.125)) return float4(center,1);
    float2 direction = float2(-((a+b)-(c+d)),(a+c)-(b+d));
    float reduction = max((a+b+c+d)*(0.25*0.125),1.0/128.0);
    direction = clamp(direction/(min(abs(direction.x),abs(direction.y))+reduction),-8.0,8.0)*texel;
    float3 inner = 0.5*(sourceColor.SampleLevel(linearClamp,v.uv-direction/6.0,0).rgb + sourceColor.SampleLevel(linearClamp,v.uv+direction/6.0,0).rgb);
    float3 outer = inner*0.5 + 0.25*(sourceColor.SampleLevel(linearClamp,v.uv-direction*0.5,0).rgb + sourceColor.SampleLevel(linearClamp,v.uv+direction*0.5,0).rgb);
    float filteredLuma = dot(outer,weights);
    return float4(filteredLuma<lo || filteredLuma>hi ? inner : outer,1);
}
