struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float life01 : TEXCOORD2;
    float type : TEXCOORD3;
};

float4 main(PSInput input) : SV_TARGET
{
    if (input.life01 <= 0.0)
    {
        discard;
    }

    float2 uv = input.uv * 2.0 - 1.0;

    // 細長いスパーク形状
    float radial = abs(uv.x);
    float longitudinal = abs(uv.y);

    float body = 1.0 - smoothstep(0.18, 1.0, radial);
    float tip = 1.0 - smoothstep(0.65, 1.0, longitudinal);

    float core = body * tip;

    // 中心を強く、端を細く
    float highlight = exp(-radial * 10.0) * (1.0 - smoothstep(0.0, 1.0, longitudinal));
    float edgeGlow = exp(-radial * 4.0) * (1.0 - smoothstep(0.75, 1.0, longitudinal));

    float sweepBoost = lerp(1.0, 1.18, saturate(input.type));

    float alpha = (core * 0.75 + edgeGlow * 0.45 + highlight * 0.7) *
                  input.life01 * sweepBoost;

    alpha *= (0.35 + 0.65 * input.color.a);

    if (alpha <= 0.001)
    {
        discard;
    }

    float3 color = input.color.rgb;
    color *= (0.8 + highlight * 0.9 + edgeGlow * 0.45);
    color *= lerp(0.65, 1.0, input.life01);

    return float4(color, alpha);
}