#include "GPUParticle.hlsli"

#define MAX_PARTICLE_BATCH_JOBS 16
#define PARTICLE_THREAD_COUNT 256
#define SPAWN_SHAPE_POINT 0u
#define SPAWN_SHAPE_SPHERE 1u
#define SPAWN_SHAPE_BOX 2u
#define SPAWN_SHAPE_RING 3u
#define SPAWN_SHAPE_DISK 4u
#define SPAWN_SHAPE_ARC 5u

cbuffer ParticleBatchParams : register(b0)
{
    uint jobCount;
    uint totalUpdateThreadCount;
    uint totalThreadCount;
    uint activeItemCapacity;
    uint updatePhase;
    uint dispatchJobIndex;
};

struct EmitterParams
{
    float4 emitterPosition;
    float4 emitterSpawnOffsetScale;
    float4 emitterSpawnShapeParams;
    float4 emitterBasisRight;
    float4 emitterBasisUp;
    float4 emitterBasisForward;
    float4 emitterDirectionAndDirectionalVelocity;
    float4 emitterVelocityBiasAndRadialVelocity;
    float4 emitterLifeAndFade;
    float4 emitterScale;
    float4 emitterAccelerationAndTurbulence;
    float4 emitterMotion;
    float4 emitterAtlasAndRotation;
    float4 emitterTintColor;
    uint4 emitterConfig;
};

struct ParticleBatchJob
{
    EmitterParams emitter;
    float4 time;
    uint particleCount;
    uint updateStart;
    uint emitStart;
    uint emitCount;
};

struct ParticleDrawItem
{
    uint systemIndex;
    uint particleIndex;
};

StructuredBuffer<ParticleBatchJob> gJobs : register(t0);
RWStructuredBuffer<Particle> gParticles[MAX_PARTICLE_BATCH_JOBS] : register(u0);
RWStructuredBuffer<uint> gFreeLists[MAX_PARTICLE_BATCH_JOBS] : register(u32);
RWStructuredBuffer<int> gFreeListIndices[MAX_PARTICLE_BATCH_JOBS] : register(u64);
RWByteAddressBuffer gActiveCount : register(u96);
RWStructuredBuffer<ParticleDrawItem> gActiveItems : register(u97);

struct RandomGenerator
{
    uint state;

    void Initialize(uint index, float particleSeed, float totalTime)
    {
        state = asuint(particleSeed) ^ (index * 747796405u) ^
                (asuint(totalTime) * 2891336453u) ^ 0x9E3779B9u;
        state = state == 0u ? 0xA341316Cu : state;
    }

    float Generate1d()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (state & 0x00FFFFFFu) / 16777216.0f;
    }
};

float3 SafeNormalize(float3 value, float3 fallback)
{
    float len = length(value);
    return len < 0.0001f ? fallback : value / len;
}

float3 MakeSphereDirection(float u0, float u1)
{
    float z = u0 * 2.0f - 1.0f;
    float angle = u1 * 6.2831853f;
    float radius = sqrt(max(0.0f, 1.0f - z * z));
    return float3(cos(angle) * radius, z, sin(angle) * radius);
}

float3 MakeSpawnOffset(uint spawnShape, float r0, float r1, float r2,
                       float4 shapeParams)
{
    if (spawnShape == SPAWN_SHAPE_POINT)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    if (spawnShape == SPAWN_SHAPE_BOX)
    {
        return float3(r0 * 2.0f - 1.0f, r1 * 2.0f - 1.0f,
                      r2 * 2.0f - 1.0f);
    }

    float angle = r0 * 6.2831853f;
    if (spawnShape == SPAWN_SHAPE_RING)
    {
        return float3(cos(angle), 0.0f, sin(angle));
    }

    if (spawnShape == SPAWN_SHAPE_DISK)
    {
        float radius = sqrt(r1);
        return float3(cos(angle) * radius, 0.0f, sin(angle) * radius);
    }

    if (spawnShape == SPAWN_SHAPE_ARC)
    {
        float arcAngle = max(0.01f, shapeParams.x);
        float halfAngle = arcAngle * 0.5f;
        float arc = lerp(-halfAngle, halfAngle, r0);
        float thickness = r1 * 2.0f - 1.0f;
        float depth = r2 * 2.0f - 1.0f;
        return float3(sin(arc), cos(arc) - cos(halfAngle), depth * 0.10f) +
               float3(0.0f, thickness * 0.08f, 0.0f);
    }

    float radius3d = pow(max(r2, 0.0001f), 0.3333333f);
    return MakeSphereDirection(r0, r1) * radius3d;
}

