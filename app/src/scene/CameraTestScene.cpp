#include "CameraTestScene.h"
#include "DirectXCommon.h"
#include "GameScene.h"
#include "Input.h"
#include "PostEffectRenderer.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "TitleScene.h"
#include "WinApp.h"
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <memory>

using namespace DirectX;

namespace {
XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }
} // namespace

void CameraTestScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    handCenters_.fill({0.5f, 0.5f});
    handActive_.fill(false);
    handSpeeds_.fill(0.0f);

    if (ctx_->requestHandTrackingStart) {
        ctx_->requestHandTrackingStart();
    }

    ctx_->dxCommon->BeginUpload();
    previewTextureId_ =
        ctx_->texture->CreateDynamicTexture(kPreviewWidth, kPreviewHeight);
    ctx_->dxCommon->EndUpload();
    ctx_->texture->ReleaseUploadBuffers();

    ctx_->postEffectRenderer->ResetEffects();
    ctx_->postEffectRenderer->SetVignettingEnabled(false);
}

void CameraTestScene::Update() {
    sceneTime_ += ctx_->deltaTime;
    handController_.Update(ctx_->deltaTime);
    cameraPreviewReceiver_.Update();
    UpdateInput(ctx_->input);
    UpdateTrails();
}

void CameraTestScene::Draw() {
    const float w = static_cast<float>(ctx_->winApp->GetWidth());
    const float h = static_cast<float>(ctx_->winApp->GetHeight());
    Layout(w, h);
    UpdatePreviewTexture();

    ctx_->sprite->PreDraw();
    DrawBackground(w, h);
    DrawPreview();
    DrawTrackingOverlay();
    DrawStatusBars(w, h);
    ctx_->sprite->PostDraw();
}

void CameraTestScene::UpdateInput(Input *input) {
    const bool gamepadBack =
        input->IsGamepadConnected() &&
        (input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_B) ||
         input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_BACK));
    if (input->IsKeyTrigger(DIK_TAB) || input->IsKeyTrigger(DIK_ESCAPE) ||
        input->IsKeyTrigger(DIK_F2) || gamepadBack) {
        ReturnToTitle();
    }
}

void CameraTestScene::UpdateTrails() {
    for (size_t i = 0; i < kHandCount; ++i) {
        float x = handCenters_[i].x;
        float y = handCenters_[i].y;
        handActive_[i] = handController_.GetHandCenter(i, x, y);
        handSpeeds_[i] = handController_.GetRawMotionSpeed(i);
        if (handActive_[i]) {
            handCenters_[i] = {x, y};
            trails_[i].insert(trails_[i].begin(), {{x, y}, 0.0f});
        }

        for (TrailSample &sample : trails_[i]) {
            sample.age += ctx_->deltaTime;
        }
        trails_[i].erase(
            std::remove_if(trails_[i].begin(), trails_[i].end(),
                           [](const TrailSample &sample) {
                               return sample.age > kTrailSeconds;
                           }),
            trails_[i].end());
        if (trails_[i].size() > kMaxTrailSamples) {
            trails_[i].resize(kMaxTrailSamples);
        }
    }
}

void CameraTestScene::UpdatePreviewTexture() {
    uint32_t previewWidth = 0;
    uint32_t previewHeight = 0;
    if (cameraPreviewReceiver_.ConsumeFrame(previewPixels_, previewWidth,
                                            previewHeight) &&
        previewWidth == kPreviewWidth && previewHeight == kPreviewHeight) {
        hasPreviewFrame_ = ctx_->texture->UpdateDynamicTexture(
            previewTextureId_, previewPixels_.data(), previewWidth,
            previewHeight);
    }
}

