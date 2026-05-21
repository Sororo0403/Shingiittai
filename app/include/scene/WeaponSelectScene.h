#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "InputControlType.h"
#include "JoyCon.h"
#include "Sprite.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class Input;

class WeaponSelectScene : public BaseScene {
  public:
    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;
    void DrawOverlay() override {}

  private:
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };

    struct ButtonRect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    static constexpr int kWeaponCount = 3;
    static constexpr int kSelectableCount = kWeaponCount + 1;
    static constexpr int kRankingButtonIndex = kWeaponCount;
    static constexpr int kMaxRanking = 5;

    Image LoadTextureImage(const std::wstring &path);
    void UpdateSelection(Input *input);
    void LoadRankings();
    std::vector<float> LoadRankingForControl(InputControlType controlType) const;
    void UpdateDeviceAvailability();
    void BeginStart();
    void Layout(float screenWidth, float screenHeight);
    InputControlType SelectedControlType() const;
    InputControlType ControlTypeForIndex(int index) const;
    bool IsHandTrackingReady() const;
    void RequestHandTrackingStartOnce();
    bool IsModeAvailable(int index) const;
    void ShowUnavailableMessage(int index);
    void UpdateCamera();
    void UpdateLighting();

    void DrawBackground(float screenWidth, float screenHeight);
    void DrawCards(float screenWidth, float screenHeight);
    void DrawModelPreviews();
    void DrawLabels(float screenWidth, float screenHeight);
    void DrawRankingButton(float screenWidth, float screenHeight);
    void DrawRankingPanel(float screenWidth, float screenHeight);
    void DrawStartTransition(float screenWidth, float screenHeight);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);
    void DrawTextLine(const std::string &text, float centerX, float y,
                      float scale = 1.0f, float alpha = 1.0f);
    float MeasureTextLine(const std::string &text, float scale) const;
    const Image *FindCharImage(char c) const;
    std::string FormatTime(float seconds) const;
    Transform MakeSwordTransform(int weaponIndex, int swordIndex) const;
    DirectX::XMFLOAT4 WeaponColor(int index, float alpha = 1.0f,
                                  bool available = true) const;

    int selectedIndex_ = 0;
    float sceneTime_ = 0.0f;
    float transitionTimer_ = 0.0f;
    bool startRequested_ = false;
    bool waitingForHandTrackingReady_ = false;
    bool handTrackingStartRequested_ = false;
    bool rankingOpen_ = false;

    uint32_t swordModelId_ = 0;
    Camera camera_;
    JoyCon leftJoyCon_;
    JoyCon rightJoyCon_;
    Image backgroundImage_{};
    Image titleImage_{};
    Image controlsImage_{};
    Image readyImage_{};
    Image rankingButtonImage_{};
    std::array<Image, kWeaponCount> weaponNameImages_{};
    std::array<Image, kWeaponCount> weaponDescImages_{};
    std::array<Image, kWeaponCount> weaponBottomImages_{};
    std::array<Image, 10> digitImages_{};
    std::array<Image, kMaxRanking> rankImages_{};
    Image colonImage_{};
    Image dotImage_{};
    Image dashImage_{};
    Image secondImage_{};
    std::array<ButtonRect, kWeaponCount> cardRects_{};
    ButtonRect rankingButtonRect_{};
    std::array<std::vector<float>, kWeaponCount> rankings_{};
    std::array<float, kWeaponCount> pulseTimers_{};
    bool joyConAvailable_ = false;
    bool cameraAvailable_ = false;
};
