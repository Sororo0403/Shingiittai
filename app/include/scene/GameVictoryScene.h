#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "Enemy.h"
#include "GPUParticleSystem.h"
#include "Player.h"
#include "SwordInputCalibration.h"
#include <DirectXMath.h>
#include <cstdint>

class GameVictoryScene : public BaseScene {
  public:
    GameVictoryScene(float clearTime,
                     const SwordInputCalibration &inputCalibration = {},
                     float combatDifficulty = 5.0f);

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawTransparent() override;

  private:
    void UpdateCamera(float screenWidth, float screenHeight);
    void UpdateCinematic(float deltaTime);
    void EmitImpactBurst();
    void DrawWorld();
    void DrawStage();
    void DrawSlash();
    void DrawOverlay(float screenWidth, float screenHeight);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void Finish();

  private:
    SwordInputCalibration inputCalibration_{};
    float combatDifficulty_ = 5.0f;
    float clearTime_ = 0.0f;
    float sceneTime_ = 0.0f;
    bool impactEmitted_ = false;
    bool finishStarted_ = false;

    Camera camera_;
    Player player_;
    Enemy enemy_;
    uint32_t playerModelId_ = 0;
    uint32_t swordModelId_ = 0;
    uint32_t enemyModelId_ = 0;
    uint32_t floorModelId_ = 0;
    uint32_t slashModelId_ = 0;
    uint32_t slashTextureId_ = 0;
    uint32_t particleTextureId_ = 0;
    bool particlesReady_ = false;
    GPUParticleSystem impactParticles_;
};
