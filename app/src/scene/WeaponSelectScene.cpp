#include "WeaponSelectScene.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "GameScene.h"
#include "ModelManager.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#include <memory>

using namespace DirectX;

namespace {
PlayerWeaponType ToWeaponType(int index) {
    switch (index) {
    case 1:
        return PlayerWeaponType::Dual;
    case 2:
        return PlayerWeaponType::GreatSword;
    case 0:
    default:
        return PlayerWeaponType::Standard;
    }
}

XMFLOAT4 MakeColor(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

XMFLOAT4 MakeQuat(float pitch, float yaw, float roll) {
    XMFLOAT4 q{};
    XMStoreFloat4(&q, XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
    return q;
}

} // namespace

void WeaponSelectScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    selectedIndex_ = 0;
    startRequested_ = false;
    requestedWeaponType_ = PlayerWeaponType::Standard;
    sceneTime_ = 0.0f;
    selectionFlashTimer_ = 0.0f;
    readyTimer_ = 0.0f;
    tilePulse_.fill(0.0f);

    const float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                         static_cast<float>(ctx_->winApp->GetHeight());
    camera_.Initialize(aspect);
    camera_.SetMode(CameraMode::LookAt);
    camera_.SetPerspectiveFovDeg(42.0f);
    UpdateCamera();

    ctx_->dxCommon->BeginUpload();
    backgroundImage_ =
        LoadTextImage(L"app/resources/select/weapon_select_bg.png");
    titleImage_ = LoadTextImage(L"app/resources/text/weapon_title.png");
    weaponNameImages_[0] =
        LoadTextImage(L"app/resources/text/weapon_standard.png");
    weaponNameImages_[1] = LoadTextImage(L"app/resources/text/weapon_dual.png");
    weaponNameImages_[2] =
        LoadTextImage(L"app/resources/text/weapon_great.png");
    weaponDescImages_[0] =
        LoadTextImage(L"app/resources/text/weapon_standard_desc.png");
    weaponDescImages_[1] =
        LoadTextImage(L"app/resources/text/weapon_dual_desc.png");
    weaponDescImages_[2] =
        LoadTextImage(L"app/resources/text/weapon_great_desc.png");
    weaponBottomImages_[0] =
        LoadTextImage(L"app/resources/text/weapon_bottom_standard.png");
    weaponBottomImages_[1] =
        LoadTextImage(L"app/resources/text/weapon_bottom_dual.png");
    weaponBottomImages_[2] =
        LoadTextImage(L"app/resources/text/weapon_bottom_great.png");
    controlsImage_ = LoadTextImage(L"app/resources/text/weapon_controls.png");
    readyImage_ = LoadTextImage(L"app/resources/text/weapon_ready.png");
    swordModelId_ = ctx_->model->Load(L"app/resources/models/player/sword.glb");
    ctx_->dxCommon->EndUpload();
    ctx_->texture->ReleaseUploadBuffers();

    UpdateLighting();
}

void WeaponSelectScene::Update() {
    sceneTime_ += ctx_->deltaTime;
    if (selectionFlashTimer_ > 0.0f) {
        selectionFlashTimer_ =
            (std::max)(0.0f, selectionFlashTimer_ - ctx_->deltaTime);
    }
    for (float &pulse : tilePulse_) {
        pulse = (std::max)(0.0f, pulse - ctx_->deltaTime * 2.4f);
    }

    if (!startRequested_) {
        UpdateSelection(ctx_->input);
    } else {
        readyTimer_ += ctx_->deltaTime;
        if (readyTimer_ >= 0.72f) {
            sceneManager_->ChangeScene(
                std::make_unique<GameScene>(requestedWeaponType_));
        }
    }

    UpdateCamera();
    UpdateLighting();
}

void WeaponSelectScene::Draw() {}

void WeaponSelectScene::DrawOverlay() {
    ctx_->dxCommon->SetBackBufferRenderTarget(false, true);
    ctx_->sprite->PreDraw();
    DrawUiBase();
    ctx_->sprite->PostDraw();

    ctx_->dxCommon->SetBackBufferRenderTarget(false, true);
    DrawModels();

    ctx_->dxCommon->SetBackBufferRenderTarget(false, false);
    ctx_->sprite->PreDraw();
    DrawUiOverlay();
    ctx_->sprite->PostDraw();
}

void WeaponSelectScene::StartGame(PlayerWeaponType weaponType) {
    requestedWeaponType_ = weaponType;
    startRequested_ = true;
    readyTimer_ = 0.0f;
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
    } else if (input->IsKeyTrigger(DIK_2)) {
        nextIndex = 1;
    } else if (input->IsKeyTrigger(DIK_3)) {
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
        selectionFlashTimer_ = 0.18f;
        tilePulse_[selectedIndex_] = 1.0f;
    }

