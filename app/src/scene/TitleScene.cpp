#include "TitleScene.h"
#include <WinSock2.h>
#include <WS2tcpip.h>
#include "DirectXCommon.h"
#include "GameScene.h"
#include "HandTrackingTestScene.h"
#include "Input.h"
#include "PostEffectRenderer.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TipScene.h"
#include "TextureManager.h"
#include "WinApp.h"
#include "WeaponSelectScene.h"
#include <DirectXTex.h>
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>

using namespace DirectX;

namespace {
constexpr float kFadeDuration = 0.35f;
constexpr uint16_t kPreviewPort = 5006;
constexpr float kPreviewStaleSeconds = 0.75f;
constexpr float kReadyHoldSeconds = 0.18f;
constexpr float kWaveStartSpeed = 0.78f;
constexpr float kWaveResetSpeed = 0.32f;
constexpr float kReadyMinX = 0.18f;
constexpr float kReadyMaxX = 0.82f;
constexpr float kReadyMinY = 0.18f;
constexpr float kReadyMaxY = 0.84f;

XMFLOAT4 MakeColor(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }

SOCKET ToSocket(uintptr_t value) {
    return static_cast<SOCKET>(value);
}
} // namespace

TitleScene::~TitleScene() { ClosePreviewSocket(); }

void TitleScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    fadeTimer_ = 0.0f;
    startRequested_ = false;
    cameraStartRequested_ = false;
    waitingForCameraReady_ = false;
    handTrackingStartRequested_ = false;
    cameraRequestTimer_ = 999.0f;
    handsReadyTimer_ = 0.0f;
    waveArmed_ = true;
    previewFrame_ = {};
    previewFrame_.textureId = ctx.texture->CreateDynamicTexture(
        previewFrame_.width, previewFrame_.height);
    previewFrame_.rgbaPixels.resize(
        static_cast<size_t>(previewFrame_.width) * previewFrame_.height * 4u);
    previewJpegBuffer_.clear();
    previewChunkReceived_.clear();
    previewFrameId_ = 0;
    previewReceivedChunks_ = 0;
    for (HandReadyState &hand : handReadyStates_) {
        hand = {};
    }

    ctx_->dxCommon->BeginUpload();
    logoImage_ = LoadTitleImage(L"app/resources/title/title_simple.png");
    ctx_->dxCommon->EndUpload();
    ctx_->texture->ReleaseUploadBuffers();

    demoScene_ = std::make_unique<GameScene>(GameScene::RunMode::TitleDemo);
    demoScene_->SetSceneManager(sceneManager_);
    demoScene_->Initialize(ctx);
}

void TitleScene::Update() {
    sceneTime_ += ctx_->deltaTime;

    if (demoScene_) {
        demoScene_->Update();
    }

    handWarmupController_.Update(ctx_->deltaTime);
    if (cameraStartRequested_) {
        UpdateCameraPreparation();
    }

    if (waitingForCameraReady_ && HasHandsInReadyZone() && HasWaveGesture()) {
        startRequested_ = true;
        waitingForCameraReady_ = false;
        fadeTimer_ = 0.0f;
    }

    if (startRequested_) {
        fadeTimer_ += ctx_->deltaTime;
        if (fadeTimer_ >= kFadeDuration) {
            if (cameraStartRequested_) {
                SwordInputCalibration calibration{};
                calibration.controlType = InputControlType::Hand;
                sceneManager_->ChangeScene(std::make_unique<TipScene>(calibration));
            } else {
                sceneManager_->ChangeScene(
                    std::make_unique<WeaponSelectScene>());
            }
        }
        return;
    }

    if (IsHandTestShortcutTriggered(*ctx_->input)) {
        sceneManager_->ChangeScene(std::make_unique<HandTrackingTestScene>());
        return;
    }

    if (IsCameraShortcutTriggered(*ctx_->input)) {
        BeginCameraMode();
        return;
    }

    if (cameraStartRequested_) {
        waitingForCameraReady_ = true;
        return;
    }

    if (IsAnyButtonTriggered(*ctx_->input)) {
        startRequested_ = true;
        fadeTimer_ = 0.0f;
    }
}

void TitleScene::Draw() {
    if (demoScene_) {
        demoScene_->Draw();
    }
}

