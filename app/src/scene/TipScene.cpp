#include "TipScene.h"
#include "AppSceneServices.h"
#include "DirectXCommon.h"
#include "GameScene.h"
#include "Input.h"
#include "PostProcessSystem.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WeaponSelectScene.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#include <memory>

using namespace DirectX;

namespace {
constexpr float kHandSwingStartSpeed = 0.78f;
constexpr float kHandSwingResetSpeed = 0.32f;
constexpr int kRequiredHandSwings = 3;
constexpr uint16_t kPreviewPort = 5006;
constexpr float kPreviewStaleSeconds = 0.75f;
constexpr float kTransitionDuration = 0.16f;
constexpr float kStartTransitionDuration = 0.36f;
constexpr float kTipRotateSeconds = 4.8f;
constexpr float kIntroFrameDelay = 0.08f;
constexpr float kIntroFrameDuration = 0.24f;
constexpr float kIntroBackgroundDelay = 0.28f;
constexpr float kIntroBackgroundDuration = 0.36f;
constexpr float kIntroContentDelay = 0.48f;
constexpr float kIntroContentDuration = 0.22f;

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float SmoothStep(float t) {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}

bool IsHandControl(InputControlType controlType) {
    return controlType == InputControlType::Hand;
}
} // namespace

TipScene::TipScene(const SwordInputCalibration &inputCalibration)
    : inputCalibration_(inputCalibration) {}

TipScene::~TipScene() { previewReceiver_.Close(); }

void TipScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    transitionTimer_ = 0.0f;
    handSwingCount_ = 0;
    handSwingArmed_ = true;
    handTrackingStartRequested_ = false;
    returnToSelectRequested_ = false;
    startGameRequested_ = false;

    if (inputCalibration_.controlType == InputControlType::JoyCon) {
        leftJoyCon_.Initialize(true);
        rightJoyCon_.Initialize(false);
    }
    if (IsHandControl(inputCalibration_.controlType)) {
        handController_.SetCalibration(inputCalibration_);
        RequestHandTrackingStartOnce();
        previewReceiver_.Initialize(ctx_->rendering.texture, kPreviewPort);
    }

    backgroundScene_ = std::make_unique<GameScene>(GameScene::Mode::ReadyPreview);
    backgroundScene_->Initialize(ctx);
    bodyImages_[0] =
        LoadTextureImage(L"app/resources/ui/tip/text/tip_body_0.png");
    bodyImages_[1] =
        LoadTextureImage(L"app/resources/ui/tip/text/tip_body_1.png");
    bodyImages_[2] =
        LoadTextureImage(L"app/resources/ui/tip/text/tip_body_2.png");
    switch (inputCalibration_.controlType) {
    case InputControlType::JoyCon:
        promptImage_ = LoadTextureImage(L"app/resources/ui/tip/text/tip_joycon.png");
        break;
    case InputControlType::Hand:
        promptImage_ = LoadTextureImage(L"app/resources/ui/tip/text/tip_hand.png");
        break;
    case InputControlType::KeyboardMouse:
    default:
        promptImage_ = LoadTextureImage(L"app/resources/ui/tip/text/tip_kbm.png");
        break;
    }

    PostProcessProfile postProfile{};
    postProfile.vignette.enabled = true;
    postProfile.vignette.strength = 0.26f;
    ctx_->rendering.postProcessSystem->SetProfile(postProfile);
}

void TipScene::Update() {
    sceneTime_ += ctx_->frame.deltaTime;
    if (backgroundScene_) {
        backgroundScene_->Update();
    }
    if (returnToSelectRequested_) {
        transitionTimer_ += ctx_->frame.deltaTime;
        if (transitionTimer_ >= kTransitionDuration) {
            sceneManager_->ChangeScene(std::make_unique<WeaponSelectScene>());
        }
        return;
    }
    if (startGameRequested_) {
        transitionTimer_ += ctx_->frame.deltaTime;
        if (transitionTimer_ >= kStartTransitionDuration) {
            sceneManager_->ChangeScene(std::make_unique<GameScene>(inputCalibration_));
        }
        return;
    }

    if (inputCalibration_.controlType == InputControlType::JoyCon) {
        leftJoyCon_.Update(ctx_->frame.deltaTime);
        rightJoyCon_.Update(ctx_->frame.deltaTime);
    }
    if (IsHandControl(inputCalibration_.controlType)) {
        RequestHandTrackingStartOnce();
        handController_.Update(ctx_->frame.deltaTime);
        UpdateCameraPreview(ctx_->frame.deltaTime);
    }

    if (ctx_->systems.input != nullptr &&
        ctx_->systems.input->IsKeyTrigger(DIK_ESCAPE)) {
        returnToSelectRequested_ = true;
        transitionTimer_ = 0.0f;
        return;
    }

    if (ShouldStart()) {
        startGameRequested_ = true;
        transitionTimer_ = 0.0f;
    }
}

