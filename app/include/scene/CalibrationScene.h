#pragma once
#include "BaseScene.h"
#include "InputControlType.h"
#include "Sprite.h"
#include "SwordInputCalibration.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <string>

/// <summary>
/// 選択した入力方式に必要な初期キャリブレーションを進行する
/// </summary>
class CalibrationScene : public BaseScene {
  public:
    /// <summary>
    /// CalibrationSceneに対応する公開処理を実行する
    /// </summary>
    explicit CalibrationScene(InputControlType controlType);

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
    void DrawTransparent() override;

  private:
    struct Image {
        uint32_t textureId = 0;
        float width = 0.0f;
        float height = 0.0f;
    };

    Image LoadTextureImage(const std::wstring &path);
    void FinishCalibration();
    void DrawBackground(float screenWidth, float screenHeight);
    void DrawProgress(float screenWidth, float screenHeight);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);

    InputControlType controlType_ = InputControlType::KeyboardMouse;
    SwordInputCalibration inputCalibration_{};

    Image backgroundImage_{};
    std::array<Image, 10> digitImages_{};

    float sceneTime_ = 0.0f;
    float stableTimer_ = 0.0f;
    bool finished_ = false;
};
