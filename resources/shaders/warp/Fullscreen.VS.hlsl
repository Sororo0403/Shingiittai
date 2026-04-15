struct VSOutput
{
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput main(uint vertexID : SV_VertexID)
{
    VSOutput o;

    float2 pos;
    float2 uv;

    if (vertexID == 0)
    {
        pos = float2(-1.0f, -1.0f);
        uv = float2(0.0f, 1.0f);
    }
    else if (vertexID == 1)
    {
        pos = float2(-1.0f, 3.0f);
        uv = float2(0.0f, -1.0f);
    }
    else
    {
        pos = float2(3.0f, -1.0f);
        uv = float2(2.0f, 1.0f);
    }

    o.svpos = float4(pos, 0.0f, 1.0f);
    o.uv = uv;
    return o;
}