void CameraTestScene::Layout(float screenWidth, float screenHeight) {
    const float maxW = screenWidth - 96.0f;
    const float maxH = screenHeight - 154.0f;
    const float scale = (std::max)(
        1.0f, (std::min)(maxW / static_cast<float>(kPreviewWidth),
                         maxH / static_cast<float>(kPreviewHeight)));
    const float previewW = static_cast<float>(kPreviewWidth) * scale;
    const float previewH = static_cast<float>(kPreviewHeight) * scale;
    previewRect_ = {(screenWidth - previewW) * 0.5f, 48.0f, previewW,
                    previewH};
}

void CameraTestScene::ReturnToTitle() {
    sceneManager_->ChangeScene(std::make_unique<TitleScene>());
}

XMFLOAT2 CameraTestScene::PreviewPoint(const XMFLOAT2 &point) const {
    return {previewRect_.x + Clamp01(point.x) * previewRect_.w,
            previewRect_.y + Clamp01(point.y) * previewRect_.h};
}

XMFLOAT4 CameraTestScene::HandColor(size_t handIndex, float alpha) const {
    if (handIndex == 0) {
        return Color(0.10f, 0.56f, 1.0f, alpha);
    }
    return Color(0.10f, 0.92f, 0.46f, alpha);
}

void CameraTestScene::DrawBackground(float screenWidth, float screenHeight) {
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.015f, 0.017f, 0.020f, 1.0f));
    DrawRect(0.0f, 0.0f, screenWidth, 36.0f,
             Color(0.10f, 0.56f, 1.0f, 0.62f));
    DrawRect(0.0f, screenHeight - 54.0f, screenWidth, 54.0f,
             Color(0.02f, 0.024f, 0.030f, 1.0f));
}

