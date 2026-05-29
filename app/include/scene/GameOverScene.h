#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "Player.h"
#include "Sprite.h"
#include "SwordInputCalibration.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class GameOverScene : public BaseScene {
  public:
    GameOverScene(float elapsedTime,
                  const SwordInputCalibration &inputCalibration = {},
                  float combatDifficulty = 5.0f);

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawTransparent() override;
    static void ResetDefeatCounts();

  private:
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };
    struct SpotlightDust {
        float path = 0.0f;
        float radius = 0.0f;
        float angle = 0.0f;
        float phase = 0.0f;
        float driftSpeed = 0.0f;
        float size = 0.0f;
        float alpha = 0.0f;
    };
    enum class State {
        DefeatIntro,
        Menu,
        RetryRise,
        TitleFade,
    };

    void CreateTextImages();
    void FinishDefeatIntro();
    void UpdateMenu();
    void UpdateRetryRise(float deltaTime);
    void UpdateTitleFade(float deltaTime);
    void UpdateCamera(float screenWidth, float screenHeight);
    void DrawWorld();
    void InitializeSpotlightDust();
    void DrawSpotlightDust();
    void DrawOverlay(float screenWidth, float screenHeight);
    void DrawDefeatTitle(float screenWidth, float screenHeight);
    void DrawMenu(float screenWidth, float screenHeight);
    void DrawTitleFade(float screenWidth, float screenHeight);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);
    void DrawImageTint(const Image &image, float x, float y, float scale,
                       const DirectX::XMFLOAT4 &color);
    void DrawImageSlice(const Image &image, float x, float y, float w, float h,
                        const DirectX::XMFLOAT4 &color, float uvLeft,
                        float uvTop, float uvWidth, float uvHeight);
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
    int menuIndex_ = 0;
    float retryRiseTimer_ = 0.0f;
    float retrySkipFadeTimer_ = 0.0f;
    float titleFadeTimer_ = 0.0f;
    bool retrySkipFadeActive_ = false;
    bool titleFadeSkipRequested_ = false;

    Camera camera_;
    Player player_;
    uint32_t playerModelId_ = 0;
    uint32_t swordModelId_ = 0;
    uint32_t gameOverFloorModelId_ = 0;
    uint32_t spotlightPoolModelId_ = 0;
    uint32_t spotlightDustModelId_ = 0;
    uint32_t spotlightDustTextureId_ = 0;
    std::vector<SpotlightDust> spotlightDust_;

    Image defeatCleanImage_{};
    Image defeatImage_{};
    Image retryImage_{};
    Image titleImage_{};
    std::array<Image, 8> gameOverLetterImages_{};
};
