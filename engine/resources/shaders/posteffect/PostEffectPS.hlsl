#include "PostEffect.hlsli"
#include "ColorEffect.hlsli"
#include "EdgeEffect.hlsli"
#include "FilterEffect.hlsli"
#include "RadialBlurEffect.hlsli"
#include "RandomEffect.hlsli"
#include "VignettingEffect.hlsli"

Texture2D renderTexture : register(t0);
Texture2D depthTexture : register(t1);
SamplerState textureSampler : register(s0);

float3 ApplyScreenGrade(float3 color)
{
    float luminance = dot(color, float3(0.2125f, 0.7154f, 0.0721f));
    color = lerp(float3(luminance, luminance, luminance), color, 1.07f);
    color = (color - 0.5f) * 1.055f + 0.5f;
    color *= float3(1.015f, 1.005f, 0.985f);
    return saturate(color);
}

float4 main(PostEffectVSOutput input) : SV_TARGET
{
    float4 outputColor =
        ApplyFilterEffect(renderTexture, textureSampler, input.uv, filterMode);
    outputColor = ApplyRadialBlurEffect(renderTexture, textureSampler, input.uv,
                                        outputColor);

    outputColor.rgb =
        ApplyColorEffect(outputColor.rgb, colorMode, grayscaleWeights,
                         sepiaTone);
    if (enableVignetting != 0)
    {
        outputColor.rgb =
            ApplyVignettingEffect(outputColor.rgb, input.uv,
                                  vignettingStrength, vignettingScale,
                                  vignettingPower);
    }
    outputColor.rgb *= 1.0f - saturate(sceneDimStrength) * 0.62f;
    outputColor.rgb = ApplyScreenGrade(outputColor.rgb);

    outputColor = ApplyEdgeEffect(outputColor, renderTexture, depthTexture,
                                  textureSampler, input.uv, edgeMode);
    outputColor = ApplyRandomEffect(outputColor, input.uv);

    return outputColor;
}
