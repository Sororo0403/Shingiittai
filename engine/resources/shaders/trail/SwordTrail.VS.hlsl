cbuffer TrailMaterial : register(b0)
{
    float4x4 gViewProj;

    float4 gInnerColor;
    float4 gOuterColor;
    float4 gDarkColor;

    float gGlobalAlpha;
    float gNoiseScale;
    float gCorePower;
    float gEdgePower;

    float gTime;
    float gPad1;
    float gPad2;
    float gPad3;
};

struct VSInput
{
    float3 position : POSITION;
    float side : TEXCOORD0;
    float t : TEXCOORD1;
    float alpha : TEXCOORD2;
    float width : TEXCOORD3;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float side : TEXCOORD0;
    float t : TEXCOORD1;
    float alpha : TEXCOORD2;
    float width : TEXCOORD3;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0f), gViewProj);
    output.side = input.side;
    output.t = input.t;
    output.alpha = input.alpha;
    output.width = input.width;
    return output;
}