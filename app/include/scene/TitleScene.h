#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "CombatFeedbackDirector.h"
#include "GameScene.h"
#include "Sprite.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>
#include <memory>
#include <string>

class Input;

class TitleScene : public BaseScene {
  public:
    ~TitleScene() override;

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawTransparent() override;

  private:
    enum class Phase {
        Movie,
        TitleLogo,
        Title,
    };

    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };

    Image LoadTitleImage(const std::wstring &path);
    void InitializeMovieScene();
    void UpdateMovieScene(float deltaTime);
    void DrawMovieScene();
    void TriggerMovieFeedback(int beatIndex, CombatFeedbackEventType type,
                              float power);
    void BeginTitleLogo();
    void CompleteTitleIntro();
    void StopTitleBgm();
    void UpdateTitleBgmVolume();
    void DrawMovieOverlay(float screenWidth, float screenHeight);
    void DrawStartupFrame(float screenWidth, float screenHeight);
    void DrawTitleBackground(float screenWidth, float screenHeight);
    void DrawTitleLogo(float screenWidth, float screenHeight, float alpha,
                       float scaleBias = 1.0f, float yOffset = 0.0f);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float alpha = 1.0f);
    void DrawImage(const Image &image, float x, float y, float alpha,
                   float scale);
    bool IsAnyButtonTriggered(const Input &input) const;
    bool IsSkipTriggered(const Input &input) const;

  private:
    Image logoImage_;
    Image pressAnyButtonImage_;
    float sceneTime_ = 0.0f;
    float movieTimer_ = 0.0f;
    float titleLogoTimer_ = 0.0f;
    float frameIntroTimer_ = 0.0f;
    float fadeTimer_ = 0.0f;
    Phase phase_ = Phase::Movie;
    bool startRequested_ = false;
    uint32_t titleBgmSoundId_ = 0;
    uint32_t titleBgmVoice_ = UINT32_MAX;
    std::unique_ptr<GameScene> titleDemoScene_;
    Camera movieCamera_;
    CombatFeedbackDirector movieFeedback_;
    int movieFeedbackBeat_ = -1;
    uint32_t movieCounterSoundId_ = UINT32_MAX;
    uint32_t movieDamageSoundId_ = UINT32_MAX;
    uint32_t movieHitSoundId_ = UINT32_MAX;
    uint32_t playerModelId_ = UINT32_MAX;
    uint32_t swordModelId_ = UINT32_MAX;
    uint32_t enemyModelId_ = UINT32_MAX;
    uint32_t movieFloorModelId_ = UINT32_MAX;
    uint32_t movieRingModelId_ = UINT32_MAX;
    uint32_t moviePillarModelId_ = UINT32_MAX;
    uint32_t movieCharacterRustTextureId_ = UINT32_MAX;
    uint32_t movieEnemyRustTextureId_ = UINT32_MAX;
    uint32_t movieArenaRustTextureId_ = UINT32_MAX;
    std::string moviePlayerAnimation_;
    std::string movieEnemyAnimation_;
    bool movieEnemyAnimationLoop_ = true;
};
