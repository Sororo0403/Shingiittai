#include "PostProcess.hlsli"
#include "ColorEffect.hlsli"
#include "EdgeEffect.hlsli"
#include "FilterEffect.hlsli"
#include "BloomEffect.hlsli"
#include "NoiseEffect.hlsli"
#include "TonemapEffect.hlsli"

Texture2D renderTexture : register(t0);
Texture2D depthTexture : register(t1);
SamplerState textureSampler : register(s0);

float3 ApplyNoise(float3 color, float2 uv)
{
    if (noiseEnabled == 0)
    {
        return color;
    }

    float noise = NoiseHash(uv * noiseScale + noiseTime);
    return saturate(color + (noise - 0.5f) * noiseStrength);
}

float4 ApplyRadialBlur(float2 uv, float4 baseColor)
{
    if (radialBlurStrength <= 0.0f || radialBlurSampleCount <= 1)
    {
        return baseColor;
    }

    int sampleCount = min(max(radialBlurSampleCount, 2), 32);
    float2 direction = radialBlurCenter - uv;
    float4 color = renderTexture.Sample(textureSampler, uv);

    [loop]
    for (int i = 1; i < 32; ++i)
    {
        if (i >= sampleCount)
        {
            break;
        }
        float percent = (float)i / (float)(sampleCount - 1);
        float2 sampleUv = uv + direction * radialBlurStrength * percent;
        color += renderTexture.Sample(textureSampler, sampleUv);
    }

    return color / (float)sampleCount;
}

float3 ApplyVignette(float3 color, float2 uv)
{
    float distanceFromCenter = distance(uv, float2(0.5f, 0.5f));
    float edge = smoothstep(vignetteRadius, 0.86f, distanceFromCenter);
    return color * (1.0f - edge * vignetteStrength);
}

float3 ApplyVignetting(float3 color, float2 uv)
{
    if (enableVignetting == 0)
    {
        return color;
    }

    float2 correct = uv * (1.0f - uv.yx);
    float vignette = max(correct.x * correct.y * vignettingScale, 0.0f);
    vignette = saturate(pow(vignette, vignettingPower));
    return lerp(color, color * vignette, saturate(vignetteStrength));
}

float3 ApplyScreenGrade(float3 color)
{
    float luminance = dot(color, float3(0.2125f, 0.7154f, 0.0721f));
    color = lerp(float3(luminance, luminance, luminance), color, 1.07f);
    color = (color - 0.5f) * 1.055f + 0.5f;
    color *= float3(1.015f, 1.005f, 0.985f);
    return saturate(color);
}

float Random2dTo1d(float2 value, float seed)
{
    float2 randomVector = float2(12.9898f, 78.233f);
    float random = sin(dot(value, randomVector) + seed * 37.719f);
    return frac(random * 43758.5453f);
}

float4 ApplyRandomEffect(float4 baseColor, float2 uv)
{
    if (randomMode == 0)
    {
        return baseColor;
    }

    float2 cell = floor(uv * max(randomScale, 1.0f));
    float seed = floor(randomTime * 60.0f) + randomSeed;
    float noise = Random2dTo1d(cell, seed);

    if (randomMode == 1)
    {
        return float4(noise.xxx, baseColor.a);
    }

    float grain = (noise - 0.5f) * randomStrength;
    baseColor.rgb = saturate(baseColor.rgb + grain.xxx);
    return baseColor;
}

bool IsPostProcessBypass()
{
    return colorMode == 0 &&
           filterMode == 0 &&
           edgeMode == 0 &&
           tonemapEnabled == 0 &&
           bloomEnabled == 0 &&
           noiseEnabled == 0 &&
           specialMode == 0 &&
           lensFlareEnabled == 0 &&
           enableVignetting == 0 &&
           randomMode == 0 &&
           radialBlurStrength <= 0.0f &&
           sceneDimStrength <= 0.0f;
}

float3 ApplyDissolve(float3 color, float2 uv)
{
    float noise = NoiseHash(uv * dissolveScale + noiseTime * 0.17f);
    float mask =
        smoothstep(dissolveAmount - dissolveSoftness,
                   dissolveAmount + dissolveSoftness, noise);
    float edge =
        1.0f - smoothstep(dissolveAmount, dissolveAmount + dissolveSoftness,
                          noise);
    float3 ember = float3(1.0f, 0.58f, 0.18f) * edge;
    return saturate(color * mask + ember);
}

float3 ApplySpecialEffect(float3 color, float2 uv)
{
    if (specialMode == 1)
    {
        return ApplyVignette(color, uv);
    }

    if (specialMode == 3)
    {
        return ApplyDissolve(color, uv);
    }

    return color;
}

