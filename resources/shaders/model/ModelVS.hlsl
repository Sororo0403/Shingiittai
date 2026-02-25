#include "Model.hlsli"

cbuffer Transform : register(b0)
{
    float4x4 matWVP;
};

ModelVSOutput main(ModelVSInput input)
{
    ModelVSOutput o;
    o.pos = mul(float4(input.pos, 1.0f), matWVP);
    o.uv = input.uv;
    return o;
}
