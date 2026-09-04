#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "CameraPreviewReceiver.h"
#include "Player.h"
#include "SwordInputCalibration.h"
#include "SwordPose.h"
#include "SwordUdpController.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <string>

class Input;

/// <summary>
/// カメラ入力精度と剣姿勢の調整結果を可視化する診断画面を管理する
/// </summary>
class CameraAccuracyDebugScene : public BaseScene {
  public:
    enum class ReturnTarget {
        Title,
        WeaponSelect,
        TutorialSelect,
        WeaponOption,
        TutorialOption,
    };

    /// <summary>
    /// CameraAccuracyDebugSceneに対応する公開処理を実行する
    /// </summary>
    explicit CameraAccuracyDebugScene(
        ReturnTarget returnTarget = ReturnTarget::Title);
    /// <summary>
    /// ~CameraAccuracyDebugSceneに対応する公開処理を実行する
    /// </summary>
    ~CameraAccuracyDebugScene() override;

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

    bool RequestHandTrackingStartOnce();
    Image LoadTextureImage(const std::wstring &path);
    void UpdateCamera();
    void ReturnToPreviousScene();
    void UpdateSensitivityInput(Input &input);
    void AdjustSelectedSensitivity(int direction);
    int SensitivityItemCount() const;
    float GetSensitivityValue(size_t index) const;
    float GetSensitivityMin(size_t index) const;
    float GetSensitivityMax(size_t index) const;
    float GetSensitivityStep(size_t index) const;
    void SetSensitivityValue(size_t index, float value);
    float GetSensitivityNormalizedValue(size_t index) const;
    void CaptureNeutral();
    void ResetNeutral();
    void UpdateGamePreview(float deltaTime);
    Transform BuildSwordTransform(const SwordPose &pose,
                                  const DirectX::XMFLOAT3 &anchor,
                                  bool isLeft) const;
    void DrawDebugSwords();
    void DrawPreviewBackground(float screenWidth, float screenHeight);
    void DrawOverlay(float screenWidth, float screenHeight);
    void DrawSensitivityPanel(float screenWidth);
    void DrawGaugeRow(size_t index, const Image &label, float value,
                      const DirectX::XMFLOAT4 &barColor, float panelX,
                      float rowY, float panelW);
    void DrawSettingGaugeRow(size_t index, const Image &label,
                             const DirectX::XMFLOAT4 &barColor, float panelX,
                             float rowY, float panelW);
    void DrawImage(const Image &image, float x, float y, float scale = 1.0f,
                   float alpha = 1.0f);
    void DrawImageSized(const Image &image, float x, float y, float w, float h,
                        const DirectX::XMFLOAT4 &color);
    void DrawImageCentered(const Image &image, float centerX, float centerY,
                           float maxWidth, float maxHeight, float alpha = 1.0f);
    void DrawSensitivityValue(float value, float x, float y, float scale,
                              const DirectX::XMFLOAT4 &color);
    float RequiredTravelForSensitivity(float axisSensitivity) const;
    void DrawHandPanel(const char *title, const char *subtitle,
                       size_t handIndex, float x, float y, float w, float h);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawFrame(float x, float y, float w, float h, float thickness,
                   const DirectX::XMFLOAT4 &color);
    void DrawPoint(float x, float y, float radius,
                   const DirectX::XMFLOAT4 &color);
    void DrawText(const std::string &text, float x, float y, float scale,
                  const DirectX::XMFLOAT4 &color);

    SwordInputCalibration calibration_{};
    SwordUdpController controller_;
    CameraPreviewReceiver previewReceiver_{};
    Camera camera_;
    Player gamePreviewPlayer_{};
    uint32_t playerModelId_ = 0;
    uint32_t swordModelId_ = 0;
    Image titleImage_{};
    Image controlsImage_{};
    Image detailTitleImage_{};
    Image basicTitleImage_{};
    Image detailHintImage_{};
    Image basicHintImage_{};
    Image pageLabelImage_{};
    Image gaugeTrackImage_{};
    Image gaugeFillImage_{};
    Image gaugeFrameImage_{};
    Image rowSelectImage_{};
    std::array<Image, 64> settingLabelImages_{};
    std::array<Image, 10> digitImages_{};
    Image dotImage_{};
    ReturnTarget returnTarget_ = ReturnTarget::Title;
    int selectedSensitivityIndex_ = 0;
    bool detailedSensitivityMode_ = false;
    bool handTrackingStartRequested_ = false;
    bool neutralCapturedThisScene_ = false;
    float sceneTime_ = 0.0f;
};
