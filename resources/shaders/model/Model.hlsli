#ifndef MODEL_HLSLI
#define MODEL_HLSLI

struct ModelVSInput
{
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
    uint4 boneIndex : BONEINDEX;
    float4 boneWeight : BONEWEIGHT;
};

struct ModelVSOutput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

#endif // MODEL_HLSLI