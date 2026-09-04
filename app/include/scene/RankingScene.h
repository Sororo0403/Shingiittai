#pragma once
#include "BaseScene.h"
#include "GameScene.h"
#include "InputControlType.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// <summary>
/// 保存された戦績を読み込み、ランキングとして表示する
/// </summary>
class RankingScene : public BaseScene {
  public:
    enum class ReturnTarget {
        WeaponSelect,
        TutorialSelect,
    };

    /// <summary>
    /// RankingSceneに対応する公開処理を実行する
    /// </summary>
    explicit RankingScene(ReturnTarget returnTarget);

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
    void DrawTransparent() override {}

  private:
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
        float inkLeft = 0.0f;
        float inkRight = 0.0f;
        float inkTop = 0.0f;
        float inkBottom = 0.0f;
    };

    struct RankingEntry {
        int score = 0;
        float difficulty = 0.0f;
        float clearTime = 0.0f;
        InputControlType controlType = InputControlType::KeyboardMouse;
    };

    Image LoadTextureImage(const std::wstring &path);
    void LoadRanking();
    void BeginReturn();
    void ChangeControlType(InputControlType controlType);
    void DrawOverlay(float screenWidth, float screenHeight);
    void DrawRanking(float screenWidth, float screenHeight);
    void DrawRankingHeader(float x, float y, float panelWidth,
                           float panelHeight, float intro);
    void DrawTransition(float screenWidth, float screenHeight);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawFrame(float x, float y, float w, float h, float thickness,
                   const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);
    void DrawTextLineLeft(const std::string &text, float x, float y,
                          float scale = 1.0f, float alpha = 1.0f);
    void DrawTextLineRight(const std::string &text, float rightX, float y,
                           float scale = 1.0f, float alpha = 1.0f);
    void DrawTextLineLeftBaseline(const std::string &text, float x,
                                  float baselineY, float scale = 1.0f,
                                  float alpha = 1.0f);
    void DrawTextLineRightBaseline(const std::string &text, float rightX,
                                   float baselineY, float scale = 1.0f,
                                   float alpha = 1.0f);
    float GetCharAdvance(char c) const;
    float MeasureTextLine(const std::string &text, float scale) const;
    float MeasureTextInkCenterOffset(const std::string &text,
                                     float scale) const;
    const Image *FindCharImage(char c) const;
    std::string FormatTime(float seconds) const;
    std::string FormatDifficulty(float difficulty) const;
    std::string FormatScore(int score) const;
    std::unique_ptr<BaseScene> CreateReturnScene() const;

    ReturnTarget returnTarget_ = ReturnTarget::WeaponSelect;
    std::unique_ptr<GameScene> backgroundScene_;
    Image rankingTitleLabel_{};
    Image rankHeaderLabel_{};
    Image scoreHeaderLabel_{};
    Image timeHeaderLabel_{};
    Image difficultyHeaderLabel_{};
    Image modeKbmLabel_{};
    Image modeHandLabel_{};
    std::array<Image, 10> digitImages_{};
    Image colonImage_{};
    Image dotImage_{};
    Image dashImage_{};
    Image secondImage_{};
    std::vector<RankingEntry> rankingEntries_;
    InputControlType selectedControlType_ = InputControlType::KeyboardMouse;
    float introTimer_ = 0.0f;
    float transitionTimer_ = 0.0f;
    bool returnRequested_ = false;
};
