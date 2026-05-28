#pragma once
#include "BaseScene.h"
#include "BattleArenaRenderer.h"
#include "Camera.h"
#include "Enemy.h"
#include "GPUParticleSystem.h"
#include "Player.h"
#include "SwordInputCalibration.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <string>

class GameVictoryScene : public BaseScene {
  public:
    GameVictoryScene(float clearTime,
                     const SwordInputCalibration &inputCalibration = {},
                     float combatDifficulty = 5.0f);

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawTransparent() override;
    void DrawPostProcessOverlay() override;

  private:
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };

    void UpdateCamera(float screenWidth, float screenHeight);
    void UpdateCinematic(float deltaTime);
    void EmitImpactBurst();
    void DrawWorld();
    void DrawStage();
    void DrawExplodingEnemy();
    void DrawSlash();
    void DrawOverlay(float screenWidth, float screenHeight);
    void BeginResult();
    void UpdateResultPostProcess();
    void DrawResultOverlay(float screenWidth, float screenHeight);
    int ComputeScore(float clearTime, float difficulty) const;
    std::string FormatTime(float seconds) const;
    std::string FormatScore(int score) const;
    void DrawImage(const Image &image, float x, float y,
                   float scale = 1.0f, float alpha = 1.0f);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawFrame(float x, float y, float w, float h, float thickness,
                   const DirectX::XMFLOAT4 &color);
    void DrawTextLine(const std::string &text, float centerX, float y,
                      float scale = 1.0f, float alpha = 1.0f);
    void DrawTextLineLeft(const std::string &text, float x, float y,
                          float scale = 1.0f, float alpha = 1.0f);
    float GetCharAdvance(char c) const;
    float MeasureTextLine(const std::string &text, float scale) const;
    const Image *FindCharImage(char c) const;
    Image LoadTextureImage(const std::wstring &path);

  private:
    SwordInputCalibration inputCalibration_{};
    float combatDifficulty_ = 5.0f;
    float clearTime_ = 0.0f;
    int currentScore_ = 0;
    float sceneTime_ = 0.0f;
    float resultTimer_ = 0.0f;
    bool impactEmitted_ = false;
    bool resultMode_ = false;
    Transform explosionEnemyTransform_{};

    Camera camera_;
    Player player_;
    Enemy enemy_;
    uint32_t playerModelId_ = 0;
    uint32_t swordModelId_ = 0;
    uint32_t enemyModelId_ = 0;
    uint32_t enemyTextureId_ = 0;
    BattleArenaModelIds arenaModels_{};
    uint32_t slashModelId_ = 0;
    uint32_t slashTextureId_ = 0;
    uint32_t particleTextureId_ = 0;
    bool particlesReady_ = false;
    GPUParticleSystem impactParticles_;

    Image missionCompleteLabel_{};
    Image clearTimeLabel_{};
    Image scoreTitleLabel_{};
    std::array<Image, 10> digitImages_{};
    Image colonImage_{};
    Image dotImage_{};
    Image dashImage_{};
    Image secondImage_{};
};
