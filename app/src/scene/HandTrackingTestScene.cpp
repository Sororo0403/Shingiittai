#include "HandTrackingTestScene.h"
#include "AppSceneServices.h"
#include "DirectXCommon.h"
#include "GameScene.h"
#include "Input.h"
#include "PostEffectManager.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "TitleScene.h"
#include "WinApp.h"
#include <DirectXTex.h>
#include <WS2tcpip.h>
#include <WinSock2.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>

using namespace DirectX;

namespace {
constexpr float kFieldWidthRatio = 0.72f;
constexpr float kFieldHeightRatio = 0.62f;
constexpr float kRawSpeedFull = 2.1f;
constexpr float kMotionSpeedFull = 2600.0f;
constexpr float kRangedGestureJoinDistance = 0.105f;
constexpr float kRangedGestureReleaseDistance = 0.16f;
constexpr float kRangedGestureJoinMaxAxisOffset = 0.085f;
constexpr float kRangedGestureReleaseMaxAxisOffset = 0.13f;
constexpr float kRangedGestureWindupSeconds = 0.42f;
constexpr float kRangedGestureChargeSeconds = 1.15f;
constexpr float kRangedGestureRecoverySeconds = 0.55f;
constexpr uint16_t kPreviewPort = 5006;

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

SOCKET ToSocket(uintptr_t value) { return static_cast<SOCKET>(value); }

bool IsRangedGesturePose(float dx, float dy, bool alreadyHeld) {
    const float maxDistance = alreadyHeld ? kRangedGestureReleaseDistance
                                          : kRangedGestureJoinDistance;
    const float maxAxisOffset = alreadyHeld ? kRangedGestureReleaseMaxAxisOffset
                                            : kRangedGestureJoinMaxAxisOffset;
    return std::sqrt(dx * dx + dy * dy) < maxDistance &&
           std::fabs(dx) < maxAxisOffset && std::fabs(dy) < maxAxisOffset;
}
} // namespace

HandTrackingTestScene::~HandTrackingTestScene() {
    ClosePreviewSocket();
    AppSceneServices::RequestHandTrackingStop();
}

void HandTrackingTestScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    cameraRequestTimer_ = 999.0f;
    handTrackingStartRequested_ = false;
    calibration_ = {};
    calibration_.controlType = InputControlType::Hand;
    handController_.SetCalibration(calibration_);
    previewFrame_ = {};
    previewFrame_.rgbaPixels.resize(static_cast<size_t>(previewFrame_.width) *
                                    previewFrame_.height * 4u);
    previewFrame_.textureId = ctx_->rendering.texture->CreateFromRgbaPixels(
        previewFrame_.width, previewFrame_.height,
        previewFrame_.rgbaPixels.data());
    previewJpegBuffer_.clear();
    previewChunkReceived_.clear();
    previewFrameId_ = 0;
    previewReceivedChunks_ = 0;

    for (HandDebugState &hand : hands_) {
        hand = {};
    }
    rangedGestureState_ = RangedGestureState::Idle;
    rangedGestureHeld_ = false;
    rangedGestureReleased_ = false;
    rangedGestureTimer_ = 0.0f;
    rangedGestureChargeRatio_ = 0.0f;
    rangedGesturePulse_ = 0.0f;
    rangedGestureCenter_ = {0.5f, 0.5f};
    rangedGestureAim_ = {0.0f, -1.0f};

    if (ctx_->rendering.postEffectManager != nullptr) {
        ctx_->rendering.postEffectManager->SetBaseProfile(PostProcessProfile{});
    }

    RequestHandTrackingStartOnce();
}

void HandTrackingTestScene::Update() {
    sceneTime_ += ctx_->frame.deltaTime;
    cameraRequestTimer_ += ctx_->frame.deltaTime;
    previewFrame_.staleTimer += ctx_->frame.deltaTime;

    handController_.Update(ctx_->frame.deltaTime);
    ReceivePreviewPackets();
    const bool hasCameraData =
        previewFrame_.valid && previewFrame_.staleTimer <= 0.75f;
    const bool hasHandData = handController_.HasFreshInput();
    if (!hasCameraData && !hasHandData && cameraRequestTimer_ >= 1.0f) {
        handTrackingStartRequested_ = false;
        RequestHandTrackingStartOnce();
    }
    for (size_t i = 0; i < hands_.size(); ++i) {
        UpdateHand(i);
    }
    UpdateRangedGesture();

    if (ctx_->systems.input->IsKeyTrigger(DIK_ESCAPE) ||
        ctx_->systems.input->IsKeyTrigger(DIK_TAB) ||
        ctx_->systems.input->IsKeyTrigger(DIK_BACK)) {
        sceneManager_->ChangeScene(std::make_unique<TitleScene>());
        return;
    }
}

