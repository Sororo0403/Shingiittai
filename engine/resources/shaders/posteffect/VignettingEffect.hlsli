#ifndef VIGNETTING_EFFECT_HLSLI
#define VIGNETTING_EFFECT_HLSLI

float3 ApplyVignettingEffect(float3 color, float2 uv, float strength,
                             float scale, float power)
{
    float2 correct = uv * (1.0f - uv.yx);
    float vignette = correct.x * correct.y * scale;
    vignette = saturate(pow(vignette, power));
    return lerp(color, color * vignette, saturate(strength));
}

#endif // VIGNETTING_EFFECT_HLSLI
