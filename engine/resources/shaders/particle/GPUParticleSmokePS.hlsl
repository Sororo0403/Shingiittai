#include "GPUParticle.hlsli"

Texture2D particleTexture : register(t1);
Texture2D noiseTexture : register(t2);
SamplerState particleSampler : register(s0);

float SoftSmokeMask(float2 p, float ageRate, float randomValue)
{
    float radius = length(p);
    float angle = atan2(p.y, p.x);
    float wobble = sin(angle * 5.0f + randomValue * 19.0f + ageRate * 2.4f) * 0.070f +
                   sin(angle * 11.0f - randomValue * 7.0f) * 0.038f;
    float body = 1.0f - smoothstep(0.38f + wobble, 0.88f + wobble, radius);
    float hollow = smoothstep(0.02f, 0.22f, radius);
    float soften = 1.0f - smoothstep(0.70f, 1.04f, radius);
    return saturate(body * hollow * soften);
}

float4 main(ParticleVSOutput input) : SV_TARGET
{
    float2 p = input.localUv * 2.0f - 1.0f;
    float ageRate = input.params.x;
    float randomValue = input.params.y;

    float4 texel = particleTexture.Sample(particleSampler, input.uv);
    float2 noiseUv = input.uv * (1.8f + randomValue * 1.4f) +
                     float2(randomValue * 0.37f, ageRate * 0.18f);
    float noise = noiseTexture.Sample(particleSampler, noiseUv).r;

    float mask = SoftSmokeMask(p, ageRate, randomValue);
    float brokenEdge = smoothstep(0.10f, 0.88f, noise + mask * 0.58f);
    float ageLift = smoothstep(0.0f, 0.18f, ageRate) *
                    (1.0f - smoothstep(0.74f, 1.0f, ageRate));

    float3 smokeBase = saturate(input.color.rgb);
    float3 warmCenter = saturate(smokeBase * 1.36f + float3(0.12f, 0.07f, 0.02f));
    float center = 1.0f - smoothstep(0.04f, 0.58f, length(p));
    float3 rgb = lerp(smokeBase * 0.82f, warmCenter, center * 0.55f);
    rgb *= lerp(0.78f, 1.18f, noise);
    rgb *= lerp(0.96f, 0.72f, ageRate);

    float alpha = input.color.a * mask * brokenEdge * ageLift;
    alpha *= lerp(0.46f, 1.0f, texel.a);
    return float4(rgb, alpha);
}