void HandTrackingTestScene::Draw() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    ctx_->rendering.sprite->PreDraw();
    UploadPreviewTextureIfNeeded();
    DrawBackground(w, h);
    DrawCameraPreview(w, h);
    DrawTrackingField(w, h);
    DrawStatusPanel(w, h);
    DrawCameraBadge(w, h);
    for (size_t i = 0; i < hands_.size(); ++i) {
        DrawHand(i, w, h);
    }
    DrawRangedGesture(w, h);
    ctx_->rendering.sprite->PostDraw();
}

void HandTrackingTestScene::RequestHandTrackingStartOnce() {
    if (handTrackingStartRequested_ ||
        !AppSceneServices::HasHandTrackingStart()) {
        return;
    }
    handTrackingStartRequested_ = AppSceneServices::RequestHandTrackingStart();
    cameraRequestTimer_ = 0.0f;
}

bool HandTrackingTestScene::EnsurePreviewSocket() {
    if (previewSocketReady_) {
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
    address.sin_port = htons(kPreviewPort);
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

    previewSocket_ = static_cast<uintptr_t>(udpSocket);
    previewSocketReady_ = true;
    return true;
}

void HandTrackingTestScene::ClosePreviewSocket() {
    if (previewSocketReady_) {
        closesocket(ToSocket(previewSocket_));
        WSACleanup();
    }
    previewSocket_ = UINTPTR_MAX;
    previewSocketReady_ = false;
}

void HandTrackingTestScene::ReceivePreviewPackets() {
    if (!EnsurePreviewSocket()) {
        return;
    }

    std::array<uint8_t, 1600> buffer{};
    for (;;) {
        sockaddr_in from{};
        int fromLength = sizeof(from);
        const int bytes = recvfrom(
            ToSocket(previewSocket_), reinterpret_cast<char *>(buffer.data()),
            static_cast<int>(buffer.size()), 0,
            reinterpret_cast<sockaddr *>(&from), &fromLength);
        if (bytes == SOCKET_ERROR) {
            return;
        }
        HandlePreviewPacket(buffer.data(), bytes);
    }
}

void HandTrackingTestScene::HandlePreviewPacket(const uint8_t *data,
                                                int bytes) {
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
        totalSize == 0 || totalSize > size_t{1024} * 1024u) {
        return;
    }

    const uint8_t *payload = newline + 1;
    const size_t payloadSize = static_cast<size_t>(data + bytes - payload);
    const size_t offset = chunkIndex * 1150u;
    if (offset >= totalSize || payloadSize > totalSize - offset) {
        return;
    }

    if (frameId != previewFrameId_ ||
        previewChunkReceived_.size() != chunkCount ||
        previewJpegBuffer_.size() != totalSize) {
        previewFrameId_ = frameId;
        previewJpegBuffer_.assign(totalSize, 0u);
        previewChunkReceived_.assign(chunkCount, false);
        previewReceivedChunks_ = 0;
    }

    if (!previewChunkReceived_[chunkIndex]) {
        std::memcpy(previewJpegBuffer_.data() + offset, payload, payloadSize);
        previewChunkReceived_[chunkIndex] = true;
        ++previewReceivedChunks_;
    }

    if (previewReceivedChunks_ == previewChunkReceived_.size()) {
        DecodePreviewJpeg(previewJpegBuffer_);
    }
}

