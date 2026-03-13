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
        uint i0 = min(input.boneIndex.x, (uint) (MAX_BONES - 1));
        uint i1 = min(input.boneIndex.y, (uint) (MAX_BONES - 1));
        uint i2 = min(input.boneIndex.z, (uint) (MAX_BONES - 1));
        uint i3 = min(input.boneIndex.w, (uint) (MAX_BONES - 1));

        skinnedPos =
            mul(localPos, boneMatrices[i0]) * input.boneWeight.x +
            mul(localPos, boneMatrices[i1]) * input.boneWeight.y +
            mul(localPos, boneMatrices[i2]) * input.boneWeight.z +
            mul(localPos, boneMatrices[i3]) * input.boneWeight.w;
    }

    o.pos = mul(skinnedPos, matWVP);
    o.uv = input.uv;
    return o;
}