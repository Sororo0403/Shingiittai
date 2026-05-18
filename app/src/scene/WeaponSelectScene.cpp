#include "WeaponSelectScene.h"
#include "CalibrationScene.h"
#include "DirectXCommon.h"
#include "HandRegistrationScene.h"
#include "Input.h"
#include "ModelManager.h"
#include "PostEffectRenderer.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "TipScene.h"
#include "WinApp.h"
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <memory>

using namespace DirectX;

namespace {
constexpr float kTransitionDuration = 0.34f;

XMFLOAT4 MakeColor(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }

XMFLOAT4 MakeQuat(float pitch, float yaw, float roll) {
    XMFLOAT4 q{};
    XMStoreFloat4(&q, XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
    return q;
}
} // namespace

void WeaponSelectScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    selectedIndex_ = 0;
    sceneTime_ = 0.0f;
    transitionTimer_ = 0.0f;
    startRequested_ = false;
    waitingForHandTrackingReady_ = false;
    handTrackingStartRequested_ = false;
    pulseTimers_.fill(0.0f);
    joyConAvailable_ = false;
    cameraAvailable_ = false;

    leftJoyCon_.Initialize(true);
    rightJoyCon_.Initialize(false);

    const float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                         static_cast<float>(ctx_->winApp->GetHeight());
    camera_.Initialize(aspect);
    camera_.SetMode(CameraMode::LookAt);
    camera_.SetPerspectiveFovDeg(39.0f);
    UpdateCamera();

    ctx_->dxCommon->BeginUpload();
    backgroundImage_ =
        LoadTextureImage(L"app/resources/select/weapon_select_bg.png");
    titleImage_ = LoadTextureImage(L"app/resources/text/input_title.png");
    controlsImage_ =
        LoadTextureImage(L"app/resources/text/weapon_controls.png");
    readyImage_ = LoadTextureImage(L"app/resources/text/weapon_ready.png");
    weaponNameImages_[0] =
        LoadTextureImage(L"app/resources/text/input_kbm.png");
    weaponNameImages_[1] =
        LoadTextureImage(L"app/resources/text/input_joycon.png");
    weaponNameImages_[2] =
        LoadTextureImage(L"app/resources/text/input_hand.png");
    weaponDescImages_[0] =
        LoadTextureImage(L"app/resources/text/input_ready_kbm.png");
    weaponDescImages_[1] =
        LoadTextureImage(L"app/resources/text/input_ready_joycon.png");
    weaponDescImages_[2] =
        LoadTextureImage(L"app/resources/text/input_ready_hand.png");
    weaponBottomImages_[0] =
        LoadTextureImage(L"app/resources/text/input_ready_kbm.png");
    weaponBottomImages_[1] =
        LoadTextureImage(L"app/resources/text/input_ready_joycon.png");
    weaponBottomImages_[2] =
        LoadTextureImage(L"app/resources/text/input_ready_hand.png");
    swordModelId_ = ctx_->model->Load(L"app/resources/models/player/sword.glb");
    ctx_->dxCommon->EndUpload();
    ctx_->texture->ReleaseUploadBuffers();

    ctx_->postEffectRenderer->ResetEffects();
    UpdateLighting();
}

void WeaponSelectScene::Update() {
    sceneTime_ += ctx_->deltaTime;
    UpdateDeviceAvailability();
    for (float &pulse : pulseTimers_) {
        pulse = (std::max)(0.0f, pulse - ctx_->deltaTime * 3.0f);
    }

    const float w = static_cast<float>(ctx_->winApp->GetWidth());
    const float h = static_cast<float>(ctx_->winApp->GetHeight());
    Layout(w, h);

    if (startRequested_) {
        const InputControlType selectedType = SelectedControlType();
        if (selectedType == InputControlType::Hand &&
            waitingForHandTrackingReady_) {
            if (!IsHandTrackingReady()) {
                transitionTimer_ =
                    (std::min)(transitionTimer_ + ctx_->deltaTime,
                               kTransitionDuration * 0.72f);
                return;
            }

            RequestHandTrackingStartOnce();
            waitingForHandTrackingReady_ = false;
            transitionTimer_ = 0.0f;
        } else {
            transitionTimer_ += ctx_->deltaTime;
        }

        if (transitionTimer_ >= kTransitionDuration) {
            if (selectedType == InputControlType::Hand) {
                sceneManager_->ChangeScene(
                    std::make_unique<HandRegistrationScene>());
            } else if (selectedType == InputControlType::JoyCon) {
                sceneManager_->ChangeScene(
                    std::make_unique<CalibrationScene>(selectedType));
            } else {
                SwordInputCalibration calibration{};
                calibration.controlType = selectedType;
                sceneManager_->ChangeScene(
                    std::make_unique<TipScene>(calibration));
            }
        }
        return;
    }

    UpdateSelection(ctx_->input);
    UpdateCamera();
    UpdateLighting();
}

