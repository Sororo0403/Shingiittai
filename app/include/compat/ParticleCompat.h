#pragma once
#include "compat/EngineCompat.h"
#include "particle/GPUParticleSystem.h"

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
        settings.spawnShape = ParticleSpawnShape::Arc;
        settings.spawnOffsetScale = {0.34f, 0.08f, 0.34f};
        settings.startScale = 0.12f;
        settings.endScale = 0.02f;
        settings.stretch = 2.2f;
        break;
    }

    system.EmitOnce(settings);
}
