#include "GPUParticle.hlsli"

cbuffer ParticleUpdateParams : register(b0)
{
    float4 time;
};

cbuffer EmitterParams : register(b1)
{
    float3 emitterTranslate;
    float emitterRadius;
    uint emitterCount;
    float emitterFrequency;
    float emitterFrequencyTime;
    uint emitterEmit;
    float4 emitterTintColor;
    float4 emitterDirectionSpeed;
    uint emitterStyle;
    float3 emitterPadding;
};

RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<uint> gFreeList : register(u1);
RWStructuredBuffer<int> gFreeListIndex : register(u2);

#define PARTICLE_THREAD_COUNT 256

struct RandomGenerator
{
    uint state;

    void Initialize(uint index, float particleSeed)
    {
        state = asuint(particleSeed) ^ (index * 747796405u) ^
                (asuint(time.x) * 2891336453u) ^ 0x9E3779B9u;
        state = state == 0u ? 0xA341316Cu : state;
    }

    float Generate1d()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (state & 0x00FFFFFFu) / 16777216.0f;
    }

    float3 Generate3d()
    {
        return float3(Generate1d(), Generate1d(), Generate1d());
    }
};

void Respawn(uint index, inout Particle particle)
{
    RandomGenerator generator;
    generator.Initialize(index, particle.seed + emitterFrequencyTime);
    float r0 = generator.Generate1d();
    float r1 = generator.Generate1d();
    float r2 = generator.Generate1d();
    float r3 = generator.Generate1d();
    float r4 = generator.Generate1d();
    float r5 = generator.Generate1d();
    float r6 = generator.Generate1d();

    float angle = r0 * 6.2831853f;
    float radius = emitterRadius * sqrt(r1);
    float tangent = (r5 < 0.5f) ? -1.0f : 1.0f;
    float3 emitDir = normalize(emitterDirectionSpeed.xyz);
    if (length(emitterDirectionSpeed.xyz) < 0.0001f)
    {
        emitDir = float3(0.0f, 1.0f, 0.0f);
    }
    float3 radial = normalize(float3(cos(angle), r2 * 0.65f + 0.12f, sin(angle)));

    particle.translate = emitterTranslate +
                         float3(cos(angle) * radius, (r2 - 0.58f) * emitterRadius,
                                sin(angle) * radius * 0.24f);
    if (emitterStyle == 0u)
    {
        float3 side = normalize(float3(-emitDir.z, 0.0f, emitDir.x) +
                                radial * (r3 - 0.5f) * 0.65f);
        particle.velocity = emitDir * (1.10f + r0 * 1.90f) * emitterDirectionSpeed.w +
                            side * (1.20f + r1 * 1.80f) +
                            float3(0.0f, 0.76f + r2 * 0.72f, 0.0f);
    } else if (emitterStyle == 1u)
    {
        particle.velocity = radial * (1.15f + r0 * 1.95f) * emitterDirectionSpeed.w +
                            emitDir * (0.24f + r6 * 0.50f) +
                            float3(0.0f, 0.48f + r3 * 0.85f, 0.0f);
    } else
    {
        float3 smokeDir = normalize(radial * float3(1.0f, 0.45f, 1.0f) +
                                    float3(0.0f, 0.65f + r3 * 0.55f, 0.0f));
        particle.velocity = smokeDir * (0.42f + r0 * 0.80f) * emitterDirectionSpeed.w +
                            emitDir * (0.08f + r6 * 0.18f);
    }
    particle.currentTime = 0.0f;
    particle.lifeTime = emitterStyle == 0u ? (0.34f + r2 * 0.36f)
                        : emitterStyle == 1u ? (0.50f + r2 * 0.58f)
                                             : (1.05f + r2 * 1.10f);

    float4 palette[6] =
    {
        float4(1.00f, 0.54f, 0.36f, 1.0f),
        float4(1.00f, 0.78f, 0.25f, 1.0f),
        float4(0.37f, 0.86f, 0.72f, 1.0f),
        float4(0.34f, 0.66f, 1.00f, 1.0f),
        float4(0.94f, 0.45f, 0.80f, 1.0f),
        float4(0.75f, 0.60f, 1.00f, 1.0f),
    };
    float4 baseColor = float4(1.0f, 0.84f, 0.38f, 1.0f);
    if (emitterStyle == 0u)
    {
        baseColor = lerp(float4(1.0f, 0.82f, 0.30f, 1.0f),
                         float4(0.70f, 0.92f, 1.0f, 1.0f), r4);
    } else if (emitterStyle == 1u)
    {
        baseColor = lerp(float4(1.0f, 0.32f, 0.08f, 1.0f),
                         float4(1.0f, 0.86f, 0.28f, 1.0f), r4);
    } else
    {
        float smoke = 0.20f + r4 * 0.24f;
        baseColor = float4(smoke, smoke * 0.92f, smoke * 0.84f, 1.0f);
    }
    particle.color = float4(saturate(baseColor.rgb * emitterTintColor.rgb),
                            emitterStyle == 0u ? 1.0f :
                            emitterStyle == 1u ? 0.92f : 0.55f);

    float scale = emitterStyle == 0u ? (0.022f + r0 * 0.036f)
                  : emitterStyle == 1u ? (0.20f + r0 * 0.30f)
                                       : (0.32f + r0 * 0.48f);
    particle.scale = emitterStyle == 0u
                         ? float2(scale * (3.4f + r3 * 2.9f), scale * 0.26f)
                         : float2(scale * (0.90f + r3 * 0.36f), scale);
    particle.seed += 19.19f + time.x;
    particle.padding.x = (float) emitterStyle;
    particle.padding.y = r4;
    particle.padding.z = r5;
    particle.isActive = 1;
}