    if (input->IsKeyTrigger(DIK_RETURN) || input->IsKeyTrigger(DIK_SPACE) ||
        (input->IsGamepadConnected() &&
         input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_A))) {
        StartGame(ToWeaponType(selectedIndex_));
    }
}

void WeaponSelectScene::UpdateCamera() {
    camera_.SetPosition({0.0f, 2.6f, -8.4f});
    camera_.LookAt({0.0f, 1.05f, 0.0f});
    camera_.UpdateMatrices();
}

void WeaponSelectScene::UpdateLighting() {
    SceneLighting lighting{};
    lighting.keyLightDirection = {-0.25f, -1.0f, 0.22f};
    lighting.keyLightColor = {1.35f, 1.20f, 0.92f, 1.0f};
    lighting.fillLightDirection = {0.75f, -0.20f, -0.65f};
    lighting.fillLightColor = {0.24f, 0.44f, 0.70f, 0.60f};
    lighting.ambientColor = {0.20f, 0.20f, 0.23f, 1.0f};
    lighting.lightingParams = {72.0f, 0.55f, 4.6f, 0.20f};
    lighting.pointLights[0].positionRange = {0.0f, 2.8f, -1.5f, 8.5f};
    lighting.pointLights[0].colorIntensity = {1.0f, 0.36f, 0.20f, 1.6f};
    lighting.pointLights[1].positionRange = {0.0f, 1.6f, 1.8f, 8.0f};
    lighting.pointLights[1].colorIntensity = {0.18f, 0.55f, 1.0f, 1.0f};
    ctx_->model->SetSceneLighting(lighting);
}

void WeaponSelectScene::DrawModels() {
    ctx_->model->PreDraw();
    for (int i = 0; i < kWeaponCount; ++i) {
        const int swordCount = i == 1 ? 2 : 1;
        const float selectedBoost = i == selectedIndex_ ? 1.0f : 0.0f;
        ModelDrawEffect effect{};
        effect.enabled = true;
        effect.color = WeaponColor(i, 0.80f);
        effect.intensity = 0.20f + selectedBoost * 0.70f;
        effect.fresnelPower = 2.8f;
        effect.noiseAmount = 0.0f;
        effect.time = 0.0f;
        effect.disableCulling = true;
        ctx_->model->SetDrawEffect(effect);

        for (int s = 0; s < swordCount; ++s) {
            ctx_->model->Draw(swordModelId_, MakeSwordTransform(i, s), camera_);
        }
    }
    ctx_->model->ClearDrawEffect();
    ctx_->model->PostDraw();
}

void WeaponSelectScene::DrawUiBase() {
    const float w = static_cast<float>(ctx_->winApp->GetWidth());
    const float h = static_cast<float>(ctx_->winApp->GetHeight());

    DrawRect(0.0f, 0.0f, w, h, MakeColor(0.985f, 0.988f, 0.992f, 1.0f));
    DrawStretchImage(backgroundImage_, 0.0f, 0.0f, w, h, 1.0f);

    const float cardW = 330.0f;
    const float cardH = 360.0f;
    const float cardY = 132.0f;
    const float startX = (w - cardW * 3.0f - 42.0f * 2.0f) * 0.5f;
    for (int i = 0; i < kWeaponCount; ++i) {
        const float x = startX + i * (cardW + 42.0f);
        const bool selected = i == selectedIndex_;
        XMFLOAT4 base = selected ? MakeColor(1.0f, 1.0f, 1.0f, 0.92f)
                                 : MakeColor(0.95f, 0.96f, 0.97f, 0.72f);
        DrawRect(x, cardY, cardW, cardH, base);
        DrawRect(x + 10.0f, cardY + 10.0f, cardW - 20.0f, cardH - 20.0f,
                 MakeColor(1.0f, 1.0f, 1.0f, selected ? 0.42f : 0.28f));
        DrawRect(x, cardY, cardW, 10.0f,
                 MakeColor(0.06f, 0.06f, 0.07f, selected ? 0.92f : 0.30f));
        if (selected) {
            DrawRect(x - 8.0f, cardY - 8.0f, cardW + 16.0f, 8.0f,
                     MakeColor(1.0f, 0.86f, 0.05f, 1.0f));
            DrawRect(x - 8.0f, cardY + cardH, cardW + 16.0f, 8.0f,
                     MakeColor(1.0f, 0.86f, 0.05f, 1.0f));
            DrawRect(x - 8.0f, cardY - 8.0f, 8.0f, cardH + 16.0f,
                     MakeColor(1.0f, 0.86f, 0.05f, 1.0f));
            DrawRect(x + cardW, cardY - 8.0f, 8.0f, cardH + 16.0f,
                     MakeColor(1.0f, 0.86f, 0.05f, 1.0f));
        }
    }

    DrawRect(0, 540, w, 180, MakeColor(0.98f, 0.985f, 0.99f, 0.72f));
    DrawRect(0, 540, w, 5, MakeColor(0.06f, 0.06f, 0.07f, 0.86f));
    DrawRect(0, 548, w, 5, MakeColor(1.0f, 0.86f, 0.05f, 0.95f));
}

