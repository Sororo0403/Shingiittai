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

class Input;

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
    struct RankingEntry {
        int score = 0;
        float difficulty = 0.0f;
        float clearTime = 0.0f;
        bool isCurrent = false;
    };

    Image LoadTextureImage(const std::wstring &path);
    void RegisterClearRanking();
    int ComputeScore(float clearTime, float difficulty) const;
    void LoadRanking();
    void SaveRanking() const;
    void UpdateHandResultInput(float deltaTime);
    void UpdateReturnTitleConfirm(Input &input);
    void InitializeWorld();
    void UpdateResultCamera(float screenWidth, float screenHeight);
    void DrawWorld(float screenWidth, float screenHeight);
    void DrawResultModels();
    void DrawResultOverlay(float screenWidth, float screenHeight);
    void DrawClear(float screenWidth, float screenHeight);
    void DrawGameOver(float screenWidth, float screenHeight);
    void DrawRanking(float x, float y, float w, float h);
    void DrawControlsHint(float screenWidth, float screenHeight);
    void DrawHandInputStatus(float screenWidth, float screenHeight);
    void DrawReturnTitleConfirmWindow(float screenWidth, float screenHeight);
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
    std::string FormatScore(int score) const;

  private:
    ResultKind resultKind_ = ResultKind::GameOver;
    SwordInputCalibration inputCalibration_{};
    float combatDifficulty_ = 5.0f;
    SwordUdpController handController_;
    Camera camera_;
    float clearTime_ = 0.0f;
    int currentScore_ = 0;
    int currentRank_ = -1;
    float sceneTime_ = 0.0f;
    float handIdleTimer_ = 0.0f;
    float returnTitleFadeTimer_ = 0.0f;
    int handSwingCount_ = 0;
    bool handSwingArmed_ = true;
    bool returnTitleConfirmVisible_ = false;
    bool returnTitleFadeActive_ = false;
    int returnTitleConfirmIndex_ = 1;

    Image missionCompleteLabel_{};
    Image clearTimeLabel_{};
    Image currentRecordLabel_{};
    Image rankingTitleLabel_{};
    Image controlsKbmClearImage_{};
    Image controlsKbmGameOverImage_{};
    Image controlsHandImage_{};
    Image gameOverTitle_{};
    Image noClearTimeLabel_{};
    Image returnTitleConfirmMessageImage_{};
    Image returnTitleConfirmYesImage_{};
    Image returnTitleConfirmNoImage_{};
    std::array<Image, 10> digitImages_{};
    Image colonImage_{};
    Image dotImage_{};
    Image dashImage_{};
    Image secondImage_{};
    std::vector<RankingEntry> rankingEntries_{};

    uint32_t playerModelId_ = 0;
    uint32_t swordModelId_ = 0;
    uint32_t enemyModelId_ = 0;
};
