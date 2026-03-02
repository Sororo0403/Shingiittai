#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "HitParticleSystem.h"
#include "Transform.h"
#include <cstdint>

class GameScene : public BaseScene {
  public:
    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;

  private:
    Camera camera_;

    // Sword
    uint32_t swordModelId_ = 0;
    Transform swordTf_;

    // Hit Effect
    HitParticleSystem hitEffect_;
    uint32_t hitParticleModelId_ = 0;
};