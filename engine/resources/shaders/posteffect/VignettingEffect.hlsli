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

float3 ApplyDamageVignetteEffect(float3 color, float2 uv, float strength)
{
    float amount = saturate(strength);
    if (amount <= 0.0001f)
    {
        return color;
    }

    float2 edgeUv = abs(uv * 2.0f - 1.0f);
    float edge = smoothstep(0.50f, 1.0f, max(edgeUv.x, edgeUv.y));
    float corner = smoothstep(0.52f, 1.0f, length(edgeUv));
    float mask = saturate(edge * 0.70f + corner * 0.44f);
    mask = pow(mask, 0.72f);

    float pulse = amount * mask;
    float3 bloodTint = float3(0.92f, 0.02f, 0.015f);
    color = lerp(color, bloodTint, pulse * 0.58f);
    color += float3(0.24f, 0.0f, 0.0f) * pulse * 0.30f;
    return saturate(color);
}

float3 ApplyParryVignetteEffect(float3 color, float2 uv, float strength)
{
    float amount = saturate(strength);
    if (amount <= 0.0001f)
    {
        return color;
    }

    float2 centered = uv * 2.0f - 1.0f;
    float2 edgeUv = abs(centered);
    float edge = smoothstep(0.44f, 1.0f, max(edgeUv.x, edgeUv.y));
    float corner = smoothstep(0.46f, 1.04f, length(edgeUv));
    float diagonal = pow(saturate(1.0f - abs(centered.x + centered.y) * 0.62f),
                         2.0f);
    float mask = saturate(edge * 0.52f + corner * 0.38f + diagonal * edge * 0.34f);

    float pulse = amount * pow(mask, 0.66f);
    float3 cyan = float3(0.05f, 0.72f, 1.0f);
    float3 violet = float3(0.76f, 0.10f, 1.0f);
    float3 parryTint = lerp(cyan, violet, saturate((centered.x + 1.0f) * 0.5f));
    color = lerp(color, parryTint, pulse * 0.42f);
    color += float3(0.03f, 0.22f, 0.42f) * pulse * 0.48f;
    return saturate(color);
}

#endif // VIGNETTING_EFFECT_HLSLI
