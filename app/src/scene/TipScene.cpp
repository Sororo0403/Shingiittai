#include "TipScene.h"
#include "AppSceneServices.h"
#include "DirectXCommon.h"
#include "GameScene.h"
#include "Input.h"
#include "PostProcessSystem.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <algorithm>
#include <memory>

using namespace DirectX;

namespace {
constexpr float kHandSwingStartSpeed = 0.78f;
constexpr float kHandSwingResetSpeed = 0.32f;
constexpr int kRequiredHandSwings = 3;
constexpr uint16_t kPreviewPort = 5006;
constexpr float kPreviewStaleSeconds = 0.75f;

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
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
    handSwingCount_ = 0;
    handSwingArmed_ = true;
    handTrackingStartRequested_ = false;

    if (inputCalibration_.controlType == InputControlType::JoyCon) {
        leftJoyCon_.Initialize(true);
        rightJoyCon_.Initialize(false);
    }
    if (IsHandControl(inputCalibration_.controlType)) {
        handController_.SetCalibration(inputCalibration_);
        RequestHandTrackingStartOnce();
        previewReceiver_.Initialize(ctx_->rendering.texture, kPreviewPort);
    }

    backgroundImage_ =
        LoadTextureImage(L"app/resources/ui/weapon_select/weapon_select_bg.png");
    titleImage_ = LoadTextureImage(L"app/resources/ui/tip/text/tip_title.png");
    bodyImage_ = LoadTextureImage(L"app/resources/ui/tip/text/tip_body.png");
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
    if (inputCalibration_.controlType == InputControlType::JoyCon) {
        leftJoyCon_.Update(ctx_->frame.deltaTime);
        rightJoyCon_.Update(ctx_->frame.deltaTime);
    }
    if (IsHandControl(inputCalibration_.controlType)) {
        RequestHandTrackingStartOnce();
        handController_.Update(ctx_->frame.deltaTime);
        UpdateCameraPreview(ctx_->frame.deltaTime);
    }

    if (ShouldStart()) {
        sceneManager_->ChangeScene(std::make_unique<GameScene>(inputCalibration_));
    }
}

void TipScene::Draw() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    ctx_->rendering.sprite->PreDraw();
    DrawRect(0.0f, 0.0f, w, h, Color(0.018f, 0.020f, 0.024f, 1.0f));
    DrawImage(backgroundImage_, 0.0f, 0.0f,
              (std::max)(w / (std::max)(backgroundImage_.width, 1.0f),
                         h / (std::max)(backgroundImage_.height, 1.0f)),
              0.26f);
    DrawRect(w * 0.10f, h * 0.18f, w * 0.80f, h * 0.56f,
             Color(0.025f, 0.028f, 0.032f, 0.84f));
    DrawRect(w * 0.10f, h * 0.18f, w * 0.80f, 6.0f,
             Color(1.0f, 0.82f, 0.18f, 0.92f));
    DrawImage(titleImage_, (w - titleImage_.width) * 0.5f, h * 0.23f, 1.0f);
    DrawImage(bodyImage_, (w - bodyImage_.width) * 0.5f, h * 0.42f, 1.0f,
              0.86f);
    const float pulse = 0.72f + 0.28f * std::sinf(sceneTime_ * 5.0f);
    DrawImage(promptImage_, (w - promptImage_.width) * 0.5f, h * 0.62f, 1.0f,
              pulse);
    if (IsHandControl(inputCalibration_.controlType)) {
        const float unit = 48.0f;
        const float startX = (w - unit * 3.0f - 18.0f * 2.0f) * 0.5f;
        for (int i = 0; i < 3; ++i) {
            DrawRect(startX + static_cast<float>(i) * (unit + 18.0f),
                     h * 0.78f, unit, 10.0f,
                     i < handSwingCount_ ? Color(0.10f, 0.74f, 0.36f, 1.0f)
                                         : Color(0.20f, 0.24f, 0.28f, 1.0f));
        }
    }
    ctx_->rendering.sprite->PostDraw();
}

void TipScene::DrawTransparent() {
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