void WeaponSelectScene::Draw() {
    const float w = static_cast<float>(ctx_->winApp->GetWidth());
    const float h = static_cast<float>(ctx_->winApp->GetHeight());

    ctx_->sprite->PreDraw();
    DrawBackground(w, h);
    DrawCards(w, h);
    ctx_->sprite->PostDraw();

    DrawModelPreviews();

    ctx_->sprite->PreDraw();
    DrawLabels(w, h);
    DrawStartTransition(w, h);
    ctx_->sprite->PostDraw();
}

WeaponSelectScene::Image
WeaponSelectScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->texture->Load(path);
    image.width = static_cast<float>(ctx_->texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->texture->GetHeight(image.textureId));
    return image;
}

void WeaponSelectScene::UpdateSelection(Input *input) {
    int nextIndex = selectedIndex_;

    if (input->IsKeyTrigger(DIK_LEFT) || input->IsKeyTrigger(DIK_A)) {
        nextIndex = (selectedIndex_ + kWeaponCount - 1) % kWeaponCount;
    }
    if (input->IsKeyTrigger(DIK_RIGHT) || input->IsKeyTrigger(DIK_D)) {
        nextIndex = (selectedIndex_ + 1) % kWeaponCount;
    }
    if (input->IsKeyTrigger(DIK_1)) {
        nextIndex = 0;
    }
    if (input->IsKeyTrigger(DIK_2)) {
        nextIndex = 1;
    }
    if (input->IsKeyTrigger(DIK_3)) {
        nextIndex = 2;
    }

    if (input->IsGamepadConnected()) {
        if (input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_LEFT)) {
            nextIndex = (selectedIndex_ + kWeaponCount - 1) % kWeaponCount;
        }
        if (input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_RIGHT)) {
            nextIndex = (selectedIndex_ + 1) % kWeaponCount;
        }
    }

    if (nextIndex != selectedIndex_) {
        selectedIndex_ = nextIndex;
        pulseTimers_[selectedIndex_] = 1.0f;
    }

    if (input->IsKeyTrigger(DIK_RETURN) || input->IsKeyTrigger(DIK_SPACE) ||
        (input->IsGamepadConnected() &&
         input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_A))) {
        BeginStart();
    }
}

void WeaponSelectScene::UpdateDeviceAvailability() {
    leftJoyCon_.Update(ctx_->deltaTime);
    rightJoyCon_.Update(ctx_->deltaTime);

    joyConAvailable_ =
        leftJoyCon_.IsConnected() || rightJoyCon_.IsConnected();
    cameraAvailable_ = !ctx_->isCameraDeviceAvailable ||
                       ctx_->isCameraDeviceAvailable();
}

void WeaponSelectScene::BeginStart() {
    if (!IsModeAvailable(selectedIndex_)) {
        pulseTimers_[selectedIndex_] = 1.0f;
        ShowUnavailableMessage(selectedIndex_);
        return;
    }

    const InputControlType selectedType = SelectedControlType();
    if (selectedType == InputControlType::Hand) {
        if (IsHandTrackingReady()) {
            RequestHandTrackingStartOnce();
        } else {
            waitingForHandTrackingReady_ = true;
        }
    }

    startRequested_ = true;
    transitionTimer_ = 0.0f;
}

