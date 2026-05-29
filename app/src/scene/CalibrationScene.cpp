#include "CalibrationScene.h"
#include "DirectXCommon.h"
#include "PostEffectManager.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "DifficultyCauldronScene.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#include <memory>

using namespace DirectX;

namespace {
constexpr float kRequiredStillTime = 3.0f;

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}
} // namespace

CalibrationScene::CalibrationScene(InputControlType controlType)
    : controlType_(controlType) {}

void CalibrationScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    stableTimer_ = 0.0f;
    finished_ = false;

    backgroundImage_ =
        LoadTextureImage(L"app/resources/ui/weapon_select/weapon_select_bg.png");
    for (int i = 0; i < 10; ++i) {
        digitImages_[static_cast<size_t>(i)] =
            LoadTextureImage(L"app/resources/ui/result/glyphs/char_" +
                             std::to_wstring(i) + L".png");
    }

    PostProcessProfile postProfile{};
    postProfile.vignette.enabled = true;
    postProfile.vignette.strength = 0.24f;
    ctx_->rendering.postEffectManager->SetBaseProfile(postProfile);
}

void CalibrationScene::Update() {
    sceneTime_ += ctx_->frame.deltaTime;
    if (!finished_) {
        stableTimer_ += ctx_->frame.deltaTime;
    }

    if (!finished_ && stableTimer_ >= kRequiredStillTime) {
        FinishCalibration();
    }
}

void CalibrationScene::Draw() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    ctx_->rendering.sprite->PreDraw();
    DrawBackground(w, h);
    DrawProgress(w, h);
    ctx_->rendering.sprite->PostDraw();
}

void CalibrationScene::DrawTransparent() {}

CalibrationScene::Image
CalibrationScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width = static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
    return image;
}

void CalibrationScene::FinishCalibration() {
    finished_ = true;
    inputCalibration_.controlType = controlType_;

    inputCalibration_.hasHandNeutral = false;
    inputCalibration_.hasHandRestSpeed = false;

    sceneManager_->ChangeScene(
        std::make_unique<DifficultyCauldronScene>(inputCalibration_));
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

void CalibrationScene::DrawRect(float x, float y, float w, float h,
                                const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->rendering.sprite->DrawSprite(sprite);
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
    ctx_->rendering.sprite->DrawSprite(sprite);
}
