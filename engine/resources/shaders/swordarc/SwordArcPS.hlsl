struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PSInput input) : SV_TARGET {
    float width = abs(input.uv.x - 0.72f) / 0.72f;
    float core = 1.0f - smoothstep(0.00f, 0.10f, width);
    float body = 1.0f - smoothstep(0.08f, 0.72f, width);
    float edge = 1.0f - smoothstep(0.60f, 1.0f, width);

    float along = input.uv.y;
    float endFade = smoothstep(0.00f, 0.10f, along) *
                    (1.0f - smoothstep(0.88f, 1.0f, along));

    float3 deepBlue = float3(0.02f, 0.08f, 0.42f);
    float3 blue = input.color.rgb;
    float3 cyan = float3(0.62f, 0.95f, 1.0f);
    float3 white = float3(0.96f, 1.0f, 1.0f);

    float3 color = lerp(deepBlue, blue, body);
    color = lerp(color, cyan, edge * 0.62f);
    color = lerp(color, white, core * 0.78f);
    color += blue * edge * 0.85f;

    float alpha = input.color.a * endFade;
    alpha *= 0.28f + body * 0.52f + edge * 0.34f + core * 0.32f;

    clip(alpha - 0.018f);
    return float4(color, alpha);
}
