#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "Player.h"
#include "Sprite.h"
#include "SwordInputCalibration.h"
#include <cstdint>
#include <string>

class GameOverScene : public BaseScene {
  public:
    GameOverScene(float elapsedTime,
                  const SwordInputCalibration &inputCalibration = {},
                  float combatDifficulty = 5.0f);

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawTransparent() override;

  private:
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };
    enum class State {
        DefeatIntro,
        DifficultyPrompt,
        DifficultyDrop,
        Menu,
        RetryRise,
        TitleFade,
    };

    void CreateTextImages();
    void UpdateDifficultyPrompt();
    void UpdateDifficultyDrop(float deltaTime);
    void UpdateMenu();
    void UpdateRetryRise(float deltaTime);
    void UpdateTitleFade(float deltaTime);
    void UpdateCamera(float screenWidth, float screenHeight);
    void DrawWorld();
    void DrawOverlay(float screenWidth, float screenHeight);
    void DrawDefeatTitle(float screenWidth, float screenHeight);
    void DrawDifficultyPrompt(float screenWidth, float screenHeight);
    void DrawMenu(float screenWidth, float screenHeight);
    void DrawDifficultyGauge(float x, float y, float w, float h,
                             float difficulty, float form = 1.0f);
    void DrawTitleFade(float screenWidth, float screenHeight);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);
    void DrawTextureRect(uint32_t textureId, float x, float y, float w, float h,
                         const DirectX::XMFLOAT4 &color, float uvWidth,
                         SpriteBlendMode blendMode, float uvLeft = 0.0f);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawFrame(float x, float y, float w, float h, float thickness,
                   const DirectX::XMFLOAT4 &color);
    DirectX::XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) const;

  private:
    SwordInputCalibration inputCalibration_{};
    float combatDifficulty_ = 5.0f;
    float elapsedTime_ = 0.0f;
    float sceneTime_ = 0.0f;
    State state_ = State::DefeatIntro;
    float introTimer_ = 0.0f;
    int promptIndex_ = 1;
    int menuIndex_ = 0;
    float difficultyDropTimer_ = 0.0f;
    float difficultyBeforeDrop_ = 5.0f;
    float displayedDifficulty_ = 5.0f;
    float retryRiseTimer_ = 0.0f;
    float titleFadeTimer_ = 0.0f;

    Camera camera_;
    Player player_;
    uint32_t playerModelId_ = 0;
    uint32_t swordModelId_ = 0;

    Image defeatCleanImage_{};
    Image defeatImage_{};
    Image triangleMaskImage_{};
    Image triangleGradientImage_{};
    Image lowerDifficultyImage_{};
    Image yesImage_{};
    Image noImage_{};
    Image retryImage_{};
    Image titleImage_{};
    Image gameOverImage_{};
};
