#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

class HandCameraPreviewReceiver {
  public:
    HandCameraPreviewReceiver() = default;
    ~HandCameraPreviewReceiver();

    HandCameraPreviewReceiver(const HandCameraPreviewReceiver &) = delete;
    HandCameraPreviewReceiver &
    operator=(const HandCameraPreviewReceiver &) = delete;

    void Update();
    bool ConsumeFrame(std::vector<uint8_t> &rgbaPixels, uint32_t &width,
                      uint32_t &height);
    bool HasFrame() const { return hasDecodedFrame_; }

  private:
    static constexpr uint16_t kPort = 5006;
    static constexpr size_t kMaxPacketSize = 1500;
    static constexpr size_t kMaxFrameBytes = 256 * 1024;
    static constexpr size_t kPreviewChunkBytes = 1150;

    bool EnsureSocket();
    void ReceivePackets();
    void ResetAssembly(uint32_t frameId, uint16_t chunkCount,
                       uint32_t totalBytes);
    void CloseSocket();

    uintptr_t socket_ = UINTPTR_MAX;
    bool socketReady_ = false;

    uint32_t assemblingFrameId_ = 0;
    uint32_t assemblingTotalBytes_ = 0;
    uint16_t assemblingChunkCount_ = 0;
    uint16_t receivedChunkCount_ = 0;
    std::vector<uint8_t> assemblyBuffer_;
    std::vector<uint8_t> receivedChunks_;
    std::vector<uint8_t> latestJpeg_;
    bool hasLatestJpeg_ = false;
    bool hasDecodedFrame_ = false;
};