void WeaponSelectScene::Layout(float screenWidth, float screenHeight) {
    const float cardW = (std::min)(330.0f, screenWidth * 0.25f);
    const float cardH = (std::min)(390.0f, screenHeight * 0.55f);
    const float gap = (std::max)(34.0f, screenWidth * 0.035f);
    const float totalW = cardW * static_cast<float>(kWeaponCount) +
                         gap * static_cast<float>(kWeaponCount - 1);
    const float startX = (screenWidth - totalW) * 0.5f;
    const float y = screenHeight * 0.19f;

    for (int i = 0; i < kWeaponCount; ++i) {
        cardRects_[i] = {startX + i * (cardW + gap), y, cardW, cardH};
    }
}

InputControlType WeaponSelectScene::SelectedControlType() const {
    switch (selectedIndex_) {
    case 2:
        return InputControlType::Hand;
    case 1:
        return InputControlType::JoyCon;
    default:
        return InputControlType::KeyboardMouse;
    }
}

bool WeaponSelectScene::IsHandTrackingReady() const {
    return !ctx_->isHandTrackingReady || ctx_->isHandTrackingReady();
}

void WeaponSelectScene::RequestHandTrackingStartOnce() {
    if (handTrackingStartRequested_ || !ctx_->requestHandTrackingStart) {
        return;
    }
    ctx_->requestHandTrackingStart();
    handTrackingStartRequested_ = true;
}

bool WeaponSelectScene::IsModeAvailable(int index) const {
    switch (index) {
    case 1:
        return joyConAvailable_;
    case 2:
        return cameraAvailable_;
    case 0:
    default:
        return true;
    }
}

void WeaponSelectScene::ShowUnavailableMessage(int index) {
    const wchar_t *message =
        index == 1 ? L"Joy-Conがありません" : L"ウェブカメラがありません";
    MessageBoxW(ctx_->winApp->GetHwnd(), message, L"入力モード",
                MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);
    ctx_->winApp->BringToFront();
}

void WeaponSelectScene::UpdateCamera() {
    camera_.SetPosition({0.0f, 2.5f, -8.2f});
    camera_.LookAt({0.0f, 1.05f, 0.0f});
    camera_.UpdateMatrices();
}

void WeaponSelectScene::UpdateLighting() {
    SceneLighting lighting{};
    lighting.keyLightDirection = {-0.20f, -1.0f, 0.26f};
    lighting.keyLightColor = {1.32f, 1.18f, 0.92f, 1.0f};
    lighting.fillLightDirection = {0.75f, -0.30f, -0.58f};
    lighting.fillLightColor = {0.18f, 0.46f, 0.78f, 0.70f};
    lighting.ambientColor = {0.18f, 0.18f, 0.20f, 1.0f};
    lighting.lightingParams = {78.0f, 0.60f, 4.8f, 0.24f};
    lighting.pointLights[0].positionRange = {0.0f, 2.5f, -1.4f, 8.2f};
    lighting.pointLights[0].colorIntensity = {1.0f, 0.38f, 0.14f, 1.65f};
    lighting.pointLights[1].positionRange = {0.0f, 1.7f, 1.8f, 8.0f};
    lighting.pointLights[1].colorIntensity = {0.12f, 0.50f, 1.0f, 1.05f};
    ctx_->model->SetSceneLighting(lighting);
}

void WeaponSelectScene::DrawBackground(float screenWidth, float screenHeight) {
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             MakeColor(0.96f, 0.97f, 0.98f, 1.0f));
    DrawImage(backgroundImage_, 0.0f, 0.0f,
              (std::max)(screenWidth / (std::max)(backgroundImage_.width, 1.0f),
                         screenHeight /
                             (std::max)(backgroundImage_.height, 1.0f)),
              0.78f);
    DrawRect(0.0f, 0.0f, screenWidth, 96.0f,
             MakeColor(0.03f, 0.03f, 0.035f, 0.86f));
    DrawRect(0.0f, 88.0f, screenWidth, 6.0f,
             MakeColor(1.0f, 0.82f, 0.02f, 0.98f));
    DrawRect(0.0f, screenHeight - 126.0f, screenWidth, 126.0f,
             MakeColor(0.02f, 0.02f, 0.025f, 0.88f));
}

