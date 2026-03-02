#include "ParticleSystem.h"
#include "ModelManager.h"
#include <cstdlib>

using namespace DirectX;

void ParticleSystem::Initialize(ModelManager *modelManager, uint32_t modelId) {
    modelManager_ = modelManager;
    modelId_ = modelId;

    particles_.resize(kMaxParticles_);
    for (auto &p : particles_) {
        p.isAlive = false;
    }
}

void ParticleSystem::Update(float dt) {
    for (auto &p : particles_) {
        if (!p.isAlive)
            continue;

        p.life += dt;
        if (p.life >= p.maxLife) {
            p.isAlive = false;
            continue;
        }

        p.tf.position.x += p.velocity.x * dt;
        p.tf.position.y += p.velocity.y * dt;
        p.tf.position.z += p.velocity.z * dt;
    }
}

void ParticleSystem::Draw(const Camera &camera) {
    for (auto &p : particles_) {
        if (!p.isAlive)
            continue;
        modelManager_->Draw(modelId_, p.tf, camera);
    }
}

void ParticleSystem::Emit(const XMFLOAT3 &position, const XMFLOAT3 &direction) {
    for (auto &p : particles_) {
        if (!p.isAlive) {
            p.isAlive = true;
            p.life = 0.0f;
            p.maxLife = 1.0f;

            p.tf.position = position;
            p.tf.scale = {0.2f, 0.2f, 0.2f};
            p.tf.rotation = {0, 0, 0, 1};

            p.velocity = {
                direction.x * 2.0f + (float(rand()) / RAND_MAX - 0.5f),
                direction.y * 2.0f + (float(rand()) / RAND_MAX - 0.5f),
                direction.z * 2.0f + (float(rand()) / RAND_MAX - 0.5f)};
            return;
        }
    }
}