void TipScene::Draw() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    if (backgroundScene_) {
        backgroundScene_->Draw();
    }

    const float frameBuild =
        SmoothStep((sceneTime_ - kIntroFrameDelay) / kIntroFrameDuration);
    const float backgroundReveal =
        SmoothStep((sceneTime_ - kIntroBackgroundDelay) /
                   kIntroBackgroundDuration);
    const float contentReveal =
        SmoothStep((sceneTime_ - kIntroContentDelay) / kIntroContentDuration);
    const float panelFill =
        SmoothStep((sceneTime_ - (kIntroFrameDelay + 0.10f)) / 0.18f);

    ctx_->rendering.sprite->PreDraw();
    DrawRect(0.0f, 0.0f, w, h,
             Color(0.0f, 0.0f, 0.0f, 1.0f - backgroundReveal * 0.64f));

    const float panelW = std::clamp(w * 0.58f, 720.0f, 1040.0f);
    const float panelH = std::clamp(h * 0.42f, 330.0f, 450.0f);
    const float panelX = (w - panelW) * 0.5f;
    const float panelY = (h - panelH) * 0.5f;
    if (panelFill > 0.0f) {
        DrawRect(panelX + 10.0f, panelY + 12.0f, panelW, panelH,
                 Color(0.0f, 0.0f, 0.0f, 0.30f * panelFill));
        DrawRect(panelX, panelY, panelW, panelH,
                 Color(0.018f, 0.021f, 0.027f, 0.74f * panelFill));
    }
    if (frameBuild > 0.0f) {
        const float topW = panelW * frameBuild;
        const float sideH = panelH * frameBuild;
        DrawRect(panelX, panelY, topW, 6.0f,
                 Color(1.0f, 0.88f, 0.38f, 0.82f));
        DrawRect(panelX + panelW - topW, panelY + panelH - 6.0f, topW, 6.0f,
                 Color(1.0f, 0.76f, 0.22f, 0.36f + 0.20f * frameBuild));
        DrawRect(panelX, panelY, 6.0f, sideH,
                 Color(1.0f, 0.76f, 0.22f, 0.92f));
        DrawRect(panelX + panelW - 6.0f, panelY + panelH - sideH, 6.0f,
                 sideH, Color(1.0f, 0.76f, 0.22f, 0.42f));
    }

    const Image &bodyImage = CurrentTipBodyImage();
    const float cycle = std::fmod(sceneTime_, kTipRotateSeconds);
    const float fadeIn = SmoothStep(cycle / 0.28f);
    const float fadeOut =
        1.0f - SmoothStep((cycle - (kTipRotateSeconds - 0.32f)) / 0.32f);
    const float bodyAlpha = 0.88f * (std::min)(fadeIn, fadeOut) * contentReveal;
    const float bodyScale =
        (std::min)(1.0f, (panelW - 132.0f) / (std::max)(bodyImage.width, 1.0f));
    DrawImage(bodyImage,
              panelX + (panelW - bodyImage.width * bodyScale) * 0.5f,
              panelY + panelH * 0.40f - bodyImage.height * bodyScale * 0.5f,
              bodyScale, bodyAlpha);
    const float pulse = 0.72f + 0.28f * std::sinf(sceneTime_ * 5.0f);
    const float promptScale =
        (std::min)(1.0f,
                   (panelW - 150.0f) / (std::max)(promptImage_.width, 1.0f));
    DrawImage(promptImage_,
              panelX + (panelW - promptImage_.width * promptScale) * 0.5f,
              panelY + panelH * 0.68f - promptImage_.height * promptScale * 0.5f,
              promptScale, pulse * contentReveal);
    if (IsHandControl(inputCalibration_.controlType)) {
        const float unit = 48.0f;
        const float totalW = unit * 3.0f + 18.0f * 2.0f;
        const float startX = panelX + (panelW - totalW) * 0.5f;
        for (int i = 0; i < 3; ++i) {
            DrawRect(startX + static_cast<float>(i) * (unit + 18.0f),
                     panelY + panelH * 0.88f, unit, 10.0f,
                     i < handSwingCount_
                         ? Color(0.10f, 0.74f, 0.36f, contentReveal)
                         : Color(0.20f, 0.24f, 0.28f, contentReveal));
        }
    }
    const float outroBlack =
        returnToSelectRequested_
            ? SmoothStep(transitionTimer_ / kTransitionDuration)
            : 0.0f;
    const float startBlack =
        startGameRequested_
            ? SmoothStep(transitionTimer_ / kStartTransitionDuration)
            : 0.0f;
    const float fadeAlpha =
        (std::max)(outroBlack, startBlack);
    if (fadeAlpha > 0.0f) {
        DrawRect(0.0f, 0.0f, w, h, Color(0.0f, 0.0f, 0.0f, fadeAlpha));
    }
    ctx_->rendering.sprite->PostDraw();
}

