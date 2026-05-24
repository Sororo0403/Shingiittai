#pragma once
#include "BaseScene.h"
#include "InputControlType.h"
#include "JoyCon.h"
#include "Sprite.h"
#include "SwordInputCalibration.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <string>

class CalibrationScene : public BaseScene {
  public:
    explicit CalibrationScene(InputControlType controlType);

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
    void UpdateJoyConStability(float deltaTime);
    void FinishCalibration();
    bool IsJoyConStable() const;
    void DrawBackground(float screenWidth, float screenHeight);
    void DrawProgress(float screenWidth, float screenHeight);
    void DrawStatusBars(float screenWidth, float screenHeight);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);

    InputControlType controlType_ = InputControlType::JoyCon;
    JoyCon leftJoyCon_;
    JoyCon rightJoyCon_;
    SwordInputCalibration inputCalibration_{};

    Image backgroundImage_{};
    std::array<Image, 10> digitImages_{};

    float sceneTime_ = 0.0f;
    float stableTimer_ = 0.0f;
    float leftJoyConAngularSpeed_ = 0.0f;
    float rightJoyConAngularSpeed_ = 0.0f;
    DirectX::XMFLOAT4 prevLeftJoyConOrientation_{0, 0, 0, 1};
    DirectX::XMFLOAT4 prevRightJoyConOrientation_{0, 0, 0, 1};
    bool hasPrevLeftJoyConOrientation_ = false;
    bool hasPrevRightJoyConOrientation_ = false;
    bool finished_ = false;
};
