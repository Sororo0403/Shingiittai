#include "CameraPreviewReceiver.h"

#include "Sprite.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include <DirectXMath.h>
#include <DirectXTex.h>
#include <WS2tcpip.h>
#include <WinSock2.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#include <string>

namespace {
constexpr size_t kChunkPayloadStride = 1150u;
constexpr size_t kMaxJpegSize = size_t{1024} * 1024;
constexpr size_t kReceiveBufferSize = size_t{16} * 1024;
constexpr float kPreviewMargin = 16.0f;
constexpr float kPreviewWidth = 192.0f;

SOCKET ToSocket(uintptr_t value) { return static_cast<SOCKET>(value); }

DirectX::XMFLOAT4 PreviewBorderColor(bool fresh) {
    return fresh ? DirectX::XMFLOAT4{0.32f, 0.72f, 1.0f, 0.62f}
                 : DirectX::XMFLOAT4{0.90f, 0.72f, 0.22f, 0.50f};
}

void DrawRect(SpriteManager *sprite, float x, float y, float w, float h,
              const DirectX::XMFLOAT4 &color) {
    Sprite rect{};
    rect.textureId = 0;
    rect.position = {x, y};
    rect.size = {w, h};
    rect.color = color;
    sprite->DrawSprite(rect);
}
} // namespace

CameraPreviewReceiver::~CameraPreviewReceiver() { Close(); }

void CameraPreviewReceiver::Initialize(TextureManager *texture, uint16_t port,
                                       uint32_t width, uint32_t height) {
    Close();

    port_ = port;
    frame_ = {};
    frame_.width = width;
    frame_.height = height;
    frame_.rgbaPixels.resize(static_cast<size_t>(width) *
                             static_cast<size_t>(height) * 4u);
    if (texture != nullptr) {
        frame_.textureId = texture->CreateFromRgbaPixels(
            width, height, frame_.rgbaPixels.data());
    }

    jpegBuffer_.clear();
    chunkReceived_.clear();
    frameId_ = 0;
    receivedChunks_ = 0;
}

void CameraPreviewReceiver::Update(float deltaTime) {
    frame_.staleTimer += deltaTime;
    ReceivePackets();
}

void CameraPreviewReceiver::Close() {
    if (socketReady_) {
        closesocket(ToSocket(socket_));
        WSACleanup();
    }
    socket_ = UINTPTR_MAX;
    socketReady_ = false;
}

bool CameraPreviewReceiver::HasFreshFrame(float staleSeconds) const {
    return frame_.valid && frame_.staleTimer <= staleSeconds;
}

void CameraPreviewReceiver::DrawArea(SpriteManager *sprite,
                                     TextureManager *texture, float x, float y,
                                     float w, float h, float staleSeconds,
                                     float alpha, bool mirrorX) {
    if (sprite == nullptr || texture == nullptr || w <= 0.0f || h <= 0.0f) {
        return;
    }

    UploadTextureIfNeeded(texture);
    DrawRect(sprite, x, y, w, h, {0.01f, 0.012f, 0.014f, 1.0f});
    if (frame_.valid) {
        const bool fresh = frame_.staleTimer <= staleSeconds;
        Sprite preview{};
        preview.textureId = frame_.textureId;
        preview.position = {x, y};
        preview.size = {w, h};
        if (mirrorX) {
            preview.uvLeftTop = {1.0f, 0.0f};
            preview.uvSize = {-1.0f, 1.0f};
        }
        preview.color = {1.0f, 1.0f, 1.0f, fresh ? alpha : alpha * 0.36f};
        sprite->DrawSprite(preview);
        if (!fresh) {
            DrawRect(sprite, x, y, w, h, {0.0f, 0.0f, 0.0f, 0.44f});
        }
    }
}