void WeaponSelectScene::DrawUiOverlay() {
    const float w = static_cast<float>(ctx_->winApp->GetWidth());

    DrawImage(titleImage_, (w - titleImage_.width) * 0.5f, 18.0f);

    const float cardW = 330.0f;
    const float cardY = 132.0f;
    const float startX = (w - cardW * 3.0f - 42.0f * 2.0f) * 0.5f;
    for (int i = 0; i < kWeaponCount; ++i) {
        const float x = startX + i * (cardW + 42.0f);
        const TextImage &name = weaponNameImages_[i];
        const TextImage &desc = weaponDescImages_[i];
        DrawImage(name, x + (cardW - name.width) * 0.5f, cardY + 266.0f);
        DrawImage(desc, x + (cardW - desc.width) * 0.5f, cardY + 326.0f);
    }

    const TextImage &bottom = weaponBottomImages_[selectedIndex_];
    DrawImage(bottom, 72.0f, 594.0f);
    DrawImage(controlsImage_, w - controlsImage_.width - 70.0f, 624.0f, 1.0f,
              0.82f);

    if (startRequested_) {
        const float t = std::clamp(readyTimer_ / 0.72f, 0.0f, 1.0f);
        const float bandY = 292.0f + std::sinf(t * XM_PI) * -18.0f;
        DrawRect(-30.0f, bandY, w + 60.0f, 96.0f,
                 MakeColor(1.0f, 0.82f, 0.00f, 0.98f));
        DrawRect(-30.0f, bandY + 74.0f, w + 60.0f, 16.0f,
                 MakeColor(0.92f, 0.02f, 0.02f, 1.0f));
        DrawImage(readyImage_, (w - readyImage_.width) * 0.5f, bandY + 13.0f);
    }
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

WeaponSelectScene::TextImage
WeaponSelectScene::LoadTextImage(const std::wstring &path) {
    TextImage image{};
    image.textureId = ctx_->texture->Load(path);
    image.width = static_cast<float>(ctx_->texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->texture->GetHeight(image.textureId));
    return image;
}

void WeaponSelectScene::DrawImage(const TextImage &image, float x, float y,
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

void WeaponSelectScene::DrawStretchImage(const TextImage &image, float x,
                                         float y, float w, float h,
                                         float alpha) {
    if (image.width <= 0.0f || image.height <= 0.0f || w <= 0.0f ||
        h <= 0.0f) {
        return;
    }

    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = {1.0f, 1.0f, 1.0f, alpha};
    sprite.textureId = image.textureId;
    ctx_->sprite->DrawSprite(sprite);
}

Transform WeaponSelectScene::MakeSwordTransform(int weaponIndex,
                                                int swordIndex) const {
    Transform tf{};
    const float x = (static_cast<float>(weaponIndex) - 1.0f) * 2.2f;
    const bool selected = weaponIndex == selectedIndex_;
    tf.position = {x, 1.02f + (selected ? 0.08f : 0.0f), 1.15f};

    float scale = selected ? 2.85f : 2.25f;
    if (weaponIndex == 2) {
        scale *= 1.55f;
    }
    tf.scale = {scale, scale, scale};

    const float yaw = 0.0f;
    float roll = 0.0f;
    if (weaponIndex == 1) {
        roll = swordIndex == 0 ? -0.58f : 0.58f;
        tf.position.x += swordIndex == 0 ? -0.36f : 0.36f;
    } else if (weaponIndex == 2) {
        roll = -0.18f;
        tf.position.y -= 0.12f;
    }
    tf.rotation = MakeQuat(0.72f, yaw, roll);
    return tf;
}

XMFLOAT4 WeaponSelectScene::WeaponColor(int index, float alpha) const {
    switch (index) {
    case 1:
        return MakeColor(0.10f, 0.48f, 1.0f, alpha);
    case 2:
        return MakeColor(0.78f, 0.36f, 0.08f, alpha);
    case 0:
    default:
        return MakeColor(0.95f, 0.08f, 0.10f, alpha);
    }
}
