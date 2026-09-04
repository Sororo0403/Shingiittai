#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class SpriteManager;
class TextureManager;

/// <summary>
/// UDPで分割送信されたカメラJPEGを復元し、プレビューとして描画する
/// </summary>
class CameraPreviewReceiver {
  public:
    /// <summary>
    /// ~CameraPreviewReceiverに対応する公開処理を実行する
    /// </summary>
    ~CameraPreviewReceiver();

    /// <summary>
    /// 使用するリソースと初期状態を準備する
    /// </summary>
    void Initialize(TextureManager *texture, uint16_t port = 5006,
                    uint32_t width = 320, uint32_t height = 180);
    /// <summary>
    /// 入力と状態を1フレーム進める
    /// </summary>
    void Update(float deltaTime);
    /// <summary>
    /// 受信処理と保持中のネットワークリソースを閉じる
    /// </summary>
    void Close();
    /// <summary>
    /// HasFreshFrameの条件を満たすか判定する
    /// </summary>
    bool HasFreshFrame(float staleSeconds) const;
    /// <summary>
    /// 現在の状態を描画する
    /// </summary>
    void Draw(SpriteManager *sprite, TextureManager *texture,
              float staleSeconds, bool backBufferTarget = false,
              bool mirrorX = true);
    /// <summary>
    /// DrawAreaに対応する公開処理を実行する
    /// </summary>
    void DrawArea(SpriteManager *sprite, TextureManager *texture, float x,
                  float y, float w, float h, float staleSeconds,
                  float alpha = 1.0f, bool mirrorX = true);

  private:
    struct Frame {
        bool valid = false;
        bool dirty = false;
        bool textureSizeDirty = false;
        uint32_t textureId = 0;
        uint32_t width = 320;
        uint32_t height = 180;
        float staleTimer = 999.0f;
        std::vector<uint8_t> rgbaPixels;
    };

    bool EnsureSocket();
    void ReceivePackets();
    void HandlePacket(const uint8_t *data, int bytes);
    void DecodeJpeg(const std::vector<uint8_t> &jpegData);
    void UploadTextureIfNeeded(TextureManager *texture);

    Frame frame_{};
    std::vector<uint8_t> jpegBuffer_;
    std::vector<bool> chunkReceived_;
    uintptr_t socket_ = UINTPTR_MAX;
    uint32_t frameId_ = 0;
    size_t receivedChunks_ = 0;
    uint16_t port_ = 5006;
    bool socketReady_ = false;
};
