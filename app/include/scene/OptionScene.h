#pragma once
#include "BaseScene.h"
#include "GameScene.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

class Input;

class OptionScene : public BaseScene {
  public:
    enum class ReturnTarget {
        WeaponSelect,
        TutorialSelect,
    };

    explicit OptionScene(ReturnTarget returnTarget);
    ~OptionScene() override;

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

    Image LoadTextureImage(const std::wstring &path);
    void BeginReturn();
    void AdjustSelectedOption(int direction);
    void DrawOverlay(float screenWidth, float screenHeight);
    void DrawPanel(float screenWidth, float screenHeight);
    void DrawControlsPrompt(float screenWidth, float screenHeight);
    void DrawTransition(float screenWidth, float screenHeight);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawFrame(float x, float y, float w, float h, float thickness,
                   const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);
    std::unique_ptr<BaseScene> CreateReturnScene() const;

    static constexpr int kOptionCount = 8;

    ReturnTarget returnTarget_ = ReturnTarget::WeaponSelect;
    std::unique_ptr<GameScene> backgroundScene_;
    Image titleImage_{};
    Image displayLabelImage_{};
    Image bgmLabelImage_{};
    Image seLabelImage_{};
    Image cameraSensitivityLabelImage_{};
    Image cameraSlashSensitivityLabelImage_{};
    Image cameraVerticalSensitivityLabelImage_{};
    Image cameraHorizontalSensitivityLabelImage_{};
    Image mouseSlashLabelImage_{};
    Image fullscreenValueImage_{};
    Image windowValueImage_{};
    Image keyAImage_{};
    Image keyDImage_{};
    Image tabBackPromptImage_{};
    int selectedIndex_ = 0;
    int adjustHoldDirection_ = 0;
    float adjustHoldTimer_ = 0.0f;
    float adjustRepeatTimer_ = 0.0f;
    float introTimer_ = 0.0f;
    float transitionTimer_ = 0.0f;
    bool returnRequested_ = false;
    bool preserveMenuBgmOnExit_ = false;
};
