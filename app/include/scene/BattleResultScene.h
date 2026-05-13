#pragma once
#include "BaseScene.h"
#include "PlayerWeaponType.h"
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
                      PlayerWeaponType weaponType,
                      const SwordInputCalibration &inputCalibration = {});

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawOverlay() override;

  private:
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };

    Image LoadTextureImage(const std::wstring &path);
    void LoadRanking();
    void SaveRanking() const;
    void RegisterClearTime();
    void UpdateHandResultInput(float deltaTime);
    void DrawBackground(float screenWidth, float screenHeight);
    void DrawClear(float screenWidth, float screenHeight);
    void DrawGameOver(float screenWidth, float screenHeight);
    void DrawHandInputStatus(float screenWidth, float screenHeight);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawTextLine(const std::string &text, float centerX, float y,
                      float scale = 1.0f, float alpha = 1.0f);
    float MeasureTextLine(const std::string &text, float scale) const;
    const Image *FindCharImage(char c) const;
    std::string FormatTime(float seconds) const;

  private:
    static constexpr int kMaxRanking = 5;

    ResultKind resultKind_ = ResultKind::GameOver;
    PlayerWeaponType weaponType_ = PlayerWeaponType::Standard;
    SwordInputCalibration inputCalibration_{};
    SwordUdpController handController_;
    float clearTime_ = 0.0f;
    float sceneTime_ = 0.0f;
    float handIdleTimer_ = 0.0f;
    int handSwingCount_ = 0;
    bool handSwingArmed_ = true;
    bool registered_ = false;
    bool newRecord_ = false;
    int newRecordIndex_ = -1;
    std::vector<float> ranking_;

    Image clearTitle_{};
    Image gameOverTitle_{};
    Image clearTimeLabel_{};
    Image rankingLabel_{};
    Image newRecordLabel_{};
    Image noClearTimeLabel_{};
    Image retryLabel_{};
    Image menuLabel_{};
    std::array<Image, 10> digitImages_{};
    std::array<Image, kMaxRanking> rankImages_{};
    Image colonImage_{};
    Image dotImage_{};
    Image dashImage_{};
    Image secondImage_{};
};
