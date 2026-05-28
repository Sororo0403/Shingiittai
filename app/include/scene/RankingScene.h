#pragma once
#include "BaseScene.h"
#include "GameScene.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class RankingScene : public BaseScene {
  public:
    enum class ReturnTarget {
        WeaponSelect,
        TutorialSelect,
    };

    explicit RankingScene(ReturnTarget returnTarget);

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawTransparent() override {}

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
    };

    Image LoadTextureImage(const std::wstring &path);
    void LoadRanking();
    void BeginReturn();
    void DrawOverlay(float screenWidth, float screenHeight);
    void DrawRanking(float screenWidth, float screenHeight);
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
    float MeasureTextLine(const std::string &text, float scale) const;
    const Image *FindCharImage(char c) const;
    std::string FormatTime(float seconds) const;
    std::string FormatDifficulty(float difficulty) const;
    std::string FormatScore(int score) const;
    std::unique_ptr<BaseScene> CreateReturnScene() const;

    ReturnTarget returnTarget_ = ReturnTarget::WeaponSelect;
    std::unique_ptr<GameScene> backgroundScene_;
    Image rankingTitleLabel_{};
    std::array<Image, 10> digitImages_{};
    Image colonImage_{};
    Image dotImage_{};
    Image dashImage_{};
    Image secondImage_{};
    std::vector<RankingEntry> rankingEntries_{};
    float introTimer_ = 0.0f;
    float transitionTimer_ = 0.0f;
    bool returnRequested_ = false;
};
