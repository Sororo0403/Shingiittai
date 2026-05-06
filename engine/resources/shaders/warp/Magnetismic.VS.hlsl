cbuffer VSConstants : register(b0)
{
    float4x4 gWorld;
    float4x4 gViewProj;
};

struct VSInput
{
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(float4(input.pos, 1.0f), gWorld);
    output.pos = mul(worldPos, gViewProj);
    output.uv = input.uv;

    return output;
}