float LensFlareDepthVisibility(Texture2D depthTex, SamplerState samplerState)
{
    if (lensFlareEnabled == 0 || lensFlareVisibility <= 0.001f)
    {
        return 0.0f;
    }

    const float2 offsets[9] =
    {
        float2(0.0f, 0.0f),
        float2(1.5f, 0.0f),
        float2(-1.5f, 0.0f),
        float2(0.0f, 1.5f),
        float2(0.0f, -1.5f),
        float2(3.5f, 2.0f),
        float2(-3.5f, 2.0f),
        float2(3.5f, -2.0f),
        float2(-3.5f, -2.0f),
    };

    float visible = 0.0f;
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        float2 sampleUv = saturate(lensFlareSunUv + offsets[i] * texelSize);
        float sampledDepth = depthTex.Sample(samplerState, sampleUv).r;
        visible += step(lensFlareSunDepth - lensFlareOcclusionBias,
                        sampledDepth);
    }

    return lensFlareVisibility * (visible / 9.0f);
}

float SoftCircle(float2 uv, float2 center, float radius, float power)
{
    float d = distance(uv, center) / max(radius, 0.0001f);
    return pow(saturate(1.0f - d), power);
}

float3 ApplyLensFlare(float3 color, float2 uv)
{
    float visibility = LensFlareDepthVisibility(depthTexture, textureSampler);
    if (visibility <= 0.001f)
    {
        return color;
    }

    float2 center = float2(0.5f, 0.5f);
    float2 sunVector = lensFlareSunUv - center;

    float glare = SoftCircle(uv, lensFlareSunUv, lensFlareGlareRadius, 2.35f);
    float3 flare = lensFlareGlareColor * glare *
                   lensFlareGlareIntensity * lensFlareGlareAlpha;

    const float ghostScales[5] = {0.35f, 0.65f, 1.05f, 1.45f, -0.18f};
    const float ghostRadii[5] = {0.055f, 0.040f, 0.090f, 0.035f, 0.048f};
    const float ghostAlpha[5] = {0.46f, 0.32f, 0.22f, 0.18f, 0.16f};
    const float ghostWarmth[5] = {1.0f, 0.72f, 0.18f, 0.82f, 0.45f};

    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        float2 ghostUv = center - sunVector * ghostScales[i];
        float ghost = SoftCircle(uv, ghostUv, ghostRadii[i], 2.0f);
        float3 ghostColor =
            lerp(lensFlareGhostCoolColor, lensFlareGhostWarmColor,
                 ghostWarmth[i]);
        flare += ghostColor * ghost * ghostAlpha[i] *
                 lensFlareGhostIntensity * lensFlareGhostAlpha;
    }

    float2 delta = uv - lensFlareSunUv;
    float horizontal = exp(-abs(delta.y) / max(lensFlareStreakWidth, 0.0001f));
    horizontal *= smoothstep(0.82f, 0.04f, abs(delta.x));
    float vertical = exp(-abs(delta.x) / max(lensFlareStreakWidth * 0.55f,
                                             0.0001f));
    vertical *= smoothstep(0.52f, 0.02f, abs(delta.y));
    flare += lensFlareStreakColor * (horizontal + vertical * 0.25f) *
             lensFlareStreakIntensity * lensFlareStreakAlpha;

    return saturate(color + flare * visibility);
}

float4 main(PostProcessVSOutput input) : SV_TARGET
{
    if (IsPostProcessBypass())
    {
        return renderTexture.Sample(textureSampler, input.uv);
    }

    float4 outputColor = ApplyFilterEffect(renderTexture, textureSampler,
                                           input.uv, filterMode);
    outputColor = ApplyRadialBlur(input.uv, outputColor);

    outputColor.rgb =
        ApplyColorEffect(outputColor.rgb, colorMode, grayscaleWeights,
                         sepiaTone);

    outputColor = ApplyEdgeEffect(outputColor, renderTexture, depthTexture,
                                  textureSampler, input.uv, edgeMode);

    outputColor.rgb = ApplyBloomEffect(renderTexture, textureSampler,
                                       outputColor.rgb, input.uv);
    outputColor.rgb = ApplyTonemapEffect(outputColor.rgb);
    outputColor.rgb = ApplyNoise(outputColor.rgb, input.uv);
    outputColor.rgb = ApplyVignetting(outputColor.rgb, input.uv);
    outputColor.rgb *= 1.0f - saturate(sceneDimStrength) * 0.62f;
    outputColor.rgb = ApplyScreenGrade(outputColor.rgb);
    outputColor.rgb = ApplySpecialEffect(outputColor.rgb, input.uv);
    outputColor.rgb = ApplyLensFlare(outputColor.rgb, input.uv);
    outputColor = ApplyRandomEffect(outputColor, input.uv);

    return outputColor;
}
