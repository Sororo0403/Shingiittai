#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "SwordInputCalibration.h"
#include "SwordUdpController.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class BattleResultScene : public BaseScene {
  public:
    enum class ResultKind {
        Clear,
        GameOver,
    };

    BattleResultScene(ResultKind resultKind, float clearTime,
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

    Image LoadTextureImage(const std::wstring &path);
    void UpdateHandResultInput(float deltaTime);
    void InitializeWorld();
    void UpdateResultCamera(float screenWidth, float screenHeight);
    void DrawWorld(float screenWidth, float screenHeight);
    void DrawResultModels();
    void DrawResultOverlay(float screenWidth, float screenHeight);
    void DrawClear(float screenWidth, float screenHeight);
    void DrawGameOver(float screenWidth, float screenHeight);
    void DrawRankingPanel(float screenWidth, float screenHeight);
    void DrawHandInputStatus(float screenWidth, float screenHeight);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawFrame(float x, float y, float w, float h, float thickness,
                   const DirectX::XMFLOAT4 &color);
    void DrawTextLine(const std::string &text, float centerX, float y,
                      float scale = 1.0f, float alpha = 1.0f);
    void DrawTextLineLeft(const std::string &text, float x, float y,
                          float scale = 1.0f, float alpha = 1.0f);
    float MeasureTextLine(const std::string &text, float scale) const;
    const Image *FindCharImage(char c) const;
    std::string FormatTime(float seconds) const;
    void LoadRankings();
    void SaveRankings() const;
    void RegisterClearRanking();

  private:
    ResultKind resultKind_ = ResultKind::GameOver;
    SwordInputCalibration inputCalibration_{};
    float combatDifficulty_ = 5.0f;
    SwordUdpController handController_;
    Camera camera_;
    float clearTime_ = 0.0f;
    float sceneTime_ = 0.0f;
    float handIdleTimer_ = 0.0f;
    int handSwingCount_ = 0;
    bool handSwingArmed_ = true;

    Image clearTitle_{};
    Image gameClearTitle_{};
    Image gameOverTitle_{};
    Image clearTimeLabel_{};
    Image noClearTimeLabel_{};
    Image retryLabel_{};
    Image menuLabel_{};
    Image rankingTitle_{};
    Image currentRecordLabel_{};
    std::array<Image, 2> rankingControlLabels_{};
    std::array<Image, 10> digitImages_{};
    Image colonImage_{};
    Image dotImage_{};
    Image dashImage_{};
    Image secondImage_{};
    std::array<std::vector<float>, 2> rankings_{};

    uint32_t playerModelId_ = 0;
    uint32_t swordModelId_ = 0;
    uint32_t enemyModelId_ = 0;
};
