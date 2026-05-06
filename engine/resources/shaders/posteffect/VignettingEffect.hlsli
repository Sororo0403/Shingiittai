#ifndef VIGNETTING_EFFECT_HLSLI
#define VIGNETTING_EFFECT_HLSLI

float3 ApplyVignettingEffect(float3 color, float2 uv)
{
    float2 correct = uv * (1.0f - uv.yx);
    float vignette = correct.x * correct.y * 16.0f;
    vignette = saturate(pow(vignette, 0.8f));
    return color * vignette;
}

#endif // VIGNETTING_EFFECT_HLSLI
