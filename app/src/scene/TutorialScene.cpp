#include "TutorialScene.h"
#include "AppSceneServices.h"
#include "Input.h"
#include "SceneManager.h"
#include "Sprite.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "TutorialSelectScene.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#include <memory>

using namespace DirectX;

namespace {
constexpr uint16_t kPreviewPort = 5006;
constexpr float kPreviewStaleSeconds = 0.75f;
constexpr float kTransitionDuration = 0.20f;
constexpr float kIntroDuration = 0.36f;

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float SmoothStep(float t) {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}
} // namespace

TutorialScene::TutorialScene(const SwordInputCalibration &inputCalibration)
    : inputCalibration_(inputCalibration) {}

TutorialScene::~TutorialScene() { previewReceiver_.Close(); }

void TutorialScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    transitionTimer_ = 0.0f;
    returnRequested_ = false;
    startGameRequested_ = false;
    handTrackingStartRequested_ = false;

    backgroundScene_ =
        std::make_unique<GameScene>(GameScene::Mode::TutorialBackgroundOnly);
    backgroundScene_->Initialize(ctx);

    if (IsHandTutorial()) {
        handController_.SetCalibration(inputCalibration_);
        RequestHandTrackingStartOnce();
        previewReceiver_.Initialize(ctx_->rendering.texture, kPreviewPort);
        titleImage_ =
            LoadTextureImage(L"app/resources/ui/tutorial/text/title_hand.png");
        bodyImages_[0] =
            LoadTextureImage(L"app/resources/ui/tutorial/text/body_hand_0.png");
        bodyImages_[1] =
            LoadTextureImage(L"app/resources/ui/tutorial/text/body_hand_1.png");
    } else {
        titleImage_ =
            LoadTextureImage(L"app/resources/ui/tutorial/text/title_kbm.png");
        bodyImages_[0] =
            LoadTextureImage(L"app/resources/ui/tutorial/text/body_kbm_0.png");
        bodyImages_[1] =
            LoadTextureImage(L"app/resources/ui/tutorial/text/body_kbm_1.png");
    }
    promptImage_ =
        LoadTextureImage(L"app/resources/ui/tutorial/text/prompt.png");
}

void TutorialScene::Update() {
    sceneTime_ += ctx_->frame.deltaTime;
    if (backgroundScene_) {
        backgroundScene_->Update();
    }
    if (IsHandTutorial()) {
        RequestHandTrackingStartOnce();
        handController_.Update(ctx_->frame.deltaTime);
        previewReceiver_.Update(ctx_->frame.deltaTime);
    }

    if (returnRequested_ || startGameRequested_) {
        transitionTimer_ += ctx_->frame.deltaTime;
        if (transitionTimer_ >= kTransitionDuration) {
            if (returnRequested_) {
                sceneManager_->ChangeScene(
                    std::make_unique<TutorialSelectScene>());
            } else {
                sceneManager_->ChangeScene(
                    std::make_unique<GameScene>(inputCalibration_));
            }
        }
        return;
    }

    Input *input = ctx_->systems.input;
    if (input != nullptr && input->IsKeyTrigger(DIK_ESCAPE)) {
        returnRequested_ = true;
        transitionTimer_ = 0.0f;
        return;
    }
    if (input != nullptr &&
        (input->IsKeyTrigger(DIK_SPACE) || input->IsKeyTrigger(DIK_RETURN))) {
        startGameRequested_ = true;
        transitionTimer_ = 0.0f;
    }
}