void HandTrackingTestScene::DecodePreviewJpeg(
    const std::vector<uint8_t> &jpegData) {
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
        image->height == 0 || image->width != previewFrame_.width ||
        image->height != previewFrame_.height) {
        return;
    }

    const size_t rowBytes = static_cast<size_t>(previewFrame_.width) * 4u;
    const size_t imageBytes =
        rowBytes * static_cast<size_t>(previewFrame_.height);
    if (previewFrame_.rgbaPixels.size() != imageBytes) {
        previewFrame_.rgbaPixels.resize(imageBytes);
    }

    for (uint32_t y = 0; y < previewFrame_.height; ++y) {
        std::memcpy(previewFrame_.rgbaPixels.data() + rowBytes * y,
                    image->pixels + image->rowPitch * y, rowBytes);
    }
    previewFrame_.valid = true;
    previewFrame_.dirty = true;
    previewFrame_.staleTimer = 0.0f;
}

void HandTrackingTestScene::UploadPreviewTextureIfNeeded() {
    if (!previewFrame_.dirty || !previewFrame_.valid ||
        previewFrame_.rgbaPixels.empty()) {
        return;
    }

    ctx_->rendering.texture->UpdateTexture2D(
        previewFrame_.textureId, previewFrame_.rgbaPixels.data(),
        static_cast<size_t>(previewFrame_.width) * 4u);
    previewFrame_.dirty = false;
}

void HandTrackingTestScene::UpdateHand(size_t handIndex) {
    HandDebugState &debug = hands_[handIndex];
    const bool wasSlash = debug.slash;
    const auto sample = handController_.GetDebugHandState(handIndex);
    debug.active = sample.active;
    debug.hasCenter = sample.fresh && sample.active;
    debug.x = sample.rawPalm.x;
    debug.y = sample.rawPalm.y;
    debug.rawSpeed = sample.packetMotionSpeed;
    debug.motionSpeed = sample.motionSpeed;

    const SwordPose pose = handController_.GetPose(handIndex);
    debug.slash = pose.isSlashMode;
    debug.slashDir = pose.slashDir;
    if (!wasSlash && debug.slash) {
        debug.pulse = 1.0f;
    } else {
        debug.pulse =
            (std::max)(0.0f, debug.pulse - ctx_->frame.deltaTime * 3.6f);
    }

    if (debug.hasCenter) {
        debug.trail[debug.trailCursor] = {debug.x, debug.y};
        debug.trailCursor = (debug.trailCursor + 1) % debug.trail.size();
        if (debug.trailCount < debug.trail.size()) {
            ++debug.trailCount;
        }
    } else {
        debug.trailCount = 0;
        debug.trailCursor = 0;
    }
}

void HandTrackingTestScene::UpdateRangedGesture() {
    const HandDebugState &left = hands_[0];
    const HandDebugState &right = hands_[1];
    const bool bothHands = left.hasCenter && right.hasCenter;
    const float dx = left.x - right.x;
    const float dy = left.y - right.y;
    const bool joined =
        bothHands && IsRangedGesturePose(dx, dy, rangedGestureHeld_);
    const bool pressed = joined && !rangedGestureHeld_;
    const bool released = !joined && rangedGestureHeld_;
    rangedGestureHeld_ = joined;
    rangedGestureReleased_ = false;

    if (bothHands) {
        rangedGestureCenter_ = {(left.x + right.x) * 0.5f,
                                (left.y + right.y) * 0.5f};
        const float aimX =
            std::clamp((rangedGestureCenter_.x - 0.5f) * 2.0f, -1.0f, 1.0f);
        rangedGestureAim_ = {aimX,
                             -std::sqrt((std::max)(0.0f, 1.0f - aimX * aimX))};
    }

    UpdateRangedGestureState(pressed, released, joined);

    rangedGesturePulse_ =
        (std::max)(0.0f, rangedGesturePulse_ - ctx_->frame.deltaTime * 2.5f);
}

