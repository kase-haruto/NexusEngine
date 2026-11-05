cbuffer Grid : register(b0) {
    row_major float4x4 viewProjection;
    row_major float4x4 inverseViewProjection;
    float4 cameraSpacing;
    float4 plane; // height, fade distance
    float4 color;
};
struct Vertex {
    float4 position : SV_POSITION;
    noperspective float3 nearWorld : TEXCOORD0;
    noperspective float3 midWorld : TEXCOORD1;
};
Vertex VSMain(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    float2 clip = uv * float2(2, -2) + float2(-1, 1);
    float4 nearPoint = mul(float4(clip, 0, 1), inverseViewProjection);
    float4 midPoint = mul(float4(clip, 0.5, 1), inverseViewProjection);
    Vertex v;
    v.position = float4(clip, 0, 1);
    v.nearWorld = nearPoint.xyz / nearPoint.w;
    v.midWorld = midPoint.xyz / midPoint.w;
    return v;
}
float GridLines(float2 world, float spacing, float2 pixelWidth) {
    float2 cellDistance = abs(frac(world / spacing + 0.5) - 0.5) * spacing;
    float2 pixelDistance = cellDistance / pixelWidth;
    return 1 - smoothstep(0.4, 1.25, min(pixelDistance.x, pixelDistance.y));
}
struct Pixel { float4 color : SV_TARGET; float depth : SV_DEPTH; };
Pixel PSMain(Vertex v) {
    float3 ray = v.midWorld - v.nearWorld;
    float denominator = abs(ray.y) < 1e-7 ? (ray.y < 0 ? -1e-7 : 1e-7) : ray.y;
    float t = (plane.x - v.nearWorld.y) / denominator;
    float3 world = v.nearWorld + t * ray;
    float4 projected = mul(float4(world, 1), viewProjection);
    float depth = projected.z / projected.w;
    float2 pixelWidth = max(fwidth(world.xz), 1e-6);
    // Decimal scales cross-fade, keeping roughly 64 pixels between major cells.
    float lod = clamp(log10(max(max(pixelWidth.x, pixelWidth.y) * 64 / cameraSpacing.w, 1e-4)), -4, 8);
    float spacing = cameraSpacing.w * pow(10, floor(lod));
    float fine = GridLines(world.xz, spacing, pixelWidth);
    float coarse = GridLines(world.xz, spacing * 10, pixelWidth);
    float coverage = max(fine * (1 - frac(lod)), coarse);
    float2 axes = 1 - smoothstep(0.5, 1.5, abs(world.xz) / pixelWidth);
    float axis = max(axes.x, axes.y);
    float3 axisColor = axes.y >= axes.x ? float3(0.65, 0.055, 0.045) : float3(0.055, 0.5, 0.085);
    float3 rgb = lerp(color.rgb, axisColor, axis);
    float grazingFade = smoothstep(0.005, 0.04, abs(normalize(ray).y));
    float distanceFade = 1 - smoothstep(plane.y * 0.7, plane.y, length(world - cameraSpacing.xyz));
    if(t < 0 || projected.w <= 0 || depth < 0 || depth > 1 || abs(ray.y) < 1e-7) discard;
    Pixel output;
    output.color = float4(rgb, max(coverage * color.a, axis * 0.9) * grazingFade * distanceFade);
    output.depth = depth;
    return output;
}
