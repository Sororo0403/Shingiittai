#include <WinSock2.h>
#include <WS2tcpip.h>
#include "HandRegistrationScene.h"
#include "DirectXCommon.h"
#include "PostEffectRenderer.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "TipScene.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#include <memory>

using namespace DirectX;

namespace {
constexpr float kRegistrationHoldTime = 0.55f;
constexpr float kClearHoldTime = 0.22f;
constexpr float kCommandRetryInterval = 0.45f;
constexpr uint32_t kCameraPreviewWidth = 320;
constexpr uint32_t kCameraPreviewHeight = 240;

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}
} // namespace

void HandRegistrationScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    registrationTimer_ = 0.0f;
    clearTimer_ = 0.0f;
    commandRetryTimer_ = 0.0f;
    finished_ = false;
    stage_ = RegistrationStage::RegisterLeft;
    handPreviewCenters_.fill({0.5f, 0.5f});
    handPreviewVisible_.fill(false);
    registeredCenters_.fill({0.5f, 0.5f});
    registeredVisible_.fill(false);

    if (ctx_->requestHandTrackingStart) {
        ctx_->requestHandTrackingStart();
    }
    SendHandRegistrationReset();
    SendHandRegistrationSlot(1);

    ctx_->dxCommon->BeginUpload();
    backgroundImage_ =
        LoadTextureImage(L"app/resources/select/weapon_select_bg.png");
    cameraPreviewTextureId_ =
        ctx_->texture->CreateDynamicTexture(kCameraPreviewWidth,
                                            kCameraPreviewHeight);
    ctx_->dxCommon->EndUpload();
    ctx_->texture->ReleaseUploadBuffers();

    ctx_->postEffectRenderer->ResetEffects();
    ctx_->postEffectRenderer->SetVignettingEnabled(true);
    ctx_->postEffectRenderer->SetVignettingStrength(0.24f);
}

void HandRegistrationScene::Update() {
    sceneTime_ += ctx_->deltaTime;

    handController_.Update(ctx_->deltaTime);
    cameraPreviewReceiver_.Update();
    commandRetryTimer_ += ctx_->deltaTime;

    for (size_t i = 0; i < handPreviewCenters_.size(); ++i) {
        float x = handPreviewCenters_[i].x;
        float y = handPreviewCenters_[i].y;
        handPreviewVisible_[i] = handController_.GetHandCenter(i, x, y);
        if (handPreviewVisible_[i]) {
            handPreviewCenters_[i] = {x, y};
        }
    }

    switch (stage_) {
    case RegistrationStage::RegisterLeft:
        if (commandRetryTimer_ >= kCommandRetryInterval) {
            SendHandRegistrationSlot(1);
            commandRetryTimer_ = 0.0f;
        }
        if (IsOnlyHandVisible(1)) {
            registrationTimer_ += ctx_->deltaTime;
            if (registrationTimer_ >= kRegistrationHoldTime) {
                registeredCenters_[1] = handPreviewCenters_[1];
                registeredVisible_[1] = true;
                registrationTimer_ = 0.0f;
                clearTimer_ = 0.0f;
                commandRetryTimer_ = 0.0f;
                stage_ = RegistrationStage::WaitForClear;
            }
        } else {
            registrationTimer_ = 0.0f;
        }
        break;
    case RegistrationStage::WaitForClear:
        if (AreHandsClear()) {
            clearTimer_ += ctx_->deltaTime;
            if (clearTimer_ >= kClearHoldTime) {
                clearTimer_ = 0.0f;
                registrationTimer_ = 0.0f;
                stage_ = RegistrationStage::RegisterRight;
                SendHandRegistrationSlot(0);
                commandRetryTimer_ = 0.0f;
            }
        } else {
            clearTimer_ = 0.0f;
        }
        break;
    case RegistrationStage::RegisterRight:
        if (commandRetryTimer_ >= kCommandRetryInterval) {
            SendHandRegistrationSlot(0);
            commandRetryTimer_ = 0.0f;
        }
        if (IsOnlyHandVisible(0)) {
            registrationTimer_ += ctx_->deltaTime;
            if (registrationTimer_ >= kRegistrationHoldTime) {
                registeredCenters_[0] = handPreviewCenters_[0];
                registeredVisible_[0] = true;
                stage_ = RegistrationStage::Done;
                FinishRegistration();
            }
        } else {
            registrationTimer_ = 0.0f;
        }
        break;
    case RegistrationStage::Done:
        break;
    }
}