void HandTrackingTestScene::UpdateRangedGestureState(bool pressed,
                                                     bool released,
                                                     bool joined) {
    switch (rangedGestureState_) {
    case RangedGestureState::Idle:
        rangedGestureChargeRatio_ = 0.0f;
        if (pressed) {
            rangedGestureState_ = RangedGestureState::Windup;
            rangedGestureTimer_ = 0.0f;
            rangedGesturePulse_ = 0.55f;
        }
        break;
    case RangedGestureState::Windup:
        rangedGestureTimer_ += ctx_->frame.deltaTime;
        if (released || !joined) {
            rangedGestureState_ = RangedGestureState::Recovery;
            rangedGestureTimer_ = 0.0f;
            rangedGestureChargeRatio_ = 0.0f;
            break;
        }
        if (rangedGestureTimer_ >= kRangedGestureWindupSeconds) {
            rangedGestureState_ = RangedGestureState::Charging;
            rangedGestureTimer_ = 0.0f;
        }
        break;
    case RangedGestureState::Charging:
        rangedGestureTimer_ += ctx_->frame.deltaTime;
        rangedGestureChargeRatio_ = std::clamp(
            rangedGestureTimer_ / kRangedGestureChargeSeconds, 0.0f, 1.0f);
        if (released || !joined) {
            rangedGestureReleased_ = rangedGestureChargeRatio_ >= 1.0f;
            rangedGesturePulse_ = rangedGestureReleased_ ? 1.0f : 0.34f;
            rangedGestureState_ = RangedGestureState::Recovery;
            rangedGestureTimer_ = 0.0f;
        }
        break;
    case RangedGestureState::Recovery:
        rangedGestureTimer_ += ctx_->frame.deltaTime;
        if (rangedGestureTimer_ >= kRangedGestureRecoverySeconds) {
            rangedGestureState_ = RangedGestureState::Idle;
            rangedGestureTimer_ = 0.0f;
            rangedGestureChargeRatio_ = 0.0f;
            rangedGestureReleased_ = false;
        }
        break;
    }
}

bool HandTrackingTestScene::IsHandTrackingReady() const {
    return AppSceneServices::IsHandTrackingReady();
}

XMFLOAT2 HandTrackingTestScene::ToFieldPosition(float screenWidth,
                                                float screenHeight, float x,
                                                float y) const {
    const float fieldW = screenWidth * kFieldWidthRatio;
    const float fieldH = screenHeight * kFieldHeightRatio;
    const float fieldX = (screenWidth - fieldW) * 0.5f;
    const float fieldY = screenHeight * 0.12f;
    return {fieldX + std::clamp(x, 0.0f, 1.0f) * fieldW,
            fieldY + std::clamp(y, 0.0f, 1.0f) * fieldH};
}

void HandTrackingTestScene::DrawBackground(float screenWidth,
                                           float screenHeight) {
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.020f, 0.024f, 0.028f, 1.0f));

    const float stripeH = 8.0f;
    for (int i = 0; i < 7; ++i) {
        const float y = screenHeight * 0.10f + static_cast<float>(i) * 72.0f;
        DrawRect(0.0f, y, screenWidth, stripeH,
                 Color(0.07f, 0.12f, 0.16f, 0.22f));
    }

    DrawRect(0.0f, screenHeight - 96.0f, screenWidth, 96.0f,
             Color(0.010f, 0.012f, 0.016f, 0.92f));
}

void HandTrackingTestScene::DrawCameraPreview(float screenWidth,
                                              float screenHeight) {
    const float fieldW = screenWidth * kFieldWidthRatio;
    const float fieldH = screenHeight * kFieldHeightRatio;
    const float fieldX = (screenWidth - fieldW) * 0.5f;
    const float fieldY = screenHeight * 0.12f;
    const float previewAspect =
        static_cast<float>(previewFrame_.width) /
        static_cast<float>((std::max)(previewFrame_.height, 1u));
    float drawW = fieldW;
    float drawH = drawW / previewAspect;
    if (drawH > fieldH) {
        drawH = fieldH;
        drawW = drawH * previewAspect;
    }
    const float drawX = fieldX + (fieldW - drawW) * 0.5f;
    const float drawY = fieldY + (fieldH - drawH) * 0.5f;

    DrawRect(fieldX, fieldY, fieldW, fieldH,
             Color(0.01f, 0.012f, 0.014f, 1.0f));
    if (previewFrame_.valid) {
        Sprite sprite{};
        sprite.position = {drawX, drawY};
        sprite.size = {drawW, drawH};
        sprite.color = {1.0f, 1.0f, 1.0f,
                        previewFrame_.staleTimer > 0.55f ? 0.36f : 1.0f};
        sprite.textureId = previewFrame_.textureId;
        sprite.uvLeftTop = {1.0f, 0.0f};
        sprite.uvSize = {-1.0f, 1.0f};
        ctx_->rendering.sprite->DrawSprite(sprite);

        if (previewFrame_.staleTimer > 0.55f) {
            DrawRect(fieldX, fieldY, fieldW, fieldH,
                     Color(0.02f, 0.03f, 0.04f, 0.46f));
        }
    } else {
        const float pulse = 0.45f + 0.35f * std::sinf(sceneTime_ * 4.2f);
        DrawRect(fieldX + fieldW * 0.34f, fieldY + fieldH * 0.48f,
                 fieldW * 0.32f, 7.0f, Color(0.18f, 0.24f, 0.30f, 0.90f));
        DrawRect(fieldX + fieldW * 0.34f, fieldY + fieldH * 0.48f,
                 fieldW * (0.08f + pulse * 0.24f), 7.0f,
                 Color(0.10f, 0.60f, 1.0f, 0.82f));
    }

    const float dim = 0.18f;
    DrawRect(fieldX, fieldY, fieldW, fieldH, Color(0.0f, 0.0f, 0.0f, dim));
}

