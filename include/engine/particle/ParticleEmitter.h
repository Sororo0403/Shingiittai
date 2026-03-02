#pragma once
#include "ParticleParams.h"
#include "ParticleSystem.h"
#include "Transform.h"

class ParticleEmitter {
  public:
    void Initialize(ParticleSystem *system);

    void SetTransform(const Transform &tf);
    void SetParams(const ParticleParams &params);

    void Update(float deltaTime);
    void Burst(int count);

  private:
    void SpawnOne();

  private:
    ParticleSystem *system_ = nullptr;
    Transform transform_;

    ParticleParams params_;

    float timer_ = 0.0f;
};