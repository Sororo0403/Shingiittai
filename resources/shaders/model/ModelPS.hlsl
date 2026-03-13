#include "Model.hlsli"

Texture2D tex0 : register(t0);
SamplerState samp0 : register(s0);

cbuffer Material : register(b2)
{
    float4 color;
    float4x4 uvTransform;
    int enableTexture;
    float3 padding;
};

float4 main(ModelVSOutput input) : SV_TARGET
{
    float2 uv = mul(float4(input.uv, 0.0f, 1.0f), uvTransform).xy;

    float4 texColor = float4(1, 1, 1, 1);
    if (enableTexture != 0)
    {
        texColor = tex0.Sample(samp0, uv);
    }

    float4 finalColor = texColor * color;
    return finalColor;
}