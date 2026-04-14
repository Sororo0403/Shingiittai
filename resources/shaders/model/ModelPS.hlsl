#include "Model.hlsli"

Texture2D tex0 : register(t0);
SamplerState samp0 : register(s0);

cbuffer Transform : register(b0)
{
    float4x4 matWVP;
    float4x4 matWorld;
    float4 cameraPos;
    float4 effectColor;
    float4 effectParams;
};

cbuffer Material : register(b2)
{
    float4 color;
    float4x4 uvTransform;
    int enableTexture;
    float3 padding;
};

float4 main(ModelVSOutput input) : SV_TARGET
{
    float2 uv = mul(float4(input.uv, 0.0f, 1.0f), uvTransform).xy;

    float4 texColor = float4(1, 1, 1, 1);
    if (enableTexture != 0)
    {
        texColor = tex0.Sample(samp0, uv);
    }

    float4 finalColor = texColor * color;

    float effectIntensity = effectParams.x;
    if (effectIntensity > 0.0001f)
    {
        float fresnelPower = max(effectParams.y, 0.5f);
        float noiseAmount = saturate(effectParams.z);
        float time = effectParams.w;

        float3 viewDir = normalize(cameraPos.xyz - input.worldPos);
        float rim = pow(saturate(1.0f - abs(dot(normalize(input.pseudoNormal), viewDir))),
                        fresnelPower);

        float noise =
            sin(input.worldPos.x * 10.0f + time * 15.0f) *
            sin(input.worldPos.y * 12.0f - time * 11.0f) *
            sin(input.worldPos.z * 9.0f + time * 17.0f);
        noise = lerp(1.0f, 0.65f + 0.35f * noise, noiseAmount);

        float pulse = 0.8f + 0.2f * sin(time * 20.0f + input.worldPos.y * 8.0f);
        float glow = rim * noise * pulse * effectIntensity;

        float bodyFade = saturate(effectIntensity * 0.38f);
        finalColor.rgb *= lerp(1.0f, 0.24f, bodyFade);
        finalColor.rgb = lerp(finalColor.rgb, finalColor.rgb * float3(0.28f, 0.08f, 0.16f),
                              bodyFade * 0.75f);

        finalColor.rgb += effectColor.rgb * glow;
        finalColor.a = saturate(finalColor.a + effectColor.a * glow * 0.55f);
    }

    return finalColor;
}
