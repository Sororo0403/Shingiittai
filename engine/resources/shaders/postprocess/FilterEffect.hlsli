#ifndef FILTER_EFFECT_HLSLI
#define FILTER_EFFECT_HLSLI

#include "BoxFilterEffect.hlsli"
#include "GaussianFilterEffect.hlsli"

float4 ApplyFilterEffect(Texture2D sourceTexture, SamplerState sourceSampler,
                         float2 uv, int mode)
{
    float4 result = sourceTexture.Sample(sourceSampler, uv);

    if (mode == 1)
    {
        result = SampleBoxFilter(sourceTexture, sourceSampler, uv, texelSize, 1);
    }
    else if (mode == 2)
    {
        result = SampleBoxFilter(sourceTexture, sourceSampler, uv, texelSize, 2);
    }
    else if (mode == 3)
    {
        result = SampleGaussianBlur(sourceTexture, sourceSampler, uv, texelSize,
                                    1, 1.0f);
    }
    else if (mode == 4)
    {
        result = SampleGaussianBlur(sourceTexture, sourceSampler, uv, texelSize,
                                    3, 2.0f);
    }

    return result;
}

#endif // FILTER_EFFECT_HLSLI
