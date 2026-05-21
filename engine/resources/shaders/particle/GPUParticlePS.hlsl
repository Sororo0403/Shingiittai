#include "GPUParticle.hlsli"

Texture2D particleTexture : register(t1);
SamplerState particleSampler : register(s0);

float StarMask(float2 p)
{
    float angle = atan2(p.y, p.x);
    float radius = length(p);
    float points = pow(cos(angle * 5.0f) * 0.5f + 0.5f, 1.45f);
    float edge = lerp(0.28f, 0.76f, points);
    return 1.0f - smoothstep(edge - 0.030f, edge + 0.030f, radius);
}

float ScallopMask(float2 p)
{
    float radius = length(p);
    float wobble = sin(atan2(p.y, p.x) * 6.0f) * 0.055f;
    return 1.0f - smoothstep(0.58f + wobble, 0.64f + wobble, radius);
}

float PetalMask(float2 p)
{
    p.y += 0.04f;
    float left = length((p - float2(-0.16f, 0.08f)) / float2(0.48f, 0.42f));
    float right = length((p - float2(0.16f, 0.08f)) / float2(0.48f, 0.42f));
    float bottom = length((p - float2(0.0f, -0.20f)) / float2(0.38f, 0.52f));
    float mask = min(min(left, right), bottom);
    return 1.0f - smoothstep(0.94f, 1.02f, mask);
}

float BoltMask(float2 p)
{
    p.x += 0.05f;
    float d0 = abs(p.x + p.y * 0.28f) - 0.13f;
    float d1 = abs(p.x - p.y * 0.34f) - 0.16f;
    float cut = step(-0.18f, p.y) * step(p.y, 0.62f);
    float tail = step(-0.64f, p.y) * step(p.y, 0.06f);
    return saturate((1.0f - smoothstep(0.0f, 0.055f, min(d0, d1))) * max(cut, tail));
}

float SparkMask(float2 p)
{
    p.x *= 0.32f;
    float core = 1.0f - smoothstep(0.02f, 0.14f, abs(p.y));
    float tip = 1.0f - smoothstep(0.58f, 1.02f, abs(p.x));
    float hot = 1.0f - smoothstep(0.02f, 0.34f, length(p / float2(0.42f, 0.20f)));
    return saturate(max(core * tip, hot));
}

float SlashLineMask(float2 p)
{
    float taper = 1.0f - smoothstep(0.55f, 1.02f, abs(p.x));
    float core = 1.0f - smoothstep(0.012f, 0.080f, abs(p.y));
    float glow = 1.0f - smoothstep(0.05f, 0.34f, abs(p.y));
    return saturate((core * 0.85f + glow * 0.42f) * taper);
}

float FlashMask(float2 p, float ageRate)
{
    float radius = length(p);
    float cross = max(1.0f - smoothstep(0.020f, 0.15f, abs(p.y)),
                      1.0f - smoothstep(0.020f, 0.15f, abs(p.x)));
    float diagA = 1.0f - smoothstep(0.012f, 0.11f,
                                    abs(p.y - p.x * 0.55f));
    float diagB = 1.0f - smoothstep(0.012f, 0.11f,
                                    abs(p.y + p.x * 0.55f));
    float core = 1.0f - smoothstep(0.03f, 0.25f, radius);
    float fade = saturate(1.0f - ageRate);
    return saturate((max(cross, max(diagA, diagB)) *
                         (1.0f - smoothstep(0.44f, 1.0f, radius)) +
                     core) *
                    fade);
}

float ExplosionMask(float2 p, float ageRate)
{
    float radius = length(p);
    float angle = atan2(p.y, p.x);
    float noise = sin(angle * 9.0f + ageRate * 7.0f) * 0.070f +
                  sin(angle * 17.0f - ageRate * 4.0f) * 0.035f;
    float edge = 0.36f + ageRate * 0.38f + noise;
    float hotShell = 1.0f - smoothstep(edge - 0.10f, edge + 0.09f, radius);
    float core = 1.0f - smoothstep(0.02f, 0.24f + ageRate * 0.18f, radius);
    float ring = smoothstep(edge - 0.18f, edge - 0.03f, radius) *
                 (1.0f - smoothstep(edge + 0.02f, edge + 0.14f, radius));
    return saturate(max(max(hotShell * (1.0f - ageRate * 0.18f), core), ring * 0.86f));
}

float SmokeMask(float2 p, float ageRate)
{
    float radius = length(p);
    float angle = atan2(p.y, p.x);
    float noise = sin(angle * 5.0f + ageRate * 2.0f) * 0.10f +
                  sin((p.x - p.y) * 8.0f + ageRate * 5.0f) * 0.045f;
    float edge = 0.46f + ageRate * 0.30f + noise;
    float body = 1.0f - smoothstep(edge - 0.16f, edge + 0.12f, radius);
    float centerBreak = smoothstep(0.05f, 0.36f + ageRate * 0.18f, radius);
    return saturate(body * lerp(0.78f, 1.0f, centerBreak));
}