void HandTrackingTestScene::DrawTrackingField(float screenWidth,
                                              float screenHeight) {
    const float fieldW = screenWidth * kFieldWidthRatio;
    const float fieldH = screenHeight * kFieldHeightRatio;
    const float fieldX = (screenWidth - fieldW) * 0.5f;
    const float fieldY = screenHeight * 0.12f;

    DrawRect(fieldX, fieldY, fieldW, 4.0f, Color(0.72f, 0.82f, 0.94f, 0.88f));
    DrawRect(fieldX, fieldY + fieldH - 4.0f, fieldW, 4.0f,
             Color(0.72f, 0.82f, 0.94f, 0.88f));
    DrawRect(fieldX, fieldY, 4.0f, fieldH, Color(0.72f, 0.82f, 0.94f, 0.88f));
    DrawRect(fieldX + fieldW - 4.0f, fieldY, 4.0f, fieldH,
             Color(0.72f, 0.82f, 0.94f, 0.88f));

    for (int i = 1; i < 4; ++i) {
        const float t = static_cast<float>(i) / 4.0f;
        DrawRect(fieldX + fieldW * t, fieldY, 2.0f, fieldH,
                 Color(0.34f, 0.46f, 0.54f, 0.28f));
        DrawRect(fieldX, fieldY + fieldH * t, fieldW, 2.0f,
                 Color(0.34f, 0.46f, 0.54f, 0.28f));
    }

    const float cx = fieldX + fieldW * 0.5f;
    const float cy = fieldY + fieldH * 0.5f;
    DrawRect(cx - 42.0f, cy - 2.0f, 84.0f, 4.0f,
             Color(1.0f, 0.82f, 0.20f, 0.76f));
    DrawRect(cx - 2.0f, cy - 42.0f, 4.0f, 84.0f,
             Color(1.0f, 0.82f, 0.20f, 0.76f));
}

void HandTrackingTestScene::DrawHand(size_t handIndex, float screenWidth,
                                     float screenHeight) {
    const HandDebugState &hand = hands_[handIndex];
    if (!hand.hasCenter) {
        return;
    }

    const XMFLOAT4 color = HandColor(handIndex);
    const size_t count = hand.trailCount;
    for (size_t i = 0; i < count; ++i) {
        const size_t index =
            (hand.trailCursor + hand.trail.size() - count + i) %
            hand.trail.size();
        const XMFLOAT2 point =
            ToFieldPosition(screenWidth, screenHeight, hand.trail[index].x,
                            hand.trail[index].y);
        const float alpha = static_cast<float>(i + 1) /
                            static_cast<float>((std::max)(count, size_t{1}));
        DrawRect(point.x - 3.0f, point.y - 3.0f, 6.0f, 6.0f,
                 HandColor(handIndex, 0.12f + alpha * 0.36f));
    }

    const XMFLOAT2 pos =
        ToFieldPosition(screenWidth, screenHeight, hand.x, hand.y);
    const float pulseSize = hand.pulse * 34.0f;
    const float baseSize = hand.slash ? 30.0f : 22.0f;
    DrawRect(pos.x - baseSize - pulseSize * 0.5f, pos.y - 2.0f,
             baseSize * 2.0f + pulseSize, 4.0f, color);
    DrawRect(pos.x - 2.0f, pos.y - baseSize - pulseSize * 0.5f, 4.0f,
             baseSize * 2.0f + pulseSize, color);
    DrawRect(pos.x - 11.0f, pos.y - 11.0f, 22.0f, 22.0f,
             HandColor(handIndex, hand.slash ? 0.86f : 0.58f));

    if (hand.slash) {
        const float len = 74.0f;
        DrawLine(pos.x, pos.y, pos.x + hand.slashDir.x * len,
                 pos.y + hand.slashDir.y * len, 8.0f,
                 Color(1.0f, 0.18f, 0.10f, 0.88f));
    }
}