float3 MakeTurbulence(float seed, float age)
{
    return float3(sin(age * 11.7f + seed * 0.31f),
                  cos(age * 9.1f + seed * 0.43f),
                  sin(age * 7.4f + seed * 0.59f));
}

void AppendActiveParticle(uint systemIndex, uint index, Particle particle)
{
    if (particle.isActive == 0u || particle.color.a <= 0.0001f)
    {
        return;
    }

    uint activeIndex = 0u;
    gActiveCount.InterlockedAdd(0, 1u, activeIndex);
    if (activeIndex >= activeItemCapacity)
    {
        return;
    }

    ParticleDrawItem item;
    item.systemIndex = systemIndex;
    item.particleIndex = index;
    gActiveItems[activeIndex] = item;
}

void Respawn(uint index, inout Particle particle, ParticleBatchJob job)
{
    EmitterParams emitter = job.emitter;
    RandomGenerator generator;
    generator.Initialize(index, particle.seed + job.time.x, job.time.x);
    float r0 = generator.Generate1d();
    float r1 = generator.Generate1d();
    float r2 = generator.Generate1d();
    float r3 = generator.Generate1d();
    float r4 = generator.Generate1d();
    float r5 = generator.Generate1d();
    float r6 = generator.Generate1d();

    uint spawnShape = emitter.emitterConfig.y;
    float3 offset =
        MakeSpawnOffset(spawnShape, r0, r1, r2, emitter.emitterSpawnShapeParams);
    float3 scaledOffset = offset * emitter.emitterSpawnOffsetScale.xyz;
    float3 worldOffset = emitter.emitterBasisRight.xyz * scaledOffset.x +
                         emitter.emitterBasisUp.xyz * scaledOffset.y +
                         emitter.emitterBasisForward.xyz * scaledOffset.z;
    float3 fallbackDirection = MakeSphereDirection(r3, r4);
    float3 radialDirection = SafeNormalize(worldOffset, fallbackDirection);
    if (spawnShape == SPAWN_SHAPE_RING || spawnShape == SPAWN_SHAPE_DISK)
    {
        float3 planeOffset = emitter.emitterBasisRight.xyz * scaledOffset.x +
                             emitter.emitterBasisUp.xyz * scaledOffset.y;
        radialDirection = SafeNormalize(planeOffset, emitter.emitterBasisRight.xyz);
    }
    if (spawnShape == SPAWN_SHAPE_ARC)
    {
        radialDirection =
            SafeNormalize(emitter.emitterBasisUp.xyz * 0.35f +
                              emitter.emitterBasisForward.xyz,
                          emitter.emitterBasisForward.xyz);
    }

    float3 direction =
        SafeNormalize(emitter.emitterDirectionAndDirectionalVelocity.xyz,
                      float3(0.0f, 1.0f, 0.0f));
    float directionalVelocity = emitter.emitterDirectionAndDirectionalVelocity.w;
    float3 velocityBias = emitter.emitterVelocityBiasAndRadialVelocity.xyz;
    float radialVelocity = emitter.emitterVelocityBiasAndRadialVelocity.w;

    particle.translate = emitter.emitterPosition.xyz + worldOffset;
    particle.velocity = radialDirection * radialVelocity +
                        direction * directionalVelocity + velocityBias;
    particle.currentTime = 0.0f;
    particle.lifeTime =
        max(0.01f, emitter.emitterLifeAndFade.x + r5 * emitter.emitterLifeAndFade.y);

    float startScale = max(0.0f, emitter.emitterScale.x + r6 * emitter.emitterScale.z);
    float endScale = max(0.0f, emitter.emitterScale.y);
    float atlasFrameCount = max(1.0f, emitter.emitterAtlasAndRotation.y);
    float frameIndex =
        emitter.emitterAtlasAndRotation.x + floor(r3 * atlasFrameCount);
    particle.scale = float2(frameIndex, emitter.emitterAtlasAndRotation.z);
    particle.color = emitter.emitterTintColor;
    particle.seed += 19.19f + job.time.x + r4;
    particle.params0 =
        float4(startScale, endScale, emitter.emitterLifeAndFade.z,
               emitter.emitterLifeAndFade.w);
    float initialRoll = emitter.emitterAtlasAndRotation.w > 0.5f
                            ? 6.2831853f * r4
                            : 0.0f;
    particle.params1 = float4(max(0.01f, emitter.emitterMotion.y),
                              max(0.0f, emitter.emitterScale.w), r4,
                              initialRoll);
    particle.isActive = 1;
}