void WeaponSelectScene::DrawCards(float, float) {
    for (int i = 0; i < kWeaponCount; ++i) {
        const ButtonRect &rect = cardRects_[i];
        const bool selected = i == selectedIndex_;
        const bool available = IsModeAvailable(i);
        const float pulse = pulseTimers_[i];
        const float lift = selected ? 10.0f : 0.0f;
        const XMFLOAT4 body =
            available ? (selected ? MakeColor(0.98f, 0.985f, 0.99f, 0.92f)
                                  : MakeColor(0.88f, 0.90f, 0.93f, 0.64f))
                      : (selected ? MakeColor(0.38f, 0.39f, 0.41f, 0.82f)
                                  : MakeColor(0.27f, 0.28f, 0.30f, 0.58f));
        DrawRect(rect.x, rect.y - lift, rect.w, rect.h, body);
        DrawRect(rect.x, rect.y + rect.h - lift - 8.0f, rect.w, 8.0f,
                 available
                     ? MakeColor(0.02f, 0.02f, 0.025f,
                                 selected ? 0.90f : 0.45f)
                     : MakeColor(0.10f, 0.105f, 0.12f,
                                 selected ? 0.86f : 0.54f));
        DrawRect(rect.x, rect.y - lift, rect.w, 8.0f,
                 WeaponColor(i, selected ? 0.95f : 0.42f, available));

        if (selected) {
            const float frame = 6.0f + pulse * 4.0f;
            const XMFLOAT4 color =
                available ? MakeColor(1.0f, 0.82f, 0.02f, 1.0f)
                          : MakeColor(0.56f, 0.57f, 0.60f, 1.0f);
            DrawRect(rect.x - frame, rect.y - lift - frame,
                     rect.w + frame * 2.0f, frame, color);
            DrawRect(rect.x - frame, rect.y + rect.h - lift,
                     rect.w + frame * 2.0f, frame, color);
            DrawRect(rect.x - frame, rect.y - lift - frame, frame,
                     rect.h + frame * 2.0f, color);
            DrawRect(rect.x + rect.w, rect.y - lift - frame, frame,
                     rect.h + frame * 2.0f, color);
        }
    }
}

void WeaponSelectScene::DrawModelPreviews() {
    ctx_->model->PreDraw();
    for (int i = 0; i < kWeaponCount; ++i) {
        const bool available = IsModeAvailable(i);
        const bool selected = i == selectedIndex_;
        ModelDrawEffect effect{};
        effect.enabled = true;
        effect.disableCulling = true;
        effect.additiveBlend = selected && available;
        effect.color =
            WeaponColor(i, selected ? 0.72f : 0.42f, available);
        effect.intensity =
            available ? (selected ? 0.62f : 0.18f) : (selected ? 0.16f : 0.08f);
        effect.fresnelPower = 1.6f;
        ctx_->model->SetDrawEffect(effect);

        const int swordCount = 2;
        for (int swordIndex = 0; swordIndex < swordCount; ++swordIndex) {
            ctx_->model->Draw(swordModelId_, MakeSwordTransform(i, swordIndex),
                              camera_);
        }
    }
    ctx_->model->ClearDrawEffect();
    ctx_->model->PostDraw();
}