bool CameraPreviewReceiver::EnsureSocket() {
    if (socketReady_) {
        return true;
    }

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }

    SOCKET udpSocket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_);
    if (bind(udpSocket, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) == SOCKET_ERROR) {
        closesocket(udpSocket);
        WSACleanup();
        return false;
    }

    u_long nonBlocking = 1;
    if (ioctlsocket(udpSocket, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
        closesocket(udpSocket);
        WSACleanup();
        return false;
    }

    socket_ = static_cast<uintptr_t>(udpSocket);
    socketReady_ = true;
    return true;
}

void CameraPreviewReceiver::ReceivePackets() {
    if (!EnsureSocket()) {
        return;
    }

    std::array<uint8_t, kReceiveBufferSize> buffer{};
    for (;;) {
        sockaddr_in from{};
        int fromLength = sizeof(from);
        const int bytes =
            recvfrom(ToSocket(socket_), reinterpret_cast<char *>(buffer.data()),
                     static_cast<int>(buffer.size()), 0,
                     reinterpret_cast<sockaddr *>(&from), &fromLength);
        if (bytes == SOCKET_ERROR) {
            return;
        }
        HandlePacket(buffer.data(), bytes);
    }
}

void CameraPreviewReceiver::HandlePacket(const uint8_t *data, int bytes) {
    const uint8_t *newline = static_cast<const uint8_t *>(
        std::memchr(data, '\n', static_cast<size_t>(bytes)));
    if (newline == nullptr) {
        return;
    }

    const std::string header(reinterpret_cast<const char *>(data),
                             reinterpret_cast<const char *>(newline));
    std::istringstream stream(header);
    std::string magic;
    uint32_t frameId = 0;
    size_t chunkIndex = 0;
    size_t chunkCount = 0;
    size_t totalSize = 0;
    if (!(stream >> magic >> frameId >> chunkIndex >> chunkCount >>
          totalSize) ||
        magic != "SGCAM" || chunkCount == 0 || chunkIndex >= chunkCount ||
        totalSize == 0 || totalSize > kMaxJpegSize) {
        return;
    }

    const uint8_t *payload = newline + 1;
    const size_t payloadSize = static_cast<size_t>(data + bytes - payload);
    const size_t offset = chunkIndex * kChunkPayloadStride;
    if (offset >= totalSize || payloadSize > totalSize - offset) {
        return;
    }

    if (frameId != frameId_ || chunkReceived_.size() != chunkCount ||
        jpegBuffer_.size() != totalSize) {
        frameId_ = frameId;
        jpegBuffer_.assign(totalSize, 0u);
        chunkReceived_.assign(chunkCount, false);
        receivedChunks_ = 0;
    }

    if (!chunkReceived_[chunkIndex]) {
        std::memcpy(jpegBuffer_.data() + offset, payload, payloadSize);
        chunkReceived_[chunkIndex] = true;
        ++receivedChunks_;
    }

    if (receivedChunks_ == chunkReceived_.size()) {
        DecodeJpeg(jpegBuffer_);
    }
}

