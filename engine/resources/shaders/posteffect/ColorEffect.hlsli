#ifndef COLOR_EFFECT_HLSLI
#define COLOR_EFFECT_HLSLI

float3 ApplyGrayscale(float3 color, float3 weights)
{
    const float value = dot(color, weights);
    return value.xxx;
}

float3 ApplySepia(float3 color, float3 weights, float3 tone)
{
    const float value = dot(color, weights);
    return saturate(value.xxx * tone);
}

float3 ApplyColorEffect(float3 color, int mode, float3 weights, float3 tone)
{
    if (mode == 1)
    {
        return ApplyGrayscale(color, weights);
    }

    if (mode == 2)
    {
        return ApplySepia(color, weights, tone);
    }

    return color;
}

#endif // COLOR_EFFECT_HLSLI
