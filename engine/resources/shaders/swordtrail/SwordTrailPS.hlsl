struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PSInput input) : SV_TARGET {
    float width = abs(input.uv.x - 0.5f) * 2.0f;

    float core = 1.0f - smoothstep(0.00f, 0.095f, width);
    float innerRim = 1.0f - smoothstep(0.08f, 0.34f, width);
    float outerGlow = 1.0f - smoothstep(0.30f, 1.00f, width);
    float edgeFade = 1.0f - smoothstep(0.82f, 1.00f, width);

    float lengthFade = 1.0f - saturate(input.uv.y);
    lengthFade = pow(lengthFade, 1.05f);

    float3 blackCore = float3(0.004f, 0.000f, 0.012f);
    float3 purpleRim = float3(0.42f, 0.02f, 0.78f);
    float3 redOuter = input.color.rgb;

    float rimMix = saturate(innerRim * 0.82f + outerGlow * 0.38f);
    float3 colorRgb = lerp(redOuter, purpleRim, rimMix);
    colorRgb += redOuter * outerGlow * 0.58f;
    colorRgb = lerp(colorRgb, blackCore, core * 0.92f);

    float alpha = input.color.a * lengthFade * edgeFade;
    alpha *= lerp(0.58f, 1.0f, outerGlow);
    alpha = max(alpha, core * input.color.a * lengthFade * 0.34f);

    float4 color = float4(colorRgb, alpha);

    clip(color.a - 0.025f);
    return color;
}