[numthreads(PARTICLE_THREAD_COUNT, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;
    uint particleCount = (uint) time.z;
    if (index >= particleCount)
    {
        return;
    }

    Particle particle = gParticles[index];

    if (particle.isActive != 0)
    {
        float deltaTime = time.y;
        particle.currentTime += deltaTime;
        float ageRate = saturate(particle.currentTime / max(particle.lifeTime, 0.001f));
        float wave = sin(time.x * 3.6f + particle.seed);
        float3 wind = float3(0.14f + wave * 0.18f,
                             sin(time.x * 2.0f + particle.seed) * 0.09f,
                             cos(time.x * 1.7f + particle.seed) * 0.04f);
        float3 gravity = float3(0.0f, -0.22f, 0.0f);
        if (particle.padding.x < 0.5f)
        {
            gravity = float3(0.0f, -1.70f, 0.0f);
            wind *= 0.25f;
        } else if (particle.padding.x > 1.5f)
        {
            gravity = float3(0.0f, 0.10f, 0.0f);
            wind = float3(0.08f + wave * 0.12f,
                          0.18f + sin(time.x * 1.3f + particle.seed) * 0.05f,
                          cos(time.x * 1.1f + particle.seed) * 0.08f);
        }

        particle.velocity += (wind + gravity) * deltaTime;
        particle.translate += particle.velocity * deltaTime;
        particle.color.a = particle.padding.x < 0.5f
                               ? saturate(1.0f - ageRate) * 1.0f
                               : particle.padding.x > 1.5f
                                     ? smoothstep(0.0f, 0.18f, ageRate) *
                                           saturate(1.0f - ageRate) * 0.48f
                                     : saturate(1.0f - ageRate) * 0.92f;

        if (particle.currentTime >= particle.lifeTime)
        {
            particle.isActive = 0;
            particle.color.a = 0.0f;
            gParticles[index] = particle;

            int freeListIndex = 0;
            InterlockedAdd(gFreeListIndex[0], 1, freeListIndex);
            if (freeListIndex < (int) particleCount)
            {
                gFreeList[freeListIndex] = index;
            } else
            {
                InterlockedAdd(gFreeListIndex[0], -1);
            }
        } else
        {
            gParticles[index] = particle;
        }
    }

    if (emitterEmit != 0 && index < emitterCount)
    {
        int freeListIndex = 0;
        InterlockedAdd(gFreeListIndex[0], -1, freeListIndex);
        if (freeListIndex <= 0)
        {
            InterlockedAdd(gFreeListIndex[0], 1);
        } else
        {
            uint particleIndex = gFreeList[freeListIndex - 1];
            Particle respawnParticle = gParticles[particleIndex];
            Respawn(particleIndex, respawnParticle);
            gParticles[particleIndex] = respawnParticle;
        }
    }
}
