#ifndef RADIAL_BLUR_EFFECT_HLSLI
#define RADIAL_BLUR_EFFECT_HLSLI

float4 ApplyRadialBlurEffect(Texture2D sourceTexture, SamplerState sourceSampler,
                             float2 uv, float4 baseColor)
{
    if (radialBlurStrength <= 0.0f || radialBlurSampleCount <= 1)
    {
        return baseColor;
    }

    const int sampleCount = min(radialBlurSampleCount, 32);
    const float2 direction = radialBlurCenter - uv;
    float4 color = baseColor;

    for (int i = 1; i < sampleCount; ++i)
    {
        const float percent = (float)i / (float)(sampleCount - 1);
        const float2 sampleUv = uv + direction * radialBlurStrength * percent;
        color += sourceTexture.Sample(sourceSampler, sampleUv);
    }

    return color / (float)sampleCount;
}

#endif // RADIAL_BLUR_EFFECT_HLSLI