void WeaponSelectScene::DrawLabels(float screenWidth, float screenHeight) {
    const float titleScale =
        (std::min)(1.0f, (screenWidth * 0.38f) /
                             (std::max)(titleImage_.width, 1.0f));
    DrawImage(titleImage_, (screenWidth - titleImage_.width * titleScale) * 0.5f,
              13.0f, titleScale, 1.0f);

    for (int i = 0; i < kWeaponCount; ++i) {
        const ButtonRect &rect = cardRects_[i];
        const bool selected = i == selectedIndex_;
        const bool available = IsModeAvailable(i);
        const float lift = selected ? 10.0f : 0.0f;
        const Image &name = weaponNameImages_[i];
        const Image &desc = weaponDescImages_[i];
        const float disabledAlpha = available ? 1.0f : 0.42f;
        const float nameScale =
            (std::min)(1.0f, (rect.w * 0.70f) / (std::max)(name.width, 1.0f));
        const float descScale =
            (std::min)(1.0f, (rect.w * 0.82f) / (std::max)(desc.width, 1.0f));
        DrawImage(name, rect.x + (rect.w - name.width * nameScale) * 0.5f,
                  rect.y + rect.h - lift - 102.0f, nameScale,
                  disabledAlpha * (selected ? 1.0f : 0.70f));
        DrawImage(desc, rect.x + (rect.w - desc.width * descScale) * 0.5f,
                  rect.y + rect.h - lift - 43.0f, descScale,
                  disabledAlpha * (selected ? 1.0f : 0.62f));
    }

    const Image &bottom = weaponBottomImages_[selectedIndex_];
    const float bottomScale =
        (std::min)(1.0f, (screenWidth * 0.56f) /
                             (std::max)(bottom.width, 1.0f));
    DrawImage(bottom, 58.0f, screenHeight - 88.0f, bottomScale,
              IsModeAvailable(selectedIndex_) ? 1.0f : 0.42f);

    const float controlsScale =
        (std::min)(1.0f, (screenWidth * 0.32f) /
                             (std::max)(controlsImage_.width, 1.0f));
    DrawImage(controlsImage_,
              screenWidth - controlsImage_.width * controlsScale - 52.0f,
              screenHeight - 78.0f, controlsScale, 0.82f);
}

void WeaponSelectScene::DrawStartTransition(float screenWidth,
                                            float screenHeight) {
    if (!startRequested_) {
        return;
    }

    const float t =
        std::clamp(transitionTimer_ / kTransitionDuration, 0.0f, 1.0f);
    const float eased = SmoothStep(t);
    const float bandY = screenHeight * 0.43f - 52.0f;
    DrawRect(0.0f, bandY, screenWidth, 104.0f,
             MakeColor(1.0f, 0.82f, 0.02f, 0.96f));
    DrawRect(0.0f, bandY + 78.0f, screenWidth, 16.0f,
             MakeColor(0.88f, 0.02f, 0.02f, 0.98f));
    DrawImage(readyImage_, (screenWidth - readyImage_.width) * 0.5f,
              bandY + 15.0f, 1.0f, 1.0f);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             MakeColor(0.0f, 0.0f, 0.0f, eased * 0.36f));
}

void WeaponSelectScene::DrawRect(float x, float y, float w, float h,
                                 const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->sprite->DrawSprite(sprite);
}

void WeaponSelectScene::DrawImage(const Image &image, float x, float y,
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

Transform WeaponSelectScene::MakeSwordTransform(int weaponIndex,
                                                int swordIndex) const {
    Transform transform{};
    const float centerOffset =
        static_cast<float>(weaponIndex) -
        (static_cast<float>(kWeaponCount - 1) * 0.5f);
    const float baseX = centerOffset * 2.2f;
    const bool selected = weaponIndex == selectedIndex_;
    const float sway =
        selected ? std::sinf(sceneTime_ * 2.6f + weaponIndex) * 0.05f : 0.0f;
    transform.position = {baseX, 1.08f + (selected ? 0.07f : 0.0f) + sway,
                          1.12f};

    float scale = selected ? 2.95f : 2.35f;
    transform.scale = {scale, scale, scale};

    float roll = 0.0f;
    if (weaponIndex == 1) {
        roll = swordIndex == 0 ? -0.58f : 0.58f;
        transform.position.x += swordIndex == 0 ? -0.34f : 0.34f;
    }

    transform.rotation = MakeQuat(0.72f, 0.0f, roll);
    return transform;
}

XMFLOAT4 WeaponSelectScene::WeaponColor(int index, float alpha,
                                        bool available) const {
    if (!available) {
        return MakeColor(0.45f, 0.46f, 0.49f, alpha);
    }

    switch (index) {
    case 2:
        return MakeColor(0.24f, 0.84f, 0.58f, alpha);
    case 1:
        return MakeColor(0.10f, 0.52f, 1.0f, alpha);
    case 0:
    default:
        return MakeColor(0.92f, 0.02f, 0.05f, alpha);
    }
}
