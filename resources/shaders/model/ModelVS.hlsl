#define MAX_BONES 128
#include "Model.hlsli"

cbuffer Transform : register(b0)
{
    float4x4 matWVP;
};

cbuffer Skinning : register(b1)
{
    float4x4 boneMatrices[MAX_BONES];
};

ModelVSOutput main(ModelVSInput input)
{
    ModelVSOutput o;

    float4 localPos = float4(input.pos, 1.0f);

    float weightSum =
        input.boneWeight.x +
        input.boneWeight.y +
        input.boneWeight.z +
        input.boneWeight.w;

    float4 skinnedPos = localPos;

    if (weightSum > 0.0001f)
    {
        skinnedPos =
            mul(localPos, boneMatrices[input.boneIndex.x]) * input.boneWeight.x +
            mul(localPos, boneMatrices[input.boneIndex.y]) * input.boneWeight.y +
            mul(localPos, boneMatrices[input.boneIndex.z]) * input.boneWeight.z +
            mul(localPos, boneMatrices[input.boneIndex.w]) * input.boneWeight.w;
    }

    o.pos = mul(skinnedPos, matWVP);
    o.uv = input.uv;
    return o;
}