void HandTrackingTestScene::DrawRangedGesture(float screenWidth,
                                              float screenHeight) {
    const bool visible = rangedGestureHeld_ ||
                         rangedGestureState_ != RangedGestureState::Idle ||
                         rangedGesturePulse_ > 0.0f;
    if (!visible) {
        return;
    }

    const XMFLOAT2 center =
        ToFieldPosition(screenWidth, screenHeight, rangedGestureCenter_.x,
                        rangedGestureCenter_.y);
    const XMFLOAT2 left =
        ToFieldPosition(screenWidth, screenHeight, hands_[0].x, hands_[0].y);
    const XMFLOAT2 right =
        ToFieldPosition(screenWidth, screenHeight, hands_[1].x, hands_[1].y);
    const float ready = rangedGestureChargeRatio_;
    const bool charging = rangedGestureState_ == RangedGestureState::Charging;
    const bool windup = rangedGestureState_ == RangedGestureState::Windup;
    const XMFLOAT4 chargeColor =
        ready >= 1.0f ? Color(1.0f, 0.92f, 0.20f, 0.96f)
                      : Color(1.0f, 0.58f, 0.12f, charging ? 0.84f : 0.56f);

    if (hands_[0].hasCenter && hands_[1].hasCenter) {
        DrawLine(left.x, left.y, right.x, right.y, windup ? 5.0f : 8.0f,
                 Color(1.0f, 0.78f, 0.18f, windup ? 0.46f : 0.78f));
    }

    const float ring = 26.0f + ready * 38.0f + rangedGesturePulse_ * 42.0f;
    DrawRect(center.x - ring, center.y - 3.0f, ring * 2.0f, 6.0f, chargeColor);
    DrawRect(center.x - 3.0f, center.y - ring, 6.0f, ring * 2.0f, chargeColor);
    DrawRect(center.x - 14.0f, center.y - 14.0f, 28.0f, 28.0f,
             Color(1.0f, 0.86f, 0.24f, 0.62f + ready * 0.30f));

    const float aimLen = 94.0f + ready * 36.0f;
    DrawLine(center.x, center.y, center.x + rangedGestureAim_.x * aimLen,
             center.y + rangedGestureAim_.y * aimLen, 7.0f,
             ready >= 1.0f ? Color(1.0f, 0.96f, 0.32f, 0.96f)
                           : Color(1.0f, 0.62f, 0.16f, 0.74f));

    const float meterW = screenWidth * 0.28f;
    const float meterX = (screenWidth - meterW) * 0.5f;
    const float meterY = screenHeight - 34.0f;
    DrawRect(meterX, meterY, meterW, 12.0f, Color(0.16f, 0.13f, 0.08f, 0.92f));
    DrawRect(meterX, meterY, meterW * ready, 12.0f, chargeColor);
    if (rangedGestureReleased_) {
        DrawRect(
            meterX - 18.0f, meterY - 7.0f, meterW + 36.0f, 26.0f,
            Color(1.0f, 0.90f, 0.22f, 0.32f + rangedGesturePulse_ * 0.38f));
    }
}

void HandTrackingTestScene::DrawStatusPanel(float screenWidth,
                                            float screenHeight) {
    const float panelY = screenHeight - 78.0f;
    const float panelW = screenWidth * 0.36f;
    DrawHandMeter(0, screenWidth * 0.08f, panelY, panelW);
    DrawHandMeter(1, screenWidth * 0.56f, panelY, panelW);
}

