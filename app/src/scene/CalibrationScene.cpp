#include "CalibrationScene.h"
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
constexpr float kRequiredStillTime = 3.0f;
constexpr float kJoyConStableDegreesPerSecond = 18.0f;

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float QuaternionAngularSpeedDeg(const XMFLOAT4 &previous,
                                const XMFLOAT4 &current, float deltaTime) {
    if (deltaTime <= 0.0f) {
        return 0.0f;
    }

    XMVECTOR prev = XMQuaternionNormalize(XMLoadFloat4(&previous));
    XMVECTOR now = XMQuaternionNormalize(XMLoadFloat4(&current));
    float dot = XMVectorGetX(XMQuaternionDot(prev, now));
    dot = std::clamp(std::fabs(dot), 0.0f, 1.0f);
    return XMConvertToDegrees(std::acos(dot) * 2.0f) / deltaTime;
}
} // namespace

CalibrationScene::CalibrationScene(InputControlType controlType)
    : controlType_(controlType) {}

void CalibrationScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    stableTimer_ = 0.0f;
    finished_ = false;

    if (controlType_ == InputControlType::JoyCon) {
        leftJoyCon_.Initialize(true);
        rightJoyCon_.Initialize(false);
        leftJoyCon_.StartCalibration();
        rightJoyCon_.StartCalibration();
    }

    ctx_->dxCommon->BeginUpload();
    backgroundImage_ =
        LoadTextureImage(L"app/resources/select/weapon_select_bg.png");
    for (int i = 0; i < 10; ++i) {
        digitImages_[static_cast<size_t>(i)] =
            LoadTextureImage(L"app/resources/result/char_" +
                             std::to_wstring(i) + L".png");
    }
    ctx_->dxCommon->EndUpload();
    ctx_->texture->ReleaseUploadBuffers();

    ctx_->postEffectRenderer->ResetEffects();
    ctx_->postEffectRenderer->SetVignettingEnabled(true);
    ctx_->postEffectRenderer->SetVignettingStrength(0.24f);
}

void CalibrationScene::Update() {
    sceneTime_ += ctx_->deltaTime;

    if (controlType_ == InputControlType::JoyCon) {
        leftJoyCon_.Update(ctx_->deltaTime);
        rightJoyCon_.Update(ctx_->deltaTime);
        UpdateJoyConStability(ctx_->deltaTime);
    }

    if (IsJoyConStable()) {
        stableTimer_ += ctx_->deltaTime;
    } else {
        stableTimer_ = 0.0f;
    }

    if (!finished_ && stableTimer_ >= kRequiredStillTime) {
        FinishCalibration();
    }
}

void CalibrationScene::Draw() {
    const float w = static_cast<float>(ctx_->winApp->GetWidth());
    const float h = static_cast<float>(ctx_->winApp->GetHeight());

    ctx_->sprite->PreDraw();
    DrawBackground(w, h);
    DrawProgress(w, h);
    DrawStatusBars(w, h);
    ctx_->sprite->PostDraw();
}

void CalibrationScene::DrawOverlay() {}

CalibrationScene::Image
CalibrationScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->texture->Load(path);
    image.width = static_cast<float>(ctx_->texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->texture->GetHeight(image.textureId));
    return image;
}

void CalibrationScene::UpdateJoyConStability(float deltaTime) {
    const auto updateOne = [&](JoyCon &joyCon, bool &hasPrevious,
                               XMFLOAT4 &previous,
                               float &angularSpeed) {
        if (!joyCon.IsConnected()) {
            hasPrevious = false;
            angularSpeed = 0.0f;
            return;
        }

        XMFLOAT4 current{};
        XMStoreFloat4(&current, joyCon.GetRawOrientation());
        if (hasPrevious) {
            angularSpeed =
                QuaternionAngularSpeedDeg(previous, current, deltaTime);
        } else {
            angularSpeed = 0.0f;
            hasPrevious = true;
        }
        previous = current;
    };

    updateOne(leftJoyCon_, hasPrevLeftJoyConOrientation_,
              prevLeftJoyConOrientation_, leftJoyConAngularSpeed_);
    updateOne(rightJoyCon_, hasPrevRightJoyConOrientation_,
              prevRightJoyConOrientation_, rightJoyConAngularSpeed_);
}

void CalibrationScene::FinishCalibration() {
    finished_ = true;
    inputCalibration_.controlType = controlType_;

    const bool hasLeftJoyCon = leftJoyCon_.IsConnected();
    const bool hasRightJoyCon = rightJoyCon_.IsConnected();
    inputCalibration_.resetJoyConBaseOnStart = hasLeftJoyCon || hasRightJoyCon;
    inputCalibration_.hasHandNeutral = false;
    inputCalibration_.hasHandRestSpeed = false;

    sceneManager_->ChangeScene(std::make_unique<TipScene>(inputCalibration_));
}

