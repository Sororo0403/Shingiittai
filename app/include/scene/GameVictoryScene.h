#pragma once
#include "BaseScene.h"
#include "BattleArenaRenderer.h"
#include "Camera.h"
#include "Enemy.h"
#include "EnemyPhaseMaterial.h"
#include "InputControlType.h"
#include "Player.h"
#include "SwordInputCalibration.h"
#include <DirectXMath.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// <summary>
/// 勝利演出、クリア結果、ランキング登録を管理する
/// </summary>
class GameVictoryScene : public BaseScene {
  public:
    /// <summary>
    /// GameVictorySceneに対応する公開処理を実行する
    /// </summary>
    GameVictoryScene(float clearTime,
                     const SwordInputCalibration &inputCalibration = {},
                     float combatDifficulty = 5.0f);
    /// <summary>
    /// ~GameVictorySceneに対応する公開処理を実行する
    /// </summary>
    ~GameVictoryScene() override;

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
    /// 前景3D描画パスを使用するか判定する
    /// </summary>
    bool UsesForeground3DPass() const override;
    /// <summary>
    /// 前景3D描画パスへ必要な要素を描画する
    /// </summary>
    void DrawForeground3D() override;
    /// <summary>
    /// 透明描画パスへ必要な要素を描画する
    /// </summary>
    void DrawTransparent() override;
    /// <summary>
    /// ポストプロセス後のオーバーレイを描画する
    /// </summary>
    void DrawPostProcessOverlay() override;