[numthreads(PARTICLE_THREAD_COUNT, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint globalIndex = dispatchThreadId.x;
    if (globalIndex >= totalThreadCount)
    {
        return;
    }

    if (updatePhase == 0u)
    {
        if (dispatchJobIndex >= jobCount)
        {
            return;
        }

        uint jobIndex = dispatchJobIndex;
        ParticleBatchJob job = gJobs[jobIndex];
        if (globalIndex >= job.particleCount)
        {
            return;
        }
        uint particleIndex = globalIndex;

        uint resourceIndex = NonUniformResourceIndex(jobIndex);
        Particle particle = gParticles[resourceIndex][particleIndex];
        if (particle.isActive != 0)
        {
            float deltaTime = job.time.y;
            particle.currentTime += deltaTime;

            if (particle.currentTime >= particle.lifeTime)
            {
                particle.isActive = 0;
                particle.color.a = 0.0f;
                gParticles[resourceIndex][particleIndex] = particle;

                int freeListIndex = 0;
                InterlockedAdd(gFreeListIndices[resourceIndex][0], 1,
                               freeListIndex);
                if (freeListIndex < (int) job.particleCount)
                {
                    gFreeLists[resourceIndex][freeListIndex] = particleIndex;
                } else
                {
                    InterlockedAdd(gFreeListIndices[resourceIndex][0], -1);
                }
            } else
            {
                EmitterParams emitter = job.emitter;
                float turbulence = emitter.emitterAccelerationAndTurbulence.w;
                float3 wander =
                    MakeTurbulence(particle.seed, particle.currentTime) * turbulence;
                float damping = pow(max(emitter.emitterMotion.x, 0.0f),
                                    deltaTime * 60.0f);
                particle.velocity +=
                    (emitter.emitterAccelerationAndTurbulence.xyz + wander) *
                    deltaTime;
                particle.velocity *= damping;
                particle.translate += particle.velocity * deltaTime;

                float alpha = emitter.emitterTintColor.a;
                float fadeInTime = particle.params0.z;
                if (fadeInTime > 0.0f)
                {
                    alpha *= saturate(particle.currentTime / fadeInTime);
                }

                float fadeOutTime = particle.params0.w;
                if (fadeOutTime > 0.0f)
                {
                    float remaining = particle.lifeTime - particle.currentTime;
                    float fade = saturate(remaining / fadeOutTime);
                    alpha *= pow(fade, particle.params1.x);
                }
                particle.color = emitter.emitterTintColor;
                particle.color.a = alpha;
                gParticles[resourceIndex][particleIndex] = particle;
                AppendActiveParticle(jobIndex, particleIndex, particle);
            }
        }
        return;
    }

    if (dispatchJobIndex >= jobCount)
    {
        return;
    }

    uint emitJobIndex = dispatchJobIndex;
    ParticleBatchJob emitJob = gJobs[emitJobIndex];
    if (globalIndex >= emitJob.emitCount)
    {
        return;
    }

    uint emitResourceIndex = NonUniformResourceIndex(emitJobIndex);
    int freeListIndex = 0;
    InterlockedAdd(gFreeListIndices[emitResourceIndex][0], -1, freeListIndex);
    if (freeListIndex <= 0)
    {
        InterlockedAdd(gFreeListIndices[emitResourceIndex][0], 1);
        return;
    }
    if (freeListIndex > (int) emitJob.particleCount)
    {
        InterlockedAdd(gFreeListIndices[emitResourceIndex][0], 1);
        return;
    }

    uint particleIndex = gFreeLists[emitResourceIndex][freeListIndex - 1];
    if (particleIndex >= emitJob.particleCount)
    {
        InterlockedAdd(gFreeListIndices[emitResourceIndex][0], 1);
        return;
    }
    Particle respawnParticle = gParticles[emitResourceIndex][particleIndex];
    Respawn(particleIndex, respawnParticle, emitJob);
    gParticles[emitResourceIndex][particleIndex] = respawnParticle;
    AppendActiveParticle(emitJobIndex, particleIndex, respawnParticle);
}