void HandTrackingTestScene::DrawHandMeter(size_t handIndex, float x, float y,
                                          float width) {
    const HandDebugState &hand = hands_[handIndex];
    const XMFLOAT4 color = HandColor(handIndex, hand.active ? 0.92f : 0.24f);
    const float rawT = std::clamp(hand.rawSpeed / kRawSpeedFull, 0.0f, 1.0f);
    const float motionT =
        std::clamp(hand.motionSpeed / kMotionSpeedFull, 0.0f, 1.0f);

    DrawRect(x, y, width, 9.0f, Color(0.16f, 0.18f, 0.20f, 0.92f));
    DrawRect(x, y, width * rawT, 9.0f, color);
    DrawRect(x, y + 18.0f, width, 13.0f, Color(0.16f, 0.18f, 0.20f, 0.92f));
    DrawRect(x, y + 18.0f, width * motionT, 13.0f,
             hand.slash ? Color(1.0f, 0.18f, 0.10f, 0.95f) : color);

    DrawRect(x, y - 15.0f, hand.active ? 44.0f : 16.0f, 8.0f, color);
    DrawRect(x + width - 70.0f, y - 15.0f, hand.slash ? 70.0f : 20.0f, 8.0f,
             hand.slash ? Color(1.0f, 0.18f, 0.10f, 0.95f)
                        : Color(0.22f, 0.24f, 0.26f, 0.70f));
}

void HandTrackingTestScene::DrawCameraBadge(float, float) {
    const bool cameraAvailable = AppSceneServices::IsCameraDeviceAvailable();
    const bool ready = IsHandTrackingReady();
    const bool hasPacket = handController_.HasFreshInput();
    const float x = 20.0f;
    const float y = 18.0f;
    const float pulse = 0.55f + 0.45f * std::sinf(sceneTime_ * 5.0f);

    DrawRect(x, y, 146.0f, 44.0f, Color(0.02f, 0.03f, 0.04f, 0.82f));
    DrawRect(x + 14.0f, y + 13.0f, 32.0f, 20.0f,
             Color(0.78f, 0.88f, 0.96f, 0.88f));
    DrawRect(x + 21.0f, y + 8.0f, 13.0f, 6.0f,
             Color(0.78f, 0.88f, 0.96f, 0.88f));
    DrawRect(x + 25.0f, y + 18.0f, 11.0f, 10.0f,
             Color(0.04f, 0.06f, 0.08f, 0.92f));

    DrawRect(x + 64.0f, y + 13.0f, 13.0f, 13.0f,
             cameraAvailable ? Color(0.18f, 0.86f, 0.38f, 0.92f)
                             : Color(0.88f, 0.12f, 0.08f, 0.92f));
    DrawRect(x + 87.0f, y + 13.0f, 13.0f, 13.0f,
             ready ? Color(0.18f, 0.86f, 0.38f, 0.92f)
                   : Color(1.0f, 0.78f, 0.12f, 0.45f + pulse * 0.45f));
    DrawRect(x + 110.0f, y + 13.0f, 13.0f, 13.0f,
             hasPacket ? Color(0.10f, 0.60f, 1.0f, 0.92f)
                       : Color(0.30f, 0.34f, 0.38f, 0.78f));
}

void HandTrackingTestScene::DrawRect(float x, float y, float w, float h,
                                     const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void HandTrackingTestScene::DrawLine(float x0, float y0, float x1, float y1,
                                     float thickness, const XMFLOAT4 &color) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const int steps =
        (std::max)(1, static_cast<int>(std::ceil(std::sqrt(dx * dx + dy * dy) /
                                                 (std::max)(thickness, 1.0f))));
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float x = x0 + dx * t;
        const float y = y0 + dy * t;
        DrawRect(x - thickness * 0.5f, y - thickness * 0.5f, thickness,
                 thickness, color);
    }
}

XMFLOAT4 HandTrackingTestScene::HandColor(size_t handIndex, float alpha) const {
    if (handIndex == 0) {
        return Color(0.10f, 0.72f, 1.0f, alpha);
    }
    return Color(0.22f, 1.0f, 0.48f, alpha);
}
