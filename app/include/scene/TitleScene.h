#pragma once
#include "BaseScene.h"
#include "GameScene.h"
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
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };

    Image LoadTitleImage(const std::wstring &path);
    void UpdateExitConfirm(Input &input);
    void DrawTitleOverlay(float screenWidth, float screenHeight);
    void DrawExitConfirmWindow(float screenWidth, float screenHeight);
    void DrawStartupFrame(float screenWidth, float screenHeight);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float alpha,
                   float scale);
    bool IsAnyButtonTriggered(const Input &input) const;
    void StartTitleBgm();
    void StopTitleBgm();

    Image logoImage_;
    Image pressAnyButtonImage_;
    Image exitPromptImage_;
    Image exitConfirmMessageImage_;
    Image exitConfirmYesImage_;
    Image exitConfirmNoImage_;
    std::unique_ptr<GameScene> backgroundScene_;
    float sceneTime_ = 0.0f;
    float frameIntroTimer_ = 0.0f;
    float fadeTimer_ = 0.0f;
    uint32_t titleBgmSoundId_ = UINT32_MAX;
    uint32_t titleBgmVoiceHandle_ = UINT32_MAX;
    bool startRequested_ = false;
    bool exitConfirmVisible_ = false;
    int exitConfirmIndex_ = 1;
};
