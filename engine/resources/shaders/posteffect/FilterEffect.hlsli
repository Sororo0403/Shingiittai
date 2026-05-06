#ifndef FILTER_EFFECT_HLSLI
#define FILTER_EFFECT_HLSLI

#include "BoxFilterEffect.hlsli"
#include "GaussianFilterEffect.hlsli"

float4 ApplyFilterEffect(Texture2D sourceTexture, SamplerState sourceSampler,
                         float2 uv, int mode)
{
    if (mode == 1)
    {
        return SampleBoxFilter(sourceTexture, sourceSampler, uv, 1,
                               1.0f / 9.0f);
    }

    if (mode == 2)
    {
        return SampleBoxFilter(sourceTexture, sourceSampler, uv, 2,
                               1.0f / 25.0f);
    }

    if (mode == 3)
    {
        return SampleGaussianBlur(sourceTexture, sourceSampler, uv, 1, 1.0f);
    }

    if (mode == 4)
    {
        return SampleGaussianBlur(sourceTexture, sourceSampler, uv, 3, 2.0f);
    }

    return sourceTexture.Sample(sourceSampler, uv);
}

#endif // FILTER_EFFECT_HLSLI
