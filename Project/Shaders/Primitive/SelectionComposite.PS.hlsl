Texture2D<float4> selectionMask : register(t0);
float4 main(float4 position : SV_POSITION) : SV_TARGET
{
    int2 pixel = int2(position.xy);
    // 内部を塗らず、マスクの外側だけを約2.5pixel膨張する。
    if (selectionMask.Load(int3(pixel, 0)).r > 0.5) discard;
    float alpha = 0;
    [unroll] for (int y = -3; y <= 3; ++y)
        [unroll] for (int x = -3; x <= 3; ++x)
            alpha = max(alpha, selectionMask.Load(int3(pixel + int2(x, y), 0)).r * saturate(3.0 - length(float2(x, y))));
    if (alpha <= 0) discard;
    return float4(1.0, 0.22, 0.015, alpha);
}