void HandRegistrationScene::Draw() {
    const float w = static_cast<float>(ctx_->winApp->GetWidth());
    const float h = static_cast<float>(ctx_->winApp->GetHeight());

    uint32_t previewWidth = 0;
    uint32_t previewHeight = 0;
    if (cameraPreviewReceiver_.ConsumeFrame(
            cameraPreviewPixels_, previewWidth, previewHeight) &&
        previewWidth == kCameraPreviewWidth &&
        previewHeight == kCameraPreviewHeight) {
        hasCameraPreviewFrame_ = ctx_->texture->UpdateDynamicTexture(
            cameraPreviewTextureId_, cameraPreviewPixels_.data(), previewWidth,
            previewHeight);
    }

    ctx_->sprite->PreDraw();
    DrawBackground(w, h);
    DrawCameraPreview(w, h);
    DrawProgress(w, h);
    DrawHandStatus(w, h);
    ctx_->sprite->PostDraw();
}

void HandRegistrationScene::DrawOverlay() {
    ctx_->sprite->PreDraw();
    DrawCameraBadge();
    ctx_->sprite->PostDraw();
}

HandRegistrationScene::Image
HandRegistrationScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->texture->Load(path);
    image.width = static_cast<float>(ctx_->texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->texture->GetHeight(image.textureId));
    return image;
}

void HandRegistrationScene::SendHandRegistrationReset() {
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return;
    }

    SOCKET udpSocket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        WSACleanup();
        return;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(5007);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

    constexpr char kCommand[] = "SGREGISTER_RESET";
    sendto(udpSocket, kCommand, static_cast<int>(sizeof(kCommand) - 1), 0,
           reinterpret_cast<sockaddr *>(&address), sizeof(address));
    closesocket(udpSocket);
    WSACleanup();
}

void HandRegistrationScene::SendHandRegistrationSlot(size_t handIndex) {
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return;
    }

    SOCKET udpSocket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        WSACleanup();
        return;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(5007);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

    const std::string command =
        "SGREGISTER_SLOT " + std::to_string(handIndex);
    sendto(udpSocket, command.c_str(), static_cast<int>(command.size()), 0,
           reinterpret_cast<sockaddr *>(&address), sizeof(address));
    closesocket(udpSocket);
    WSACleanup();
}

void HandRegistrationScene::FinishRegistration() {
    finished_ = true;
    inputCalibration_.controlType = InputControlType::Hand;

    bool hasHandNeutral = false;
    for (size_t i = 0; i < inputCalibration_.handNeutral.size(); ++i) {
        if (registeredVisible_[i]) {
            inputCalibration_.handNeutral[i] = registeredCenters_[i];
            hasHandNeutral = true;
        }
    }
    inputCalibration_.hasHandNeutral = hasHandNeutral;
    inputCalibration_.hasHandRestSpeed = false;

    sceneManager_->ChangeScene(std::make_unique<TipScene>(inputCalibration_));
}

bool HandRegistrationScene::IsOnlyHandVisible(size_t handIndex) const {
    if (handIndex >= handPreviewVisible_.size() ||
        !handPreviewVisible_[handIndex]) {
        return false;
    }

    for (size_t i = 0; i < handPreviewVisible_.size(); ++i) {
        if (i != handIndex && handPreviewVisible_[i]) {
            return false;
        }
    }
    return true;
}

bool HandRegistrationScene::AreHandsClear() const {
    for (bool visible : handPreviewVisible_) {
        if (visible) {
            return false;
        }
    }
    return true;
}

void HandRegistrationScene::DrawBackground(float screenWidth,
                                           float screenHeight) {
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.018f, 0.020f, 0.024f, 1.0f));
    DrawImage(backgroundImage_, 0.0f, 0.0f,
              (std::max)(screenWidth / (std::max)(backgroundImage_.width, 1.0f),
                         screenHeight /
                             (std::max)(backgroundImage_.height, 1.0f)),
              0.32f);
}