float EmberMask(float2 p, float ageRate)
{
    p.x *= 0.78f;
    float radius = length(p);
    float soft = 1.0f - smoothstep(0.18f, 0.82f, radius);
    float core = 1.0f - smoothstep(0.02f, 0.28f, radius);
    float shimmer = 0.82f + 0.18f * sin(atan2(p.y, p.x) * 4.0f + ageRate * 9.0f);
    return saturate(max(core, soft * shimmer) *
                    saturate(1.0f - smoothstep(0.18f, 1.0f, ageRate)));
}

float SpiritMask(float2 p, float ageRate)
{
    float radius = length(p);
    float core = 1.0f - smoothstep(0.03f, 0.22f, radius);
    float aura = 1.0f - smoothstep(0.12f, 0.82f, radius);
    float pulse = 0.84f + 0.16f * sin(ageRate * 8.0f + atan2(p.y, p.x) * 2.0f);
    return saturate((core + aura * 0.56f) * pulse *
                    saturate(1.0f - smoothstep(0.20f, 1.0f, ageRate)));
}

float4 main(ParticleVSOutput input) : SV_TARGET
{
    float2 p = input.uv * 2.0f - 1.0f;
    float style = input.params.x;
    float ageRate = input.params.y;
    float fillMask = style < 0.5f ? SparkMask(p)
                    : style < 1.5f ? ExplosionMask(p, ageRate)
                    : style > 2.5f && style < 3.5f ? SlashLineMask(p)
                    : style > 3.5f && style < 4.5f ? FlashMask(p, ageRate)
                    : style > 4.5f && style < 5.5f ? EmberMask(p, ageRate)
                    : style > 5.5f && style < 6.5f ? SpiritMask(p, ageRate)
                    : style > 6.5f && style < 7.5f ? SpiritMask(p * 1.18f, ageRate)
                                   : SmokeMask(p, ageRate);
    float outlineMask = style < 0.5f ? SparkMask(p * 0.84f)
                        : style < 1.5f ? ExplosionMask(p * 0.88f, ageRate)
                        : style > 2.5f && style < 3.5f ? SlashLineMask(p * 0.88f)
                        : style > 3.5f && style < 4.5f ? FlashMask(p * 0.90f, ageRate)
                        : style > 4.5f && style < 5.5f ? EmberMask(p * 0.88f, ageRate)
                        : style > 5.5f && style < 6.5f ? SpiritMask(p * 0.90f, ageRate)
                        : style > 6.5f && style < 7.5f ? SpiritMask(p, ageRate)
                                       : SmokeMask(p * 0.90f, ageRate);
    float outline = saturate(outlineMask - fillMask);
    float grain = 0.92f + 0.08f * sin((input.uv.x + input.uv.y) * 78.0f + ageRate * 31.0f);
    float metallicHotspot =
        smoothstep(0.10f, 0.95f, 1.0f - length(p - float2(-0.22f, 0.22f)));

    float3 hotCore = style < 0.5f ? float3(1.0f, 0.94f, 0.72f)
                     : style < 1.5f ? float3(1.0f, 0.46f, 0.12f)
                     : style > 2.5f && style < 3.5f ? float3(0.88f, 0.96f, 1.0f)
                     : style > 3.5f && style < 4.5f ? float3(1.0f, 0.95f, 0.64f)
                     : style > 4.5f && style < 5.5f ? float3(1.0f, 0.78f, 0.30f)
                     : style > 5.5f && style < 6.5f ? float3(0.88f, 0.96f, 1.0f)
                     : style > 6.5f && style < 7.5f ? float3(1.0f, 1.0f, 0.96f)
                                    : float3(0.10f, 0.09f, 0.08f);
    float3 fill = saturate(input.color.rgb * grain + hotCore * metallicHotspot * 0.42f);
    if (style > 1.5f && !(style > 4.5f && style < 7.5f))
    {
        fill = lerp(fill, float3(0.06f, 0.055f, 0.05f), saturate(ageRate * 0.85f));
    }
    float3 edge = style < 0.5f ? float3(1.0f, 0.98f, 0.80f)
                  : style < 1.5f ? float3(0.30f, 0.12f, 0.04f)
                  : style > 2.5f && style < 3.5f ? float3(0.64f, 0.90f, 1.0f)
                  : style > 3.5f && style < 4.5f ? float3(1.0f, 0.62f, 0.14f)
                  : style > 4.5f && style < 5.5f ? float3(1.0f, 0.38f, 0.08f)
                  : style > 5.5f && style < 6.5f ? float3(0.72f, 0.90f, 1.0f)
                  : style > 6.5f && style < 7.5f ? float3(0.82f, 0.94f, 1.0f)
                                 : float3(0.035f, 0.032f, 0.030f);
    float alpha = saturate(fillMask + outline) * input.color.a;
    float3 rgb = lerp(fill, edge, outline * (style < 0.5f ? 0.30f :
                                             style < 1.5f ? 0.76f : 0.55f));
    return float4(rgb, alpha);
}
