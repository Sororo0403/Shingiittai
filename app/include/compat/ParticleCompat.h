#pragma once
#include "compat/EngineCompat.h"
#include "particle/GPUParticleSystem.h"
#include <cmath>

inline DirectX::XMFLOAT3 NormalizeParticleCompatVec3(
    const DirectX::XMFLOAT3 &value,
    const DirectX::XMFLOAT3 &fallback = {1.0f, 0.0f, 0.0f}) {
    const float lengthSq =
        value.x * value.x + value.y * value.y + value.z * value.z;
    if (lengthSq < 0.0001f) {
        return fallback;
    }

    const float invLength = 1.0f / std::sqrt(lengthSq);
    return {value.x * invLength, value.y * invLength, value.z * invLength};
}

inline DirectX::XMFLOAT3 CrossParticleCompatVec3(
    const DirectX::XMFLOAT3 &a, const DirectX::XMFLOAT3 &b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

inline void ConfigureParticleCompatSlashLineBasis(
    ParticleEmitterSettings &settings, const DirectX::XMFLOAT3 &direction) {
    const DirectX::XMFLOAT3 line =
        NormalizeParticleCompatVec3(direction, {1.0f, 0.0f, 0.0f});
    const DirectX::XMFLOAT3 worldUp{0.0f, 1.0f, 0.0f};
    const float upDot =
        std::fabs(line.x * worldUp.x + line.y * worldUp.y + line.z * worldUp.z);
    const DirectX::XMFLOAT3 thicknessFallback =
        upDot > 0.88f ? DirectX::XMFLOAT3{1.0f, 0.0f, 0.0f} : worldUp;
    const DirectX::XMFLOAT3 depth =
        NormalizeParticleCompatVec3(CrossParticleCompatVec3(line,
                                                            thicknessFallback),
                                    {0.0f, 0.0f, 1.0f});
    const DirectX::XMFLOAT3 thickness =
        NormalizeParticleCompatVec3(CrossParticleCompatVec3(depth, line),
                                    thicknessFallback);

    settings.basisRight = line;
    settings.basisUp = thickness;
    settings.basisForward = depth;
}

inline void EmitParticleBurst(GPUParticleSystem &system,
                              const DirectX::XMFLOAT3 &position,
                              uint32_t count, float lifeTime,
                              AppParticleBurstStyle style,
                              const DirectX::XMFLOAT4 &color,
                              const DirectX::XMFLOAT3 &direction,
                              float speed) {
    ParticleEmitterSettings settings{};
    settings.position = position;
    settings.emissionType = ParticleEmissionType::Burst;
    settings.burstCount = count;
    settings.maxParticles = count;
    settings.baseLifeTime = lifeTime;
    settings.lifeTimeRandom = lifeTime * 0.25f;
    settings.tintColor = color;
    settings.direction = direction;
    settings.directionalVelocity = speed;
    settings.radialVelocity = speed * 0.45f;
    settings.fadeOutTime = lifeTime * 0.42f;

    switch (style) {
    case AppParticleBurstStyle::Explosion:
        settings.spawnShape = ParticleSpawnShape::Sphere;
        settings.spawnOffsetScale = {0.22f, 0.22f, 0.22f};
        settings.startScale = 0.24f;
        settings.endScale = 0.02f;
        break;
    case AppParticleBurstStyle::Sparks:
    case AppParticleBurstStyle::SpiritSparkle:
        settings.spawnShape = ParticleSpawnShape::Sphere;
        settings.spawnOffsetScale = {0.12f, 0.08f, 0.12f};
        settings.startScale = 0.08f;
        settings.endScale = 0.01f;
        settings.stretch = 1.6f;
        break;
    case AppParticleBurstStyle::Flash:
        settings.spawnShape = ParticleSpawnShape::Point;
        settings.startScale = 0.36f;
        settings.endScale = 0.04f;
        break;
    case AppParticleBurstStyle::Smoke:
        settings.spawnShape = ParticleSpawnShape::Sphere;
        settings.spawnOffsetScale = {0.20f, 0.12f, 0.20f};
        settings.startScale = 0.18f;
        settings.endScale = 0.42f;
        settings.radialVelocity = speed * 0.20f;
        break;
    case AppParticleBurstStyle::SlashLine:
        ConfigureParticleCompatSlashLineBasis(settings, direction);
        settings.spawnShape = ParticleSpawnShape::Box;
        settings.spawnOffsetScale = {lifeTime * 0.34f, lifeTime * 0.035f,
                                     lifeTime * 0.014f};
        settings.baseLifeTime = 0.18f + lifeTime * 0.08f;
        settings.lifeTimeRandom = 0.08f + lifeTime * 0.035f;
        settings.fadeOutTime = 0.10f + lifeTime * 0.03f;
        settings.startScale = 0.082f;
        settings.endScale = 0.018f;
        settings.scaleRandom = 0.014f;
        settings.stretch = 0.0f;
        settings.randomStartRotation = false;
        settings.rotationSpeed = 0.0f;
        settings.radialVelocity = speed * 0.06f;
        settings.directionalVelocity = speed * 0.12f;
        break;
    }

    system.EmitOnce(settings);
}