void TitleScene::DrawOverlay() {
    const float w = static_cast<float>(ctx_->winApp->GetWidth());
    const float h = static_cast<float>(ctx_->winApp->GetHeight());

    ctx_->sprite->PreDraw();

    DrawRect(0.0f, 0.0f, w, h, MakeColor(0.0f, 0.0f, 0.0f, 0.42f));

    UploadPreviewTextureIfNeeded();
    if (cameraStartRequested_) {
        DrawCameraPreparation(w, h);
    } else {
        const float logoScale = std::clamp(w * 0.50f / logoImage_.width,
                                           0.58f, 1.0f);
        const float logoX = (w - logoImage_.width * logoScale) * 0.5f;
        const float logoY = (h - logoImage_.height * logoScale) * 0.5f;
        DrawImage(logoImage_, logoX, logoY, 1.0f, logoScale);
    }

    DrawCameraModeBadge(w, h);

    if (startRequested_) {
        const float fadeT =
            std::clamp(fadeTimer_ / kFadeDuration, 0.0f, 1.0f);
        DrawRect(0.0f, 0.0f, w, h,
                 MakeColor(0.0f, 0.0f, 0.0f, SmoothStep(fadeT)));
    }

    ctx_->sprite->PostDraw();
}

TitleScene::Image TitleScene::LoadTitleImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->texture->Load(path);
    image.width = static_cast<float>(ctx_->texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->texture->GetHeight(image.textureId));
    return image;
}

void TitleScene::DrawRect(float x, float y, float w, float h,
                          const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->sprite->DrawSprite(sprite);
}

void TitleScene::DrawImage(const Image &image, float x, float y, float alpha) {
    DrawImage(image, x, y, alpha, 1.0f);
}

void TitleScene::DrawImage(const Image &image, float x, float y, float alpha,
                           float scale) {
    if (image.width <= 0.0f || image.height <= 0.0f) {
        return;
    }

    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {image.width * scale, image.height * scale};
    sprite.color = {1.0f, 1.0f, 1.0f, alpha};
    sprite.textureId = image.textureId;
    ctx_->sprite->DrawSprite(sprite);
}

void TitleScene::DrawCameraModeBadge(float, float) {
    if (!cameraStartRequested_) {
        return;
    }

    const bool ready = IsCameraReady();
    const float x = 18.0f;
    const float y = 18.0f;
    const float pulse = 0.55f + 0.45f * std::sinf(sceneTime_ * 5.4f);
    DrawRect(x, y, 94.0f, 42.0f, MakeColor(0.02f, 0.025f, 0.035f, 0.78f));
    DrawRect(x, y + 39.0f, 78.0f, 3.0f,
             MakeColor(0.10f, 0.58f, 1.0f, 0.92f));
    DrawRect(x + 15.0f, y + 15.0f, 31.0f, 19.0f,
             MakeColor(0.86f, 0.92f, 1.0f, 0.92f));
    DrawRect(x + 21.0f, y + 9.0f, 14.0f, 7.0f,
             MakeColor(0.86f, 0.92f, 1.0f, 0.92f));
    DrawRect(x + 24.0f, y + 19.0f, 13.0f, 10.0f,
             MakeColor(0.08f, 0.12f, 0.18f, 0.92f));
    DrawRect(x + 49.0f, y + 19.0f, 13.0f, 10.0f,
             MakeColor(0.86f, 0.92f, 1.0f, 0.92f));
    DrawRect(x + 66.0f, y + 10.0f, 8.0f, 8.0f,
             ready ? MakeColor(0.20f, 1.0f, 0.42f, 0.92f)
                   : MakeColor(1.0f, 0.12f, 0.08f, 0.55f + pulse * 0.45f));
    DrawRect(x + 66.0f, y + 26.0f, ready ? 18.0f : 8.0f + pulse * 10.0f,
             4.0f,
             ready ? MakeColor(0.20f, 1.0f, 0.42f, 0.88f)
                   : MakeColor(0.10f, 0.58f, 1.0f, 0.60f));
    if (waitingForCameraReady_) {
        DrawRect(x, y + 46.0f, 94.0f, 4.0f,
                 MakeColor(0.08f, 0.10f, 0.13f, 0.84f));
        DrawRect(x, y + 46.0f, 28.0f + pulse * 46.0f, 4.0f,
                 MakeColor(1.0f, 0.82f, 0.18f, 0.92f));
    }
}