void CameraTestScene::DrawPreview() {
    DrawRect(previewRect_.x - 8.0f, previewRect_.y - 8.0f,
             previewRect_.w + 16.0f, previewRect_.h + 16.0f,
             Color(0.92f, 0.96f, 1.0f, 0.18f));
    DrawRect(previewRect_.x - 2.0f, previewRect_.y - 2.0f,
             previewRect_.w + 4.0f, previewRect_.h + 4.0f,
             Color(0.92f, 0.96f, 1.0f, 0.72f));

    if (hasPreviewFrame_) {
        DrawImage(previewTextureId_, previewRect_.x, previewRect_.y,
                  previewRect_.w, previewRect_.h,
                  Color(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
        DrawRect(previewRect_.x, previewRect_.y, previewRect_.w,
                 previewRect_.h, Color(0.045f, 0.050f, 0.060f, 1.0f));
        const float pulse = 0.35f + 0.25f * std::sinf(sceneTime_ * 5.0f);
        DrawRect(previewRect_.x + previewRect_.w * 0.30f,
                 previewRect_.y + previewRect_.h * 0.50f,
                 previewRect_.w * 0.40f, 8.0f,
                 Color(0.10f, 0.56f, 1.0f, pulse));
    }
}

void CameraTestScene::DrawTrackingOverlay() {
    for (size_t handIndex = 0; handIndex < kHandCount; ++handIndex) {
        for (const TrailSample &sample : trails_[handIndex]) {
            const float life = 1.0f - Clamp01(sample.age / kTrailSeconds);
            const XMFLOAT2 p = PreviewPoint(sample.point);
            const float size = 5.0f + life * 11.0f;
            DrawRect(p.x - size * 0.5f, p.y - size * 0.5f, size, size,
                     HandColor(handIndex, 0.14f + life * 0.46f));
        }
    }

    for (size_t handIndex = 0; handIndex < kHandCount; ++handIndex) {
        DrawHandMarker(handIndex, handCenters_[handIndex],
                       handSpeeds_[handIndex], handActive_[handIndex]);
    }

    const XMFLOAT2 a = PreviewPoint(handCenters_[0]);
    const XMFLOAT2 b = PreviewPoint(handCenters_[1]);
    if (handActive_[0] && handActive_[1]) {
        const float left = (std::min)(a.x, b.x);
        const float top = (std::min)(a.y, b.y);
        const float right = (std::max)(a.x, b.x);
        const float bottom = (std::max)(a.y, b.y);
        const bool close =
            std::abs(handCenters_[0].x - handCenters_[1].x) < 0.12f;
        DrawRect(left, top, right - left + 1.0f, 2.0f,
                 Color(1.0f, 0.92f, 0.18f, close ? 0.86f : 0.34f));
        DrawRect(left, bottom, right - left + 1.0f, 2.0f,
                 Color(1.0f, 0.92f, 0.18f, close ? 0.86f : 0.34f));
        DrawRect(left, top, 2.0f, bottom - top + 1.0f,
                 Color(1.0f, 0.92f, 0.18f, close ? 0.86f : 0.34f));
        DrawRect(right, top, 2.0f, bottom - top + 1.0f,
                 Color(1.0f, 0.92f, 0.18f, close ? 0.86f : 0.34f));
    }
}

void CameraTestScene::DrawHandMarker(size_t handIndex, const XMFLOAT2 &point,
                                     float speed, bool active) {
    const XMFLOAT2 p = PreviewPoint(point);
    const float pulse =
        active ? 1.0f + (std::min)(speed * 0.050f, 18.0f) : 1.0f;
    const float outer = 34.0f + pulse;
    const float inner = 15.0f + pulse * 0.35f;
    const XMFLOAT4 color = HandColor(handIndex, active ? 0.95f : 0.18f);
    const XMFLOAT4 soft = HandColor(handIndex, active ? 0.28f : 0.08f);

    DrawRect(p.x - outer * 0.5f, p.y - 3.0f, outer, 6.0f, soft);
    DrawRect(p.x - 3.0f, p.y - outer * 0.5f, 6.0f, outer, soft);
    DrawRect(p.x - inner * 0.5f, p.y - inner * 0.5f, inner, inner, color);

    const float slotX = p.x + (handIndex == 0 ? -30.0f : 18.0f);
    const float slotY = p.y - 34.0f;
    DrawRect(slotX, slotY, 22.0f, 22.0f, color);
    if (handIndex == 0) {
        DrawRect(slotX + 6.0f, slotY + 5.0f, 10.0f, 12.0f,
                 Color(0.015f, 0.017f, 0.020f, 0.96f));
    } else {
        DrawRect(slotX + 5.0f, slotY + 5.0f, 12.0f, 4.0f,
                 Color(0.015f, 0.017f, 0.020f, 0.96f));
        DrawRect(slotX + 9.0f, slotY + 9.0f, 4.0f, 8.0f,
                 Color(0.015f, 0.017f, 0.020f, 0.96f));
    }
}

void CameraTestScene::DrawStatusBars(float screenWidth, float screenHeight) {
    const float baseY = screenHeight - 40.0f;
    const float barW = (screenWidth - 164.0f) * 0.5f;
    for (size_t i = 0; i < kHandCount; ++i) {
        const float x = 52.0f + static_cast<float>(i) * (barW + 60.0f);
        DrawRect(x, baseY, barW, 14.0f, Color(1.0f, 1.0f, 1.0f, 0.12f));
        DrawRect(x, baseY, handActive_[i] ? barW : 18.0f, 14.0f,
                 HandColor(i, handActive_[i] ? 0.90f : 0.24f));
        DrawRect(x, baseY - 18.0f,
                 (std::min)(barW, handSpeeds_[i] * barW * 0.018f), 8.0f,
                 Color(1.0f, 0.92f, 0.18f, handActive_[i] ? 0.86f : 0.18f));
    }
}

void CameraTestScene::DrawRect(float x, float y, float w, float h,
                               const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->sprite->DrawSprite(sprite);
}

void CameraTestScene::DrawImage(uint32_t textureId, float x, float y, float w,
                                float h, const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = textureId;
    ctx_->sprite->DrawSprite(sprite);
}
