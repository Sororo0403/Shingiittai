#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "ParticleSystem.h"
#include "Transform.h"
#include <cstdint>

class GameScene : public BaseScene {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="ctx">シーンコンテキスト</param>
    void Initialize(const SceneContext &ctx) override;

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update() override;

    /// <summary>
    /// 描画処理
    /// </summary>
    void Draw() override;

  private:
    // =============================
    // Camera
    // =============================
    Camera camera_;

    // =============================
    // Sword
    // =============================
    uint32_t swordModelId_ = 0;
    Transform swordTf_;

    // =============================
    // Particle
    // =============================
    ParticleSystem particle_;
    uint32_t particleModelId_ = 0;

    float emitTimer_ = 0.0f;
    float emitInterval_ = 0.03f;
};