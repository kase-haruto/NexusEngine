#ifndef NEXUS_COLOR_SPACE_HLSLI
#define NEXUS_COLOR_SPACE_HLSLI
// IEC sRGB transfer functions. Lighting and grading use linear Rec.709 / D65.
float SrgbChannelToLinear(float value) {
    value = max(value, 0);
    return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}
float LinearChannelToSrgb(float value) {
    value = max(value, 0);
    return value <= 0.0031308 ? value * 12.92 : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}
float3 SrgbToLinear(float3 color) {
    return float3(SrgbChannelToLinear(color.r), SrgbChannelToLinear(color.g), SrgbChannelToLinear(color.b));
}
float3 LinearToSrgb(float3 color) {
    return float3(LinearChannelToSrgb(color.r), LinearChannelToSrgb(color.g), LinearChannelToSrgb(color.b));
}
#endif
