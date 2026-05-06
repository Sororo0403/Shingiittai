#ifndef BOX_FILTER_EFFECT_HLSLI
#define BOX_FILTER_EFFECT_HLSLI

float4 SampleBoxFilter(Texture2D sourceTexture, SamplerState sourceSampler,
                       float2 uv, int radius, float weight)
{
    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);

    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            const float2 offset = float2(x, y) * texelSize;
            color += sourceTexture.Sample(sourceSampler, uv + offset) * weight;
        }
    }

    return color;
}

#endif // BOX_FILTER_EFFECT_HLSLI
