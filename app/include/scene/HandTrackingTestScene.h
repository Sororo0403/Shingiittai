#pragma once
#include "BaseScene.h"
#include "Sprite.h"
#include "SwordUdpController.h"
#include <DirectXMath.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

/// <summary>
/// ハンドトラッキング入力とカメラ映像を可視化して診断する
/// </summary>
class HandTrackingTestScene : public BaseScene {
  public:
    /// <summary>
    /// ~HandTrackingTestSceneに対応する公開処理を実行する
    /// </summary>
    ~HandTrackingTestScene() override;
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
    struct HandDebugState {
        bool active = false;
        bool hasCenter = false;
        bool slash = false;
        float x = 0.5f;
        float y = 0.5f;
        float rawSpeed = 0.0f;
        float motionSpeed = 0.0f;
        DirectX::XMFLOAT2 slashDir{0.0f, 0.0f};
        float pulse = 0.0f;
        std::array<DirectX::XMFLOAT2, 36> trail{};
        size_t trailCount = 0;
        size_t trailCursor = 0;
    };

    struct CameraPreviewFrame {
        bool valid = false;
        bool dirty = false;
        uint32_t textureId = 0;
        uint32_t width = 320;
        uint32_t height = 240;
        float staleTimer = 999.0f;
        std::vector<uint8_t> rgbaPixels;
    };

    void RequestHandTrackingStartOnce();
    bool EnsurePreviewSocket();
    void ClosePreviewSocket();
    void ReceivePreviewPackets();
    void HandlePreviewPacket(const uint8_t *data, int bytes);
    void DecodePreviewJpeg(const std::vector<uint8_t> &jpegData);
    void UploadPreviewTextureIfNeeded();
    void UpdateHand(size_t handIndex);
    void UpdateRangedGesture();
    void UpdateRangedGestureState(bool pressed, bool released, bool joined);
    bool IsHandTrackingReady() const;
    DirectX::XMFLOAT2 ToFieldPosition(float screenWidth, float screenHeight,
                                      float x, float y) const;

    void DrawBackground(float screenWidth, float screenHeight);
    void DrawCameraPreview(float screenWidth, float screenHeight);
    void DrawTrackingField(float screenWidth, float screenHeight);
    void DrawHand(size_t handIndex, float screenWidth, float screenHeight);
    void DrawRangedGesture(float screenWidth, float screenHeight);
    void DrawStatusPanel(float screenWidth, float screenHeight);
    void DrawHandMeter(size_t handIndex, float x, float y, float width);
    void DrawCameraBadge(float screenWidth, float screenHeight);
    void DrawRect(float x, float y, float w, float h,
                  const DirectX::XMFLOAT4 &color);
    void DrawLine(float x0, float y0, float x1, float y1, float thickness,
                  const DirectX::XMFLOAT4 &color);
    DirectX::XMFLOAT4 HandColor(size_t handIndex, float alpha = 1.0f) const;

    SwordUdpController handController_;
    std::array<HandDebugState, 2> hands_{};
    enum class RangedGestureState { Idle, Windup, Charging, Recovery };
    RangedGestureState rangedGestureState_ = RangedGestureState::Idle;
    bool rangedGestureHeld_ = false;
    bool rangedGestureReleased_ = false;
    float rangedGestureTimer_ = 0.0f;
    float rangedGestureChargeRatio_ = 0.0f;
    float rangedGesturePulse_ = 0.0f;
    DirectX::XMFLOAT2 rangedGestureCenter_{0.5f, 0.5f};
    DirectX::XMFLOAT2 rangedGestureAim_{0.0f, -1.0f};
    CameraPreviewFrame previewFrame_{};
    std::vector<uint8_t> previewJpegBuffer_;
    std::vector<bool> previewChunkReceived_;
    uint32_t previewFrameId_ = 0;
    size_t previewReceivedChunks_ = 0;
    uintptr_t previewSocket_ = UINTPTR_MAX;
    SwordInputCalibration calibration_{};
    float sceneTime_ = 0.0f;
    float cameraRequestTimer_ = 999.0f;
    bool handTrackingStartRequested_ = false;
    bool previewSocketReady_ = false;
};
