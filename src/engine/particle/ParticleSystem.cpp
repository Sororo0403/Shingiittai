#include "ParticleSystem.h"
#include "ModelManager.h"

using namespace DirectX;

void ParticleSystem::Initialize(ModelManager *modelManager, uint32_t modelId) {
    modelManager_ = modelManager;
    modelId_ = modelId;
    particles_.resize(kMaxParticles_);
}

void ParticleSystem::Spawn(const Transform &tf, const XMFLOAT3 &velocity,
                           float life, const XMFLOAT4 &color) {

    for (auto &p : particles_) {
        if (!p.alive) {
            p.alive = true;
            p.tf = tf;
            p.velocity = velocity;
            p.life = 0.0f;
            p.maxLife = life;
            p.color = color;
            return;
        }
    }
}

void ParticleSystem::Update(float dt) {
    for (auto &p : particles_) {
        if (!p.alive)
            continue;

        p.life += dt;
        if (p.life >= p.maxLife) {
            p.alive = false;
            continue;
        }

        p.tf.position.x += p.velocity.x * dt;
        p.tf.position.y += p.velocity.y * dt;
        p.tf.position.z += p.velocity.z * dt;
    }
}

void ParticleSystem::Draw(const Camera &camera) {
    for (auto &p : particles_) {
        if (!p.alive)
            continue;
        modelManager_->Draw(modelId_, p.tf, camera);
    }
}