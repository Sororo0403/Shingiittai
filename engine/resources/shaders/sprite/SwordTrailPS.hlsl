struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PSInput input) : SV_TARGET {
    float width = abs(input.uv.x - 0.5f) * 2.0f;

    float core = 1.0f - smoothstep(0.10f, 0.42f, width);
    float edge = smoothstep(0.20f, 1.0f, width);

    float lengthFade = 1.0f - saturate(input.uv.y);
    lengthFade = pow(lengthFade, 0.55f);

    float4 darkCore = float4(0.015f, 0.000f, 0.030f, 1.0f);
    float4 glowEdge = input.color;

    float4 color = lerp(darkCore, glowEdge, edge);
    color.rgb += glowEdge.rgb * edge * 0.45f;

    color.a = input.color.a * lengthFade;
    color.a *= lerp(0.82f, 1.0f, edge);
    color.a *= 1.0f - core * 0.12f;

    clip(color.a - 0.015f);
    return color;
}