void TipScene::DrawTransparent() {
    if (startGameRequested_ || returnToSelectRequested_) {
        return;
    }
    if (sceneTime_ < kIntroContentDelay) {
        return;
    }
    if (IsHandControl(inputCalibration_.controlType)) {
        DrawCameraPreview();
    }
}

TipScene::Image TipScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width = static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
    return image;
}

TipScene::Image TipScene::CurrentTipBodyImage() const {
    const size_t index =
        static_cast<size_t>(sceneTime_ / kTipRotateSeconds) %
        bodyImages_.size();
    return bodyImages_[index];
}

bool TipScene::ShouldStart() {
    switch (inputCalibration_.controlType) {
    case InputControlType::KeyboardMouse:
        return ctx_->systems.input->IsKeyTrigger(DIK_SPACE);
    case InputControlType::JoyCon: {
        constexpr int kFaceButtons = JSMASK_S | JSMASK_E | JSMASK_W | JSMASK_N;
        const bool left =
            leftJoyCon_.IsConnected() &&
            (leftJoyCon_.IsButtonTrigger(kFaceButtons));
        const bool right =
            rightJoyCon_.IsConnected() &&
            (rightJoyCon_.IsButtonTrigger(kFaceButtons));
        return left || right;
    }
    case InputControlType::Hand: {
        if (ctx_->systems.input != nullptr &&
            (ctx_->systems.input->IsKeyTrigger(DIK_SPACE) ||
             ctx_->systems.input->IsKeyTrigger(DIK_RETURN))) {
            return true;
        }
        const float speed =
            (std::max)(handController_.GetMotionSpeed(0),
                       handController_.GetMotionSpeed(1));
        if (handSwingArmed_ && speed >= kHandSwingStartSpeed) {
            ++handSwingCount_;
            handSwingArmed_ = false;
        }
        if (speed <= kHandSwingResetSpeed) {
            handSwingArmed_ = true;
        }
        return handSwingCount_ >= kRequiredHandSwings;
    }
    default:
        return false;
    }
}

void TipScene::RequestHandTrackingStartOnce() {
    if (handTrackingStartRequested_ || ctx_ == nullptr) {
        return;
    }

    if (AppSceneServices::HasHandTrackingStart()) {
        AppSceneServices::RequestHandTrackingStart();
    } else {
        return;
    }
    handTrackingStartRequested_ = true;
}

void TipScene::UpdateCameraPreview(float deltaTime) {
    previewReceiver_.Update(deltaTime);
}

void TipScene::DrawCameraPreview() {
    if (ctx_ == nullptr || ctx_->rendering.sprite == nullptr || ctx_->rendering.texture == nullptr ||
        ctx_->systems.winApp == nullptr) {
        return;
    }

    previewReceiver_.Draw(ctx_->rendering.sprite, ctx_->rendering.texture,
                          kPreviewStaleSeconds);
}

void TipScene::DrawRect(float x, float y, float w, float h,
                        const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void TipScene::DrawImage(const Image &image, float x, float y, float scale,
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