bool CalibrationScene::IsJoyConStable() const {
    if (controlType_ != InputControlType::JoyCon) {
        return true;
    }

    const bool hasLeftJoyCon = leftJoyCon_.IsConnected();
    const bool hasRightJoyCon = rightJoyCon_.IsConnected();
    if (!hasLeftJoyCon && !hasRightJoyCon) {
        return false;
    }

    bool stable = true;
    if (hasLeftJoyCon) {
        stable = stable && !leftJoyCon_.IsCalibrating() &&
                 leftJoyConAngularSpeed_ <= kJoyConStableDegreesPerSecond;
    }
    if (hasRightJoyCon) {
        stable = stable && !rightJoyCon_.IsCalibrating() &&
                 rightJoyConAngularSpeed_ <= kJoyConStableDegreesPerSecond;
    }
    return stable;
}

void CalibrationScene::DrawBackground(float screenWidth, float screenHeight) {
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.018f, 0.020f, 0.024f, 1.0f));
    DrawImage(backgroundImage_, 0.0f, 0.0f,
              (std::max)(screenWidth / (std::max)(backgroundImage_.width, 1.0f),
                         screenHeight /
                             (std::max)(backgroundImage_.height, 1.0f)),
              0.32f);

    const float cx = screenWidth * 0.5f;
    const float cy = screenHeight * 0.48f;
    const float pulse = 0.5f + 0.5f * std::sinf(sceneTime_ * 5.2f);
    DrawRect(cx - 180.0f, cy - 2.0f, 360.0f, 4.0f,
             Color(1.0f, 0.82f, 0.18f, 0.70f));
    DrawRect(cx - 2.0f, cy - 140.0f, 4.0f, 280.0f,
             Color(1.0f, 0.82f, 0.18f, 0.70f));
    DrawRect(cx - 70.0f - pulse * 8.0f, cy - 70.0f - pulse * 8.0f,
             140.0f + pulse * 16.0f, 5.0f, Color(0.10f, 0.54f, 1.0f, 0.88f));
    DrawRect(cx - 70.0f - pulse * 8.0f, cy + 65.0f + pulse * 8.0f,
             140.0f + pulse * 16.0f, 5.0f, Color(0.10f, 0.54f, 1.0f, 0.88f));
    DrawRect(cx - 70.0f - pulse * 8.0f, cy - 70.0f - pulse * 8.0f, 5.0f,
             140.0f + pulse * 16.0f, Color(0.10f, 0.54f, 1.0f, 0.88f));
    DrawRect(cx + 65.0f + pulse * 8.0f, cy - 70.0f - pulse * 8.0f, 5.0f,
             140.0f + pulse * 16.0f, Color(0.10f, 0.54f, 1.0f, 0.88f));
}

void CalibrationScene::DrawProgress(float screenWidth, float screenHeight) {
    const float progress =
        std::clamp(stableTimer_ / kRequiredStillTime, 0.0f, 1.0f);
    const float barW = screenWidth * 0.58f;
    const float barH = 18.0f;
    const float x = (screenWidth - barW) * 0.5f;
    const float y = screenHeight * 0.76f;
    DrawRect(x, y, barW, barH, Color(0.05f, 0.06f, 0.07f, 0.92f));
    DrawRect(x, y, barW * progress, barH, Color(1.0f, 0.82f, 0.18f, 0.96f));
    DrawRect(x, y + barH + 5.0f, barW, 4.0f,
             Color(0.10f, 0.54f, 1.0f, 0.72f));

    const int countdown = static_cast<int>(std::ceil(
        (std::max)(0.0f, kRequiredStillTime - stableTimer_)));
    const int digit = std::clamp(countdown, 0, 3);
    const Image &image = digitImages_[static_cast<size_t>(digit)];
    const float scale = 1.7f;
    DrawImage(image, screenWidth * 0.5f - image.width * scale * 0.5f,
              screenHeight * 0.13f, scale, 0.94f);
}

void CalibrationScene::DrawStatusBars(float screenWidth, float screenHeight) {
    const float panelW = screenWidth * 0.58f;
    const float x = (screenWidth - panelW) * 0.5f;
    const float y = screenHeight * 0.84f;
    const float segmentW = panelW / 2.0f - 8.0f;

    const bool states[2] = {
        leftJoyCon_.IsConnected() && !leftJoyCon_.IsCalibrating() &&
            leftJoyConAngularSpeed_ <= kJoyConStableDegreesPerSecond,
        rightJoyCon_.IsConnected() && !rightJoyCon_.IsCalibrating() &&
            rightJoyConAngularSpeed_ <= kJoyConStableDegreesPerSecond};

    for (int i = 0; i < 2; ++i) {
        const XMFLOAT4 color =
            states[i] ? Color(0.10f, 0.74f, 0.36f, 0.92f)
                      : Color(0.80f, 0.12f, 0.10f, 0.88f);
        DrawRect(x + static_cast<float>(i) * (segmentW + 16.0f), y, segmentW,
                 12.0f, color);
    }
}

void CalibrationScene::DrawRect(float x, float y, float w, float h,
                                const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->sprite->DrawSprite(sprite);
}

void CalibrationScene::DrawImage(const Image &image, float x, float y,
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