  private:
    void ResetSceneState();
    void InitializePresentation();
    void LoadVictoryAssets();
    void LoadResultUi();
    void PlayOpeningSlashSounds();
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
        float inkLeft = 0.0f;
        float inkRight = 0.0f;
        float inkTop = 0.0f;
        float inkBottom = 0.0f;
    };

    struct VictoryConfetti {
        DirectX::XMFLOAT2 position{0.0f, 0.0f};
        DirectX::XMFLOAT2 velocity{0.0f, 0.0f};
        DirectX::XMFLOAT4 color{1.0f, 1.0f, 1.0f, 1.0f};
        DirectX::XMFLOAT4 highlightColor{1.0f, 1.0f, 1.0f, 1.0f};
        float width = 0.0f;
        float height = 0.0f;
        float phase = 0.0f;
        float spinSpeed = 0.0f;
        float resetDelay = 0.0f;
        float startTime = 0.0f;
        float shine = 0.0f;
    };
    struct RankingEntry {
        int score = 0;
        float difficulty = 0.0f;
        float clearTime = 0.0f;
        InputControlType controlType = InputControlType::KeyboardMouse;
        bool isCurrent = false;
    };
    struct SmokePatch {
        DirectX::XMFLOAT3 offset{};
        DirectX::XMFLOAT2 scale{};
        float roll = 0.0f;
        float delay = 0.0f;
    };
    struct RankingTableLayout {
        float contentLeft = 0.0f;
        float panelWidth = 0.0f;
        float contentWidth = 0.0f;
        float rowStartY = 0.0f;
        float rowGap = 0.0f;
        float rowFramePaddingY = 0.0f;
        float rowFrameHeight = 0.0f;
        float rowScale = 0.0f;
        float scoreRight = 0.0f;
        float timeRight = 0.0f;
        float difficultyRight = 0.0f;
    };

    void UpdateCamera(float screenWidth, float screenHeight);
    void UpdateCinematic(float deltaTime);
    void ResetVictoryConfetti(float screenWidth, float screenHeight);
    void RespawnVictoryConfetti(VictoryConfetti &confetti, float screenWidth,
                                float screenHeight, size_t index, bool initial);
    void UpdateVictoryConfetti(float deltaTime, float screenWidth,
                               float screenHeight);
    void DrawVictoryConfetti(float screenWidth, float screenHeight,
                             float alpha);
    void StartResultCrowdAudio();
    void StopResultCrowdAudio();
    void EmitPreImpactBurst();
    void EmitImpactBurst();
    void DrawWorld();
    void DrawStage();
    void DrawSlash();
    void DrawPreExplosionCharge();
    void DrawPreExplosionBillboards(const DirectX::XMFLOAT3 &center,
                                    float yaw, float alpha, float pulse,
                                    float finalSurge, float burst);
    void DrawExplosionFlash();
    void DrawExplosionCore();
    void DrawExplosionBillboards();
    void DrawExplosionSmokePatch(const SmokePatch &patch, uint32_t modelId,
                                 const DirectX::XMFLOAT3 &center, float yaw,
                                 float age, float alpha);
    void DrawForegroundEnemy();
    void DrawOverlay(float screenWidth, float screenHeight);
    void BeginResult();
    bool UpdateResultMode(float deltaTime);
    void UpdateResultPostProcess();
    void UpdateResultInput();
    void DrawResultOverlay(float screenWidth, float screenHeight);
    void RegisterRanking();
    void LoadRanking();
    void SaveRanking() const;
    void DrawRankingPanel(float screenWidth, float screenHeight, float alpha);
    void DrawRankingRows(const RankingTableLayout &layout, float alpha);
    void DrawActionButtons(float screenWidth, float screenHeight, float alpha);
    int ComputeScore(float clearTime, float difficulty) const;
    std::string FormatTime(float seconds) const;
    std::string FormatDifficulty(float difficulty) const;
    std::string FormatScore(int score) const;
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);
    void DrawImage(const Image &image, float x, float y, float scale,
                   const DirectX::XMFLOAT4 &color);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawFrame(float x, float y, float w, float h, float thickness,
                   const DirectX::XMFLOAT4 &color);
    void DrawTextLine(const std::string &text, float centerX, float y,
                      float scale = 1.0f, float alpha = 1.0f);
    void DrawTextLine(const std::string &text, float centerX, float y,
                      float scale, const DirectX::XMFLOAT4 &color);
    void DrawTextLineLeft(const std::string &text, float x, float y,
                          float scale = 1.0f, float alpha = 1.0f);
    void DrawTextLineLeft(const std::string &text, float x, float y,
                          float scale, const DirectX::XMFLOAT4 &color);
    void DrawTextLineLeftBaseline(const std::string &text, float x,
                                  float baselineY, float scale,
                                  const DirectX::XMFLOAT4 &color);
    float GetCharAdvance(char c) const;
    float MeasureTextLine(const std::string &text, float scale) const;
    float MeasureTextInkCenterOffset(const std::string &text,
                                     float scale) const;
    const Image *FindCharImage(char c) const;
    Image LoadTextureImage(const std::wstring &path);

  private:
    SwordInputCalibration inputCalibration_{};
    float combatDifficulty_ = 5.0f;
    float clearTime_ = 0.0f;
    int currentScore_ = 0;
    float sceneTime_ = 0.0f;
    float realSceneTime_ = 0.0f;
    float resultTimer_ = 0.0f;
    float exitTimer_ = 0.0f;
    bool preImpactEmitted_ = false;
    bool impactEmitted_ = false;
    bool resultMode_ = false;
    bool rankingVisible_ = false;
    bool rankingRegistered_ = false;
    bool exitRequested_ = false;
    int exitTargetIndex_ = 1;
    int currentRank_ = -1;
    int rankingDisplayFirstRank_ = 1;
    Transform explosionEnemyTransform_{};

    Camera camera_;
    Player player_;
    Enemy enemy_;
    EnemyPhaseMaterialSet enemyPhaseMaterials_{};
    uint32_t playerModelId_ = 0;
    uint32_t swordModelId_ = 0;
    uint32_t enemyModelId_ = 0;
    BattleArenaModelIds arenaModels_{};
    uint32_t slashModelId_ = 0;
    uint32_t slashTextureId_ = 0;
    uint32_t explosionCoreModelId_ = 0;
    uint32_t explosionFlashModelId_ = 0;
    uint32_t particleTextureId_ = 0;
    uint32_t fireBillboardModelId_ = 0;
    uint32_t smokeBillboardModelId_ = 0;
    uint32_t darkSmokeBillboardModelId_ = 0;
    uint32_t explosionSoundId_ = UINT32_MAX;
    uint32_t slashSoundId_ = UINT32_MAX;
    uint32_t resultCrowdIntroSoundId_ = UINT32_MAX;
    uint32_t resultCrowdIntroVoiceHandle_ = UINT32_MAX;
    uint32_t slashSound1VoiceHandle_ = UINT32_MAX;
    uint32_t slashSound2VoiceHandle_ = UINT32_MAX;
    uint32_t slashSound3VoiceHandle_ = UINT32_MAX;
    bool slashSound1Played_ = false;
    bool slashSound2Played_ = false;
    bool slashSound3Played_ = false;
    std::array<VictoryConfetti, 180> victoryConfetti_{};

    Image missionCompleteLabel_{};
    Image clearTimeLabel_{};
    Image scoreTitleLabel_{};
    Image difficultyLabel_{};
    Image rankingTitleLabel_{};
    Image rankingRankHeaderLabel_{};
    Image rankingScoreHeaderLabel_{};
    Image rankingTimeHeaderLabel_{};
    Image rankingDifficultyHeaderLabel_{};
    Image retryButtonLabel_{};
    Image titleButtonLabel_{};
    std::array<Image, 10> digitImages_{};
    Image colonImage_{};
    Image dotImage_{};
    Image dashImage_{};
    Image secondImage_{};
    std::vector<RankingEntry> rankingEntries_;
    int actionButtonIndex_ = 1;
};
