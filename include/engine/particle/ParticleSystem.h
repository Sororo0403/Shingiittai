#pragma once
#include "Camera.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>
#include <vector>

class ModelManager;

class ParticleSystem {
  public:
    void Initialize(ModelManager *modelManager, uint32_t modelId);

    void Update(float deltaTime);
    void Draw(const Camera &camera);

    void Spawn(const Transform &tf, const DirectX::XMFLOAT3 &velocity,
               float life, const DirectX::XMFLOAT4 &color);

  private:
    struct Particle {
        Transform tf;
        DirectX::XMFLOAT3 velocity;
        float life = 0.0f;
        float maxLife = 1.0f;
        DirectX::XMFLOAT4 color;
        bool alive = false;
    };

  private:
    static constexpr int kMaxParticles_ = 512;

    std::vector<Particle> particles_;
    ModelManager *modelManager_ = nullptr;
    uint32_t modelId_ = 0;
};