#include "HitParticleSystem.h"
#include <cstdlib>

using namespace DirectX;

void HitParticleSystem::Update(float deltaTime) {
    for (auto &p : particles_) {
        if (!p.isAlive)
            continue;

        p.life += deltaTime;

        if (p.life >= p.maxLife) {
            p.isAlive = false;
            continue;
        }

        float t = p.life / p.maxLife;

        p.tf.position.x += p.velocity.x * deltaTime;
        p.tf.position.y += p.velocity.y * deltaTime;
        p.tf.position.z += p.velocity.z * deltaTime;

        p.velocity.y -= 8.0f * deltaTime;

        float scale = 1.0f - t;
        p.tf.scale = {scale, scale, scale};

        p.color.w = 1.0f - t;
    }
}

void HitParticleSystem::Spawn(const Transform &baseTf) {
    for (int i = 0; i < kParticleCount; i++) {
        Transform tf = baseTf;

        tf.position.x += ((rand() % 100) / 100.0f - 0.5f) * 0.2f;
        tf.position.y += ((rand() % 100) / 100.0f - 0.5f) * 0.2f;
        tf.position.z += ((rand() % 100) / 100.0f - 0.5f) * 0.2f;

        DirectX::XMFLOAT3 velocity = {((rand() % 100) / 100.0f - 0.5f) * 6.0f,
                                      ((rand() % 100) / 100.0f) * 6.0f,
                                      ((rand() % 100) / 100.0f - 0.5f) * 6.0f};

        DirectX::XMFLOAT4 color = {1.0f, 1.0f, 0.3f, 1.0f};

        SpawnInternal(tf, velocity, 0.3f, color);
    }
}