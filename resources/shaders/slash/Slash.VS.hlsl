struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput main(uint vertexID : SV_VertexID)
{
    VSOutput output;

    float2 pos;
    if (vertexID == 0)
    {
        pos = float2(-1.0f, -1.0f);
    }
    else if (vertexID == 1)
    {
        pos = float2(-1.0f, 3.0f);
    }
    else
    {
        pos = float2(3.0f, -1.0f);
    }

    output.position = float4(pos, 0.0f, 1.0f);
    output.uv = pos * float2(0.5f, -0.5f) + 0.5f;
    return output;
}