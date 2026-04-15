#include "Model.hlsli"

cbuffer Transform : register(b0)
{
    float4x4 matWVP;
    float4x4 matWorld;
    float4 cameraPos;
    float4 effectColor;
    float4 effectParams;
};

struct Well
{
    float4x4 skeletonSpaceMatrix;
    float4x4 skeletonSpaceInverseTransposeMatrix;
};

StructuredBuffer<Well> gMatrixPalette : register(t1);

ModelVSOutput main(ModelVSInput input)
{
    ModelVSOutput o;

    float4 localPos = float4(input.pos, 1.0f);

    float weightSum =
        input.weight.x +
        input.weight.y +
        input.weight.z +
        input.weight.w;

    float4 skinnedPos = localPos;

    if (weightSum > 0.0001f)
    {
        skinnedPos =
            mul(localPos, gMatrixPalette[input.index.x].skeletonSpaceMatrix) * input.weight.x +
            mul(localPos, gMatrixPalette[input.index.y].skeletonSpaceMatrix) * input.weight.y +
            mul(localPos, gMatrixPalette[input.index.z].skeletonSpaceMatrix) * input.weight.z +
            mul(localPos, gMatrixPalette[input.index.w].skeletonSpaceMatrix) * input.weight.w;
    }

    float4 worldPos = mul(skinnedPos, matWorld);
    float3 pseudoNormal = skinnedPos.xyz;
    float normalLen = length(pseudoNormal);
    if (normalLen < 0.0001f)
    {
        pseudoNormal = float3(0.0f, 1.0f, 0.0f);
    }
    else
    {
        pseudoNormal /= normalLen;
    }

    pseudoNormal = mul(pseudoNormal, (float3x3) matWorld);
    pseudoNormal = normalize(pseudoNormal);

    o.pos = mul(skinnedPos, matWVP);
    o.uv = input.uv;
    o.worldPos = worldPos.xyz;
    o.pseudoNormal = pseudoNormal;
    return o;
}