void HandRegistrationScene::DrawCameraPreview(float screenWidth,
                                              float screenHeight) {
    const float previewW = (std::min)(screenWidth * 0.58f, 540.0f);
    const float previewH = previewW * 0.75f;
    const float x = (screenWidth - previewW) * 0.5f;
    const float y = screenHeight * 0.17f;
    const float pulse = 0.5f + 0.5f * std::sinf(sceneTime_ * 5.2f);

    DrawRect(x - 8.0f, y - 8.0f, previewW + 16.0f, previewH + 16.0f,
             Color(0.015f, 0.018f, 0.024f, 0.94f));
    DrawRect(x, y, previewW, previewH, Color(0.035f, 0.040f, 0.050f, 0.96f));

    if (hasCameraPreviewFrame_) {
        Sprite preview{};
        preview.position = {x, y};
        preview.size = {previewW, previewH};
        preview.color = {1.0f, 1.0f, 1.0f, 0.96f};
        preview.textureId = cameraPreviewTextureId_;
        ctx_->sprite->DrawSprite(preview);
        DrawRect(x, y, previewW, previewH, Color(0.02f, 0.04f, 0.06f, 0.16f));
    }

    for (size_t i = 0; i < handPreviewCenters_.size(); ++i) {
        const bool visible = handPreviewVisible_[i];
        const XMFLOAT2 center = handPreviewCenters_[i];
        const float handX = x + center.x * previewW;
        const float handY = y + center.y * previewH;
        const XMFLOAT4 color =
            i == 0 ? Color(0.20f, 1.0f, 0.42f, visible ? 0.92f : 0.24f)
                   : Color(0.18f, 0.62f, 1.0f, visible ? 0.92f : 0.24f);
        const float size = visible ? 34.0f + pulse * 6.0f : 20.0f;
        DrawRect(handX - size * 0.5f, handY - 3.0f, size, 6.0f, color);
        DrawRect(handX - 3.0f, handY - size * 0.5f, 6.0f, size, color);
        DrawRect(handX - 8.0f, handY - 8.0f, 16.0f, 16.0f, color);
    }
}

void HandRegistrationScene::DrawProgress(float screenWidth,
                                         float screenHeight) {
    const float progress =
        stage_ == RegistrationStage::WaitForClear
            ? std::clamp(clearTimer_ / kClearHoldTime, 0.0f, 1.0f)
            : std::clamp(registrationTimer_ / kRegistrationHoldTime, 0.0f,
                         1.0f);
    const float barW = screenWidth * 0.58f;
    const float barH = 18.0f;
    const float x = (screenWidth - barW) * 0.5f;
    const float y = screenHeight * 0.76f;

    DrawRect(x, y, barW, barH, Color(0.05f, 0.06f, 0.07f, 0.92f));
    DrawRect(x, y, barW * progress, barH, Color(0.20f, 1.0f, 0.42f, 0.96f));
    DrawRect(x, y + barH + 5.0f, barW, 4.0f,
             Color(0.10f, 0.54f, 1.0f, 0.72f));
}

void HandRegistrationScene::DrawHandStatus(float screenWidth,
                                           float screenHeight) {
    const float panelW = screenWidth * 0.58f;
    const float x = (screenWidth - panelW) * 0.5f;
    const float y = screenHeight * 0.84f;
    const float segmentW = panelW / 2.0f - 8.0f;

    for (int i = 0; i < 2; ++i) {
        const bool isCurrent =
            (stage_ == RegistrationStage::RegisterRight && i == 0) ||
            (stage_ == RegistrationStage::RegisterLeft && i == 1);
        const XMFLOAT4 color =
            registeredVisible_[static_cast<size_t>(i)]
                ? Color(0.10f, 0.74f, 0.36f, 0.92f)
                : isCurrent ? Color(1.0f, 0.82f, 0.18f, 0.92f)
                            : Color(0.20f, 0.24f, 0.28f, 0.88f);
        const float extraH = isCurrent ? 8.0f : 0.0f;
        DrawRect(x + static_cast<float>(i) * (segmentW + 16.0f),
                 y - extraH * 0.5f, segmentW, 12.0f + extraH, color);
    }
}

void HandRegistrationScene::DrawCameraBadge() {
    const float x = 22.0f;
    const float y = 22.0f;
    const float pulse = 0.50f + 0.50f * std::sinf(sceneTime_ * 6.0f);
    DrawRect(x, y, 68.0f, 42.0f, Color(0.02f, 0.025f, 0.032f, 0.78f));
    DrawRect(x + 12.0f, y + 15.0f, 28.0f, 17.0f,
             Color(0.90f, 0.94f, 1.0f, 0.94f));
    DrawRect(x + 17.0f, y + 9.0f, 14.0f, 7.0f,
             Color(0.90f, 0.94f, 1.0f, 0.94f));
    DrawRect(x + 21.0f, y + 18.0f, 10.0f, 9.0f,
             Color(0.07f, 0.11f, 0.17f, 0.94f));
    DrawRect(x + 45.0f, y + 18.0f, 8.0f, 8.0f,
             Color(0.20f, 1.0f, 0.42f, 0.58f + pulse * 0.42f));
}

void HandRegistrationScene::DrawRect(float x, float y, float w, float h,
                                     const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->sprite->DrawSprite(sprite);
}

void HandRegistrationScene::DrawImage(const Image &image, float x, float y,
                                      float scale, float alpha) {
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
