#include "ParticleEmitter.h"
#include <DirectXMath.h>

using namespace DirectX;

void ParticleEmitter::Initialize(ParticleSystem *system) { system_ = system; }

void ParticleEmitter::SetTransform(const Transform &tf) { transform_ = tf; }

void ParticleEmitter::SetParams(const ParticleParams &params) {
    params_ = params;
}

void ParticleEmitter::Update(float dt) {
    if (params_.emissionRate <= 0.0f)
        return;

    timer_ += dt;

    float interval = 1.0f / params_.emissionRate;

    while (timer_ >= interval) {
        timer_ -= interval;
        SpawnOne();
    }
}

void ParticleEmitter::Burst(int count) {
    for (int i = 0; i < count; ++i) {
        SpawnOne();
    }
}

void ParticleEmitter::SpawnOne() {
    Transform tf = transform_;
    tf.scale = {params_.startScale, params_.startScale, params_.startScale};

    XMVECTOR baseDir = XMLoadFloat3(&params_.baseDirection);

    baseDir = XMVector3Normalize(baseDir);

    XMVECTOR rot = XMLoadFloat4(&transform_.rotation);
    XMVECTOR worldDir = XMVector3Rotate(baseDir, rot);

    worldDir = XMVector3Normalize(worldDir);

    XMVECTOR velVec = worldDir * params_.speed;

    XMFLOAT3 velocity;
    XMStoreFloat3(&velocity, velVec);

    system_->Spawn(tf, velocity, params_.lifeTime, params_.startColor);
}