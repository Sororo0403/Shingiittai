#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "Player.h"
#include "Sprite.h"
#include "SwordInputCalibration.h"
#include <array>
#include <cstdint>
#include <string>

/// <summary>
/// 敗北演出と再挑戦またはタイトル復帰の選択を管理する
/// </summary>
class GameOverScene : public BaseScene {
  public:
    /// <summary>
    /// GameOverSceneに対応する公開処理を実行する
    /// </summary>
    GameOverScene(float elapsedTime,
                  const SwordInputCalibration &inputCalibration = {},
                  float combatDifficulty = 5.0f);

    /// <summary>
    /// 使用するリソースと初期状態を準備する
    /// </summary>
    void Initialize(const SceneContext &ctx) override;
    /// <summary>
    /// 入力と状態を1フレーム進める
    /// </summary>
    void Update() override;
    /// <summary>
    /// 現在の状態を描画する
    /// </summary>
    void Draw() override;
    /// <summary>
    /// 透明描画パスへ必要な要素を描画する
    /// </summary>
    void DrawTransparent() override;
    /// <summary>
    /// ResetDefeatCountsが管理する状態を初期値へ戻す
    /// </summary>
    static void ResetDefeatCounts();

  private:
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };
    struct TitleFadeParticleLayout {
        float bodyX = 0.0f;
        float bodyY = 0.0f;
        float bodyWidth = 0.0f;
        float bodyHeight = 0.0f;
        float scale = 0.0f;
        float screenWidth = 0.0f;
        float screenHeight = 0.0f;
        float letterAlpha = 0.0f;
        float crumbleStart = 0.0f;
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
    void DrawOverlay(float screenWidth, float screenHeight);
    void DrawDefeatTitle(float screenWidth, float screenHeight);
    void DrawMenu(float screenWidth, float screenHeight);
    void DrawTitleFade(float screenWidth, float screenHeight);
    void DrawTitleFadeBlackOverlay(float screenWidth, float screenHeight);
    void DrawTitleFadeParticles(const Image &letter, int letterIndex,
                                const TitleFadeParticleLayout &layout);
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
    uint32_t spotlightDustTextureId_ = 0;

    Image defeatCleanImage_{};
    Image defeatImage_{};
    Image retryImage_{};
    Image titleImage_{};
    std::array<Image, 8> gameOverLetterImages_{};
};