void TutorialScene::Draw() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    if (backgroundScene_) {
        backgroundScene_->Draw();
    }

    const float intro = SmoothStep(sceneTime_ / kIntroDuration);
    const float content = SmoothStep((sceneTime_ - 0.12f) / kIntroDuration);
    const float panelW = std::clamp(w * 0.62f, 760.0f, 1100.0f);
    const float panelH = std::clamp(h * 0.48f, 360.0f, 500.0f);
    const float panelX = (w - panelW) * 0.5f;
    const float panelY = (h - panelH) * 0.5f;

    ctx_->rendering.sprite->PreDraw();
    DrawRect(0.0f, 0.0f, w, h, Color(0.0f, 0.0f, 0.0f, 0.58f * intro));
    DrawRect(panelX + 10.0f, panelY + 12.0f, panelW, panelH,
             Color(0.0f, 0.0f, 0.0f, 0.30f * intro));
    DrawRect(panelX, panelY, panelW, panelH,
             Color(0.010f, 0.035f, 0.036f, 0.78f * intro));
    DrawRect(panelX, panelY, panelW * intro, 6.0f,
             Color(0.02f, 0.95f, 0.84f, 0.90f * intro));
    DrawRect(panelX, panelY + panelH - 6.0f, panelW * intro, 6.0f,
             Color(0.02f, 0.80f, 0.74f, 0.46f * intro));
    DrawRect(panelX, panelY, 6.0f, panelH * intro,
             Color(0.02f, 0.95f, 0.84f, 0.82f * intro));
    DrawRect(panelX + panelW - 6.0f, panelY + panelH - panelH * intro, 6.0f,
             panelH * intro, Color(0.02f, 0.80f, 0.74f, 0.40f * intro));

    const float titleScale =
        (std::min)(1.0f,
                   (panelW - 130.0f) / (std::max)(titleImage_.width, 1.0f));
    DrawImage(titleImage_,
              panelX + (panelW - titleImage_.width * titleScale) * 0.5f,
              panelY + panelH * 0.18f - titleImage_.height * titleScale * 0.5f,
              titleScale, content);

    for (size_t i = 0; i < bodyImages_.size(); ++i) {
        const Image &body = bodyImages_[i];
        const float bodyScale =
            (std::min)(1.0f, (panelW - 160.0f) / (std::max)(body.width, 1.0f));
        const float lineY =
            panelY + panelH * (0.40f + static_cast<float>(i) * 0.16f);
        DrawImage(body, panelX + (panelW - body.width * bodyScale) * 0.5f,
                  lineY - body.height * bodyScale * 0.5f, bodyScale, content);
    }

    const float pulse = 0.70f + 0.30f * std::sinf(sceneTime_ * 5.0f);
    const float promptScale =
        (std::min)(1.0f,
                   (panelW - 180.0f) / (std::max)(promptImage_.width, 1.0f));
    DrawImage(promptImage_,
              panelX + (panelW - promptImage_.width * promptScale) * 0.5f,
              panelY + panelH * 0.78f -
                  promptImage_.height * promptScale * 0.5f,
              promptScale, pulse * content);

    if (returnRequested_ || startGameRequested_) {
        const float fade = SmoothStep(transitionTimer_ / kTransitionDuration);
        DrawRect(0.0f, 0.0f, w, h, Color(0.0f, 0.0f, 0.0f, fade));
    }
    ctx_->rendering.sprite->PostDraw();
}

void TutorialScene::DrawTransparent() {
    if (!IsHandTutorial() || returnRequested_ || startGameRequested_ ||
        sceneTime_ < 0.36f) {
        return;
    }
    previewReceiver_.Draw(ctx_->rendering.sprite, ctx_->rendering.texture,
                          kPreviewStaleSeconds);
}

TutorialScene::Image TutorialScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width =
        static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
    return image;
}

bool TutorialScene::IsHandTutorial() const {
    return inputCalibration_.controlType == InputControlType::Hand;
}

bool TutorialScene::RequestHandTrackingStartOnce() {
    if (handTrackingStartRequested_ ||
        !AppSceneServices::HasHandTrackingStart()) {
        return handTrackingStartRequested_;
    }
    handTrackingStartRequested_ = AppSceneServices::RequestHandTrackingStart();
    return handTrackingStartRequested_;
}

void TutorialScene::DrawRect(float x, float y, float w, float h,
                             const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void TutorialScene::DrawImage(const Image &image, float x, float y, float scale,
                              float alpha) {
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