bool TitleScene::IsAnyButtonTriggered(const Input &input) const {
    for (int dik = 0; dik < 256; ++dik) {
        if (dik == DIK_F1 || dik == DIK_F2 || dik == DIK_C || dik == DIK_R) {
            continue;
        }
        if (input.IsKeyTrigger(dik)) {
            return true;
        }
    }

    if (!input.IsGamepadConnected()) {
        return false;
    }

    constexpr WORD kButtons[] = {
        XINPUT_GAMEPAD_DPAD_UP,        XINPUT_GAMEPAD_DPAD_DOWN,
        XINPUT_GAMEPAD_DPAD_LEFT,      XINPUT_GAMEPAD_DPAD_RIGHT,
        XINPUT_GAMEPAD_START,          XINPUT_GAMEPAD_BACK,
        XINPUT_GAMEPAD_LEFT_THUMB,     XINPUT_GAMEPAD_RIGHT_THUMB,
        XINPUT_GAMEPAD_LEFT_SHOULDER,  XINPUT_GAMEPAD_RIGHT_SHOULDER,
        XINPUT_GAMEPAD_A,              XINPUT_GAMEPAD_B,
        XINPUT_GAMEPAD_X,              XINPUT_GAMEPAD_Y,
    };

    for (WORD button : kButtons) {
        if (input.IsGamepadButtonTrigger(button)) {
            return true;
        }
    }

    return input.IsGamepadLeftTriggerTrigger() ||
           input.IsGamepadRightTriggerTrigger();
}

bool TitleScene::IsCameraShortcutTriggered(const Input &input) const {
    return input.IsKeyTrigger(DIK_F1);
}

void TitleScene::DrawCameraPreparation(float screenWidth, float screenHeight) {
    const float fieldW = screenWidth * 0.68f;
    const float fieldH = screenHeight * 0.66f;
    const float fieldX = (screenWidth - fieldW) * 0.5f;
    const float fieldY = screenHeight * 0.12f;
    DrawRect(fieldX - 12.0f, fieldY - 12.0f, fieldW + 24.0f,
             fieldH + 24.0f, MakeColor(0.01f, 0.012f, 0.016f, 0.86f));
    DrawCameraPreview(fieldX, fieldY, fieldW, fieldH);
    DrawReadyGuide(fieldX, fieldY, fieldW, fieldH);
    for (size_t i = 0; i < handReadyStates_.size(); ++i) {
        DrawHandMarker(handReadyStates_[i], i, fieldX, fieldY, fieldW, fieldH);
    }

    const bool hasPreview = HasFreshPreview();
    const bool hasPacket = handWarmupController_.HasRecentPacket();
    const bool handsReady = HasHandsInReadyZone();
    const float panelY = fieldY + fieldH + 24.0f;
    const float panelW = fieldW;
    DrawRect(fieldX, panelY, panelW, 46.0f,
             MakeColor(0.02f, 0.025f, 0.032f, 0.86f));

    const float segmentW = (panelW - 40.0f) / 3.0f;
    const XMFLOAT4 ok = MakeColor(0.18f, 0.86f, 0.38f, 0.92f);
    const XMFLOAT4 wait = MakeColor(1.0f, 0.76f, 0.16f, 0.72f);
    const XMFLOAT4 off = MakeColor(0.26f, 0.29f, 0.34f, 0.72f);
    const bool checks[] = {hasPreview, hasPacket, handsReady};
    for (int i = 0; i < 3; ++i) {
        const float x = fieldX + 14.0f + static_cast<float>(i) *
                                      (segmentW + 6.0f);
        DrawRect(x, panelY + 14.0f, segmentW, 18.0f,
                 checks[i] ? ok : (i == 2 && hasPacket ? wait : off));
    }

    const float waveT =
        std::clamp((std::max)(handReadyStates_[0].rawSpeed,
                              handReadyStates_[1].rawSpeed) /
                       kWaveStartSpeed,
                   0.0f, 1.0f);
    DrawRect(fieldX, panelY + 58.0f, panelW, 9.0f,
             MakeColor(0.12f, 0.14f, 0.16f, 0.84f));
    DrawRect(fieldX, panelY + 58.0f, panelW * waveT, 9.0f,
             handsReady ? MakeColor(0.10f, 0.60f, 1.0f, 0.92f)
                        : MakeColor(0.32f, 0.36f, 0.40f, 0.68f));
}

