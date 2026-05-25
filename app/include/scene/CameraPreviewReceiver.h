#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class SpriteManager;
class TextureManager;

class CameraPreviewReceiver {
  public:
    ~CameraPreviewReceiver();

    void Initialize(TextureManager *texture, uint16_t port = 5006,
                    uint32_t width = 320, uint32_t height = 180);
    void Update(float deltaTime);
    void Close();
    void Draw(SpriteManager *sprite, TextureManager *texture,
              float staleSeconds);

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
    std::vector<uint8_t> jpegBuffer_{};
    std::vector<bool> chunkReceived_{};
    uintptr_t socket_ = UINTPTR_MAX;
    uint32_t frameId_ = 0;
    size_t receivedChunks_ = 0;
    uint16_t port_ = 5006;
    bool socketReady_ = false;
};
