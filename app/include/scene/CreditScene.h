#pragma once
#include "BaseScene.h"
#include "GameScene.h"
#include <DirectXMath.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class CreditScene : public BaseScene {
  public:
    enum class ReturnTarget {
        Title,
        WeaponSelect,
        TutorialSelect,
    };

    explicit CreditScene(ReturnTarget returnTarget = ReturnTarget::Title);
    ~CreditScene() override;

    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;

  private:
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };

    Image LoadTextureImage(const std::wstring &path);
    void LoadCreditLineImages();
    void BeginReturnToTitle();
    void DrawOverlay(float screenWidth, float screenHeight);
    void DrawCredits(float screenWidth, float screenHeight);
    void DrawControlsPrompt(float screenWidth, float screenHeight);
    void DrawTransition(float screenWidth, float screenHeight);
    float CalculateLogoStopDistance(float screenWidth,
                                    float screenHeight) const;
    bool IsLogoStopped(float screenWidth, float screenHeight) const;
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawFrame(float x, float y, float w, float h, float thickness,
                   const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);
    void StartCreditBgm();
    void StopCreditBgm();
    std::unique_ptr<BaseScene> CreateReturnScene() const;

    ReturnTarget returnTarget_ = ReturnTarget::Title;
    std::unique_ptr<GameScene> backgroundScene_;
    Image logoImage_{};
    Image thankYouImage_{};
    Image taroSignatureImage_{};
    Image aotoMoriSignatureImage_{};
    Image tsunaguSignatureImage_{};
    Image controlsImage_{};
    struct CreditLine {
        Image image{};
        float centerY = 0.0f;
        float scale = 1.0f;
    };
    std::vector<CreditLine> creditLines_;
    float sceneTime_ = 0.0f;
    float creditRollDistance_ = 0.0f;
    float introTimer_ = 0.0f;
    float transitionTimer_ = 0.0f;
    bool returnRequested_ = false;
    uint32_t creditBgmSoundId_ = UINT32_MAX;
    uint32_t creditBgmVoiceHandle_ = UINT32_MAX;
};