void CameraPreviewReceiver::DecodeJpeg(const std::vector<uint8_t> &jpegData) {
    if (jpegData.empty()) {
        return;
    }

    DirectX::ScratchImage scratch;
    DirectX::TexMetadata metadata{};
    HRESULT hr = DirectX::LoadFromWICMemory(jpegData.data(), jpegData.size(),
                                            DirectX::WIC_FLAGS_FORCE_RGB,
                                            &metadata, scratch);
    if (FAILED(hr)) {
        return;
    }

    DirectX::ScratchImage converted;
    const DirectX::Image *image = scratch.GetImage(0, 0, 0);
    if (image != nullptr && image->format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        hr = DirectX::Convert(*image, DXGI_FORMAT_R8G8B8A8_UNORM,
                              DirectX::TEX_FILTER_DEFAULT, 0.0f, converted);
        if (FAILED(hr)) {
            return;
        }
        image = converted.GetImage(0, 0, 0);
    }

    if (image == nullptr || image->pixels == nullptr || image->width == 0 ||
        image->height == 0) {
        return;
    }

    const uint32_t imageWidth = static_cast<uint32_t>(image->width);
    const uint32_t imageHeight = static_cast<uint32_t>(image->height);
    if (imageWidth != frame_.width || imageHeight != frame_.height) {
        frame_.width = imageWidth;
        frame_.height = imageHeight;
        frame_.textureSizeDirty = true;
    }

    const size_t rowBytes = static_cast<size_t>(frame_.width) * 4u;
    const size_t imageBytes = rowBytes * static_cast<size_t>(frame_.height);
    if (frame_.rgbaPixels.size() != imageBytes) {
        frame_.rgbaPixels.resize(imageBytes);
    }

    for (uint32_t y = 0; y < frame_.height; ++y) {
        std::memcpy(frame_.rgbaPixels.data() + rowBytes * y,
                    image->pixels + image->rowPitch * y, rowBytes);
    }
    frame_.valid = true;
    frame_.dirty = true;
    frame_.staleTimer = 0.0f;
}

void CameraPreviewReceiver::UploadTextureIfNeeded(TextureManager *texture) {
    if (texture == nullptr || !frame_.dirty || !frame_.valid ||
        frame_.rgbaPixels.empty()) {
        return;
    }

    if (frame_.textureSizeDirty) {
        frame_.textureId = texture->CreateFromRgbaPixels(
            frame_.width, frame_.height, frame_.rgbaPixels.data());
        frame_.textureSizeDirty = false;
    } else {
        texture->UpdateTexture2D(frame_.textureId, frame_.rgbaPixels.data(),
                                 static_cast<size_t>(frame_.width) * 4u);
    }
    frame_.dirty = false;
}

void CameraPreviewReceiver::Draw(SpriteManager *sprite, TextureManager *texture,
                                 float staleSeconds, bool backBufferTarget,
                                 bool mirrorX) {
    if (sprite == nullptr || texture == nullptr) {
        return;
    }

    UploadTextureIfNeeded(texture);
    const float previewAspect =
        static_cast<float>(frame_.width) /
        static_cast<float>((std::max)(frame_.height, 1u));
    const float previewHeight = kPreviewWidth / previewAspect;
    const bool fresh = frame_.valid && frame_.staleTimer <= staleSeconds;
    const float alpha = fresh ? 0.88f : 0.34f;

    sprite->PreDraw(backBufferTarget);
    DrawRect(sprite, kPreviewMargin - 4.0f, kPreviewMargin - 4.0f,
             kPreviewWidth + 8.0f, previewHeight + 8.0f,
             {0.0f, 0.0f, 0.0f, 0.52f});

    if (frame_.valid) {
        Sprite preview{};
        preview.textureId = frame_.textureId;
        preview.position = {kPreviewMargin, kPreviewMargin};
        preview.size = {kPreviewWidth, previewHeight};
        if (mirrorX) {
            preview.uvLeftTop = {1.0f, 0.0f};
            preview.uvSize = {-1.0f, 1.0f};
        }
        preview.color = {1.0f, 1.0f, 1.0f, alpha};
        sprite->DrawSprite(preview);
    }

    const DirectX::XMFLOAT4 border = PreviewBorderColor(fresh);
    DrawRect(sprite, kPreviewMargin, kPreviewMargin, kPreviewWidth, 2.0f,
             border);
    DrawRect(sprite, kPreviewMargin, kPreviewMargin + previewHeight - 2.0f,
             kPreviewWidth, 2.0f, border);
    DrawRect(sprite, kPreviewMargin, kPreviewMargin, 2.0f, previewHeight,
             border);
    DrawRect(sprite, kPreviewMargin + kPreviewWidth - 2.0f, kPreviewMargin,
             2.0f, previewHeight, border);
    sprite->PostDraw();
}