void TitleScene::DrawCameraPreview(float fieldX, float fieldY, float fieldW,
                                   float fieldH) {
    DrawRect(fieldX, fieldY, fieldW, fieldH,
             MakeColor(0.01f, 0.012f, 0.014f, 1.0f));

    if (previewFrame_.valid) {
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

        Sprite sprite{};
        sprite.position = {drawX, drawY};
        sprite.size = {drawW, drawH};
        sprite.color = {1.0f, 1.0f, 1.0f, HasFreshPreview() ? 1.0f : 0.38f};
        sprite.textureId = previewFrame_.textureId;
        ctx_->sprite->DrawSprite(sprite);
    } else {
        const float pulse = 0.45f + 0.35f * std::sinf(sceneTime_ * 4.2f);
        DrawRect(fieldX + fieldW * 0.34f, fieldY + fieldH * 0.50f,
                 fieldW * 0.32f, 7.0f, MakeColor(0.18f, 0.24f, 0.30f, 0.90f));
        DrawRect(fieldX + fieldW * 0.34f, fieldY + fieldH * 0.50f,
                 fieldW * (0.08f + pulse * 0.24f), 7.0f,
                 MakeColor(0.10f, 0.60f, 1.0f, 0.82f));
    }

    DrawRect(fieldX, fieldY, fieldW, fieldH,
             MakeColor(0.0f, 0.0f, 0.0f, 0.14f));
}

void TitleScene::DrawReadyGuide(float fieldX, float fieldY, float fieldW,
                                float fieldH) {
    const float x0 = fieldX + fieldW * kReadyMinX;
    const float x1 = fieldX + fieldW * kReadyMaxX;
    const float y0 = fieldY + fieldH * kReadyMinY;
    const float y1 = fieldY + fieldH * kReadyMaxY;
    const XMFLOAT4 guide =
        HasHandsInReadyZone() ? MakeColor(0.18f, 0.86f, 0.38f, 0.92f)
                              : MakeColor(1.0f, 0.78f, 0.16f, 0.72f);
    DrawRect(x0, y0, x1 - x0, 4.0f, guide);
    DrawRect(x0, y1 - 4.0f, x1 - x0, 4.0f, guide);
    DrawRect(x0, y0, 4.0f, y1 - y0, guide);
    DrawRect(x1 - 4.0f, y0, 4.0f, y1 - y0, guide);
}

void TitleScene::DrawHandMarker(const HandReadyState &hand, size_t handIndex,
                                float fieldX, float fieldY, float fieldW,
                                float fieldH) {
    if (!hand.hasCenter) {
        return;
    }
    const float x = fieldX + std::clamp(hand.x, 0.0f, 1.0f) * fieldW;
    const float y = fieldY + std::clamp(hand.y, 0.0f, 1.0f) * fieldH;
    const float size = hand.insideReadyZone ? 24.0f : 16.0f;
    const XMFLOAT4 color = HandColor(handIndex, hand.active ? 0.88f : 0.32f);
    DrawRect(x - size, y - 2.0f, size * 2.0f, 4.0f, color);
    DrawRect(x - 2.0f, y - size, 4.0f, size * 2.0f, color);
    DrawRect(x - 9.0f, y - 9.0f, 18.0f, 18.0f,
             HandColor(handIndex, hand.insideReadyZone ? 0.74f : 0.36f));
}

bool TitleScene::IsHandTestShortcutTriggered(const Input &input) const {
    return input.IsKeyTrigger(DIK_F2);
}

bool TitleScene::IsCameraReady() const {
    return handWarmupController_.HasRecentPacket();
}

bool TitleScene::HasFreshPreview() const {
    return previewFrame_.valid && previewFrame_.staleTimer <= kPreviewStaleSeconds;
}

bool TitleScene::HasHandsInReadyZone() const {
    return handsReadyTimer_ >= kReadyHoldSeconds;
}

bool TitleScene::HasWaveGesture() const {
    const float speed =
        (std::max)(handReadyStates_[0].rawSpeed, handReadyStates_[1].rawSpeed);
    return waveArmed_ && speed >= kWaveStartSpeed;
}

void TitleScene::BeginCameraMode() {
    cameraStartRequested_ = true;
    waitingForCameraReady_ = true;
    fadeTimer_ = 0.0f;
    handsReadyTimer_ = 0.0f;
    waveArmed_ = true;
    RequestHandTrackingStartOnce();
}

void TitleScene::RequestHandTrackingStartOnce() {
    if (handTrackingStartRequested_ || !ctx_->requestHandTrackingStart) {
        return;
    }
    ctx_->requestHandTrackingStart();
    handTrackingStartRequested_ = true;
    cameraRequestTimer_ = 0.0f;
}

