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

    outputColor = ApplyEdgeEffect(outputColor, renderTexture, depthTexture,
                                  textureSampler, input.uv, edgeMode);
    outputColor = ApplyRandomEffect(outputColor, input.uv);

    return outputColor;
}
