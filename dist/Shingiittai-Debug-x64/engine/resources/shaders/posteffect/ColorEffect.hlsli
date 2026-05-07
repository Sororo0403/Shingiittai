#ifndef COLOR_EFFECT_HLSLI
#define COLOR_EFFECT_HLSLI

float3 ApplyGrayscale(float3 color)
{
    const float value = dot(color, float3(0.2125f, 0.7154f, 0.0721f));
    return value.xxx;
}

float3 ApplySepia(float3 color)
{
    const float value = dot(color, float3(0.2125f, 0.7154f, 0.0721f));
    return saturate(value.xxx * float3(1.20f, 1.00f, 0.80f));
}

float3 ApplyColorEffect(float3 color, int mode)
{
    if (mode == 1)
    {
        return ApplyGrayscale(color);
    }

    if (mode == 2)
    {
        return ApplySepia(color);
    }

    return color;
}

#endif // COLOR_EFFECT_HLSLI
