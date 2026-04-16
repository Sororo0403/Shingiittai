struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput main(uint vertexId : SV_VertexID) {
    VSOutput output;

    float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 3.0f),
        float2(3.0f, -1.0f),
    };

    float2 pos = positions[vertexId];
    output.position = float4(pos, 0.0f, 1.0f);
    output.uv = pos * float2(0.5f, -0.5f) + 0.5f;
    return output;
}
