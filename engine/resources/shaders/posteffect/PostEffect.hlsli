#ifndef POST_EFFECT_HLSLI
#define POST_EFFECT_HLSLI

struct PostEffectVSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

cbuffer PostEffectConstants : register(b0)
{
    int colorMode;
    int enableVignetting;
    int filterMode;
    float vignettingStrength;
    float2 texelSize;
    float vignettingScale;
    float vignettingPower;
    int edgeMode;
    float luminanceEdgeThreshold;
    float depthEdgeThreshold;
    float padding2;
    float nearZ;
    float farZ;
    float2 padding3;
    float2 radialBlurCenter;
    float radialBlurStrength;
    int radialBlurSampleCount;
    int randomMode;
    float randomStrength;
    float randomScale;
    float randomTime;
    float randomSeed;
    float3 grayscaleWeights;
    float3 sepiaTone;
    float sceneDimStrength;
};

#endif // POST_EFFECT_HLSLI
