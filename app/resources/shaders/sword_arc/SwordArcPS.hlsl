struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PSInput input) : SV_TARGET {
    float width = abs(input.uv.x - 0.5f) * 2.0f;
    float core = 1.0f - smoothstep(0.00f, 0.10f, width);
    float body = 1.0f - smoothstep(0.08f, 0.78f, width);
    float edge = 1.0f - smoothstep(0.62f, 1.0f, width);
    float rim = smoothstep(0.34f, 0.62f, width) *
                (1.0f - smoothstep(0.78f, 1.0f, width));

    float along = input.uv.y;
    float endFade = smoothstep(0.00f, 0.10f, along) *
                    (1.0f - smoothstep(0.88f, 1.0f, along));

    float3 base = input.color.rgb;
    float3 white = float3(0.96f, 1.0f, 1.0f);
    float3 warmWhite = float3(1.0f, 0.94f, 0.82f);

    float luminance = dot(base, float3(0.299f, 0.587f, 0.114f));
    float isDarkStroke = 1.0f - smoothstep(0.10f, 0.22f, luminance);

    float3 glowColor = lerp(base, white, saturate(luminance) * 0.28f);
    float3 color = base * (0.32f + body * 0.72f);
    color += glowColor * edge * 0.60f;
    color = lerp(color, white, core * 0.74f);

    float3 darkSlash = base * (0.50f + body * 0.75f);
    darkSlash = lerp(darkSlash, float3(0.0f, 0.0f, 0.0f), core * 0.24f);
    darkSlash += warmWhite * rim * 0.82f;
    darkSlash += base * edge * 0.32f;
    color = lerp(color, darkSlash, isDarkStroke);

    float alpha = input.color.a * endFade;
    float normalAlpha = 0.24f + body * 0.50f + edge * 0.30f + core * 0.30f;
    float darkAlpha = 0.20f + body * 0.74f + rim * 0.38f;
    alpha *= lerp(normalAlpha, darkAlpha, isDarkStroke);

    clip(alpha - 0.018f);
    return float4(color, alpha);
}