void TitleScene::UpdateCameraPreparation() {
    cameraRequestTimer_ += ctx_->deltaTime;
    previewFrame_.staleTimer += ctx_->deltaTime;
    ReceivePreviewPackets();

    for (size_t i = 0; i < handReadyStates_.size(); ++i) {
        UpdateHandReadyState(i);
    }

    const bool bothHandsReady =
        HasFreshPreview() && handReadyStates_[0].insideReadyZone &&
        handReadyStates_[1].insideReadyZone;
    if (bothHandsReady) {
        handsReadyTimer_ += ctx_->deltaTime;
    } else {
        handsReadyTimer_ = 0.0f;
    }

    const float speed =
        (std::max)(handReadyStates_[0].rawSpeed, handReadyStates_[1].rawSpeed);
    if (speed <= kWaveResetSpeed) {
        waveArmed_ = true;
    }

    const bool hasAnyData =
        HasFreshPreview() || handWarmupController_.HasRecentPacket();
    if (!hasAnyData && cameraRequestTimer_ >= 1.0f) {
        handTrackingStartRequested_ = false;
        RequestHandTrackingStartOnce();
    }
}

void TitleScene::UpdateHandReadyState(size_t handIndex) {
    HandReadyState &hand = handReadyStates_[handIndex];
    hand.active = handWarmupController_.IsActive(handIndex);
    hand.hasCenter =
        handWarmupController_.GetHandCenter(handIndex, hand.x, hand.y);
    hand.rawSpeed = handWarmupController_.GetRawMotionSpeed(handIndex);
    hand.insideReadyZone =
        hand.active && hand.hasCenter && hand.x >= kReadyMinX &&
        hand.x <= kReadyMaxX && hand.y >= kReadyMinY && hand.y <= kReadyMaxY;
    if (hand.insideReadyZone) {
        hand.pulse = 1.0f;
    } else {
        hand.pulse = (std::max)(0.0f, hand.pulse - ctx_->deltaTime * 3.4f);
    }
}

bool TitleScene::EnsurePreviewSocket() {
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

void TitleScene::ClosePreviewSocket() {
    if (previewSocketReady_) {
        closesocket(ToSocket(previewSocket_));
        WSACleanup();
    }
    previewSocket_ = UINTPTR_MAX;
    previewSocketReady_ = false;
}

void TitleScene::ReceivePreviewPackets() {
    if (!EnsurePreviewSocket()) {
        return;
    }

    std::array<uint8_t, 1600> buffer{};
    for (;;) {
        sockaddr_in from{};
        int fromLength = sizeof(from);
        const int bytes = recvfrom(ToSocket(previewSocket_),
                                   reinterpret_cast<char *>(buffer.data()),
                                   static_cast<int>(buffer.size()), 0,
                                   reinterpret_cast<sockaddr *>(&from),
                                   &fromLength);
        if (bytes == SOCKET_ERROR) {
            return;
        }
        HandlePreviewPacket(buffer.data(), bytes);
    }
}

void TitleScene::HandlePreviewPacket(const uint8_t *data, int bytes) {
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
    if (!(stream >> magic >> frameId >> chunkIndex >> chunkCount >> totalSize) ||
        magic != "SGCAM" || chunkCount == 0 || chunkIndex >= chunkCount ||
        totalSize == 0 || totalSize > 1024u * 1024u) {
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

void TitleScene::DecodePreviewJpeg(const std::vector<uint8_t> &jpegData) {
    if (jpegData.empty()) {
        return;
    }

    DirectX::ScratchImage scratch;
    DirectX::TexMetadata metadata{};
    HRESULT hr = DirectX::LoadFromWICMemory(
        jpegData.data(), jpegData.size(), DirectX::WIC_FLAGS_FORCE_RGB,
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

void TitleScene::UploadPreviewTextureIfNeeded() {
    if (!previewFrame_.dirty || !previewFrame_.valid ||
        previewFrame_.rgbaPixels.empty()) {
        return;
    }

    if (ctx_->texture->UpdateDynamicTexture(
            previewFrame_.textureId, previewFrame_.rgbaPixels.data(),
            previewFrame_.width, previewFrame_.height)) {
        previewFrame_.dirty = false;
    }
}

XMFLOAT4 TitleScene::HandColor(size_t handIndex, float alpha) const {
    if (handIndex == 0) {
        return MakeColor(0.10f, 0.72f, 1.0f, alpha);
    }
    return MakeColor(0.22f, 1.0f, 0.48f, alpha);
}
