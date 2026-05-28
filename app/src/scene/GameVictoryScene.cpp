#include "GameVictoryScene.h"
#include "BattleResultScene.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "Material.h"
#include "ModelManager.h"
#include "ParticleEmitterSettings.h"
#include "PostProcessSystem.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace DirectX;

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kDuration = 4.15f;
constexpr float kImpactTime = 0.82f;
constexpr XMFLOAT3 kPlayerPos{-1.38f, 0.0f, -0.52f};
constexpr XMFLOAT3 kEnemyStartPos{0.82f, 0.0f, 0.64f};

float SmoothStep01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float EaseOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

XMFLOAT4 MakeQuat(float pitch, float yaw, float roll) {
    XMFLOAT4 q{};
    XMStoreFloat4(&q, XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
    return q;
}

XMFLOAT3 CameraRotationLookAt(const XMFLOAT3 &eye, const XMFLOAT3 &target) {
    const float dx = target.x - eye.x;
    const float dy = target.y - eye.y;
    const float dz = target.z - eye.z;
    const float yaw = std::atan2f(dx, dz);
    const float flat = std::sqrtf(dx * dx + dz * dz);
    const float pitch = -std::atan2f(dy, (std::max)(flat, 0.0001f));
    return {pitch, yaw, 0.0f};
}

uint32_t CreateSlashTexture(TextureManager *texture) {
    constexpr uint32_t kWidth = 512;
    constexpr uint32_t kHeight = 128;
    std::vector<uint8_t> pixels(static_cast<size_t>(kWidth) * kHeight * 4u);
    for (uint32_t y = 0; y < kHeight; ++y) {
        for (uint32_t x = 0; x < kWidth; ++x) {
            const float u =
                static_cast<float>(x) / static_cast<float>(kWidth - 1u);
            const float v =
                static_cast<float>(y) / static_cast<float>(kHeight - 1u);
            const float center = 0.52f + 0.18f * std::sinf((u - 0.12f) * kPi);
            const float dist = std::fabs(v - center);
            const float blade =
                1.0f - SmoothStep01((dist - 0.018f) / 0.055f);
            const float core =
                1.0f - SmoothStep01((dist - 0.004f) / 0.020f);
            const float taper =
                SmoothStep01(u / 0.10f) *
                (1.0f - SmoothStep01((u - 0.82f) / 0.18f));
            const float alpha = std::clamp((blade * 0.62f + core * 0.55f) *
                                               taper,
                                           0.0f, 1.0f);
            const size_t index = (static_cast<size_t>(y) * kWidth + x) * 4u;
            pixels[index + 0] = 255u;
            pixels[index + 1] = static_cast<uint8_t>(232.0f + 23.0f * core);
            pixels[index + 2] = static_cast<uint8_t>(120.0f + 120.0f * core);
            pixels[index + 3] = static_cast<uint8_t>(alpha * 255.0f);
        }
    }
    return texture->CreateFromRgbaPixels(kWidth, kHeight, pixels.data());
}

Material MakeTransparentMaterial(uint32_t textureId, const XMFLOAT4 &color) {
    Material material{};
    material.color = color;
    material.enableTexture = textureId != 0 ? 1 : 0;
    material.baseColorTextureId = textureId;
    material.blendMode = static_cast<int32_t>(BlendMode::Transparent);
    material.cullMode = static_cast<int32_t>(MaterialCullMode::None);
    material.depthWrite = 0;
    material.reflectionStrength = 0.0f;
    material.reflectionFresnelStrength = 0.0f;
    material.roughness = 1.0f;
    return material;
}
} // namespace

GameVictoryScene::GameVictoryScene(
    float clearTime, const SwordInputCalibration &inputCalibration,
    float combatDifficulty)
    : inputCalibration_(inputCalibration),
      combatDifficulty_(std::clamp(combatDifficulty, 0.0f, 9.0f)),
      clearTime_((std::max)(0.0f, clearTime)) {}

void GameVictoryScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    impactEmitted_ = false;
    finishStarted_ = false;

    const float aspect = static_cast<float>(ctx_->systems.winApp->GetWidth()) /
                         static_cast<float>(ctx_->systems.winApp->GetHeight());
    camera_.Initialize(aspect);
    camera_.SetClipRange(0.05f, 90.0f);

    if (ctx_->rendering.postProcessSystem != nullptr) {
        PostProcessProfile profile{};
        profile.vignette.enabled = true;
        profile.vignette.strength = 0.46f;
        profile.radialBlur.strength = 0.045f;
        profile.radialBlur.sampleCount = 24;
        profile.sceneDim.strength = 0.10f;
        profile.bloom.enabled = true;
        profile.bloom.intensity = 0.50f;
        profile.bloom.threshold = 0.32f;
        ctx_->rendering.postProcessSystem->SetProfile(profile);
    }
    if (ctx_->rendering.dxCommon != nullptr) {
        ctx_->rendering.dxCommon->SetClearColor(0.018f, 0.020f, 0.026f, 1.0f);
    }

    ModelManager *model = ctx_->rendering.model;
    playerModelId_ = model->Load(L"app/resources/models/player/player.glb");
    swordModelId_ = model->Load(L"app/resources/models/player/sword.glb");
    enemyModelId_ = model->Load(L"app/resources/models/boss/boss.gltf");

    player_.Initialize(playerModelId_, swordModelId_);
    player_.SetInputCalibration(inputCalibration_);
    player_.SetCinematicBladeClashPose(kPlayerPos, 1.06f, 1.0f);

    enemy_.SetDifficulty(combatDifficulty_);
    enemy_.Initialize(enemyModelId_);
    enemy_.SetCinematicTransform(kEnemyStartPos, -2.18f);

    Material floorMaterial{};
    floorMaterial.color = {0.030f, 0.036f, 0.043f, 1.0f};
    floorMaterial.enableTexture = 0;
    floorMaterial.roughness = 0.86f;
    floorMaterial.reflectionStrength = 0.025f;
    floorMaterial.cullMode = static_cast<int32_t>(MaterialCullMode::None);
    floorModelId_ = model->CreatePlane(0, floorMaterial);

    slashTextureId_ = CreateSlashTexture(ctx_->rendering.texture);
    slashModelId_ = model->CreatePlane(
        slashTextureId_, MakeTransparentMaterial(slashTextureId_,
                                                 {2.7f, 2.25f, 0.70f, 0.95f}));

    particleTextureId_ =
        ctx_->rendering.texture->Load(L"app/resources/effects/particles/smoke.png");
    if (ctx_->rendering.dxCommon != nullptr && ctx_->rendering.srv != nullptr &&
        particleTextureId_ != 0) {
        impactParticles_.Initialize(ctx_->rendering.dxCommon, ctx_->rendering.srv,
                                    ctx_->rendering.texture, particleTextureId_,
                                    2048);
        particlesReady_ = true;
    }
}

void GameVictoryScene::Update() {
    const float deltaTime = ctx_->frame.deltaTime;
    sceneTime_ += deltaTime;
    UpdateCinematic(deltaTime);

    Input *input = ctx_->systems.input;
    const bool skip =
        input != nullptr &&
        (input->IsKeyTrigger(DIK_SPACE) || input->IsKeyTrigger(DIK_RETURN) ||
         (input->IsGamepadConnected() &&
          input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_A)));
    if (skip || sceneTime_ >= kDuration) {
        Finish();
        return;
    }
}

void GameVictoryScene::Draw() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());
    UpdateCamera(w, h);
    DrawWorld();

    ctx_->rendering.sprite->PreDraw();
    DrawOverlay(w, h);
    ctx_->rendering.sprite->PostDraw();
}

void GameVictoryScene::DrawTransparent() {}

void GameVictoryScene::UpdateCinematic(float deltaTime) {
    const float slashRatio = SmoothStep01((sceneTime_ - 0.12f) / 0.78f);
    const float recoil = SmoothStep01((sceneTime_ - 0.48f) / 0.55f);
    const XMFLOAT3 playerPos{kPlayerPos.x - 0.22f * (1.0f - slashRatio),
                             kPlayerPos.y,
                             kPlayerPos.z - 0.20f * (1.0f - slashRatio)};
    player_.SetCinematicBladeClashPose(playerPos, 1.06f + 0.10f * recoil,
                                       0.60f + 0.40f * slashRatio);

    const float enemyRatio = EaseOutCubic((sceneTime_ - kImpactTime) / 1.60f);
    enemy_.ApplyVictoryDefeatPose(enemyRatio, kEnemyStartPos, kPlayerPos);

    if (!impactEmitted_ && sceneTime_ >= kImpactTime) {
        impactEmitted_ = true;
        EmitImpactBurst();
    }
    if (particlesReady_) {
        impactParticles_.Update(deltaTime);
    }
}

void GameVictoryScene::EmitImpactBurst() {
    if (!particlesReady_) {
        return;
    }

    ParticleEmitterSettings settings{};
    settings.position = {kEnemyStartPos.x, kEnemyStartPos.y + 1.18f,
                         kEnemyStartPos.z};
    settings.emissionType = ParticleEmissionType::Burst;
    settings.spawnShape = ParticleSpawnShape::Box;
    settings.burstCount = 460;
    settings.maxParticles = 460;
    settings.spawnOffsetScale = {0.45f, 0.38f, 0.45f};
    settings.tintColor = {1.0f, 0.86f, 0.34f, 0.96f};
    settings.direction = {0.45f, 0.72f, 0.16f};
    settings.velocityBias = {1.25f, 0.42f, 0.32f};
    settings.directionalVelocity = 2.80f;
    settings.radialVelocity = 2.15f;
    settings.baseLifeTime = 1.20f;
    settings.lifeTimeRandom = 0.45f;
    settings.startScale = 0.085f;
    settings.endScale = 0.018f;
    settings.scaleRandom = 0.055f;
    settings.stretch = 1.70f;
    settings.acceleration = {0.0f, -0.56f, 0.0f};
    settings.turbulence = 0.38f;
    settings.damping = 0.94f;
    settings.fadeInTime = 0.02f;
    settings.fadeOutTime = 0.46f;
    impactParticles_.EmitOnce(settings);
}

void GameVictoryScene::UpdateCamera(float screenWidth, float screenHeight) {
    camera_.SetAspect(screenWidth / (std::max)(screenHeight, 1.0f));
    const float orbit = 0.18f * std::sinf(sceneTime_ * 0.72f);
    const XMFLOAT3 target{0.20f, 1.15f, 0.18f};
    const XMFLOAT3 eye{-3.75f + orbit, 2.15f,
                       -4.60f + 0.36f * std::sinf(sceneTime_ * 0.44f)};
    camera_.SetPerspectiveFovDeg(43.0f);
    camera_.SetPosition(eye);
    camera_.SetRotation(CameraRotationLookAt(eye, target));
}

void GameVictoryScene::DrawWorld() {
    ModelManager *model = ctx_->rendering.model;
    if (model == nullptr) {
        return;
    }

    SceneLighting lighting{};
    lighting.keyLightDirection = {-0.52f, -0.70f, 0.34f};
    lighting.keyLightColor = {1.30f, 1.12f, 0.88f, 1.0f};
    lighting.fillLightDirection = {0.68f, -0.26f, -0.46f};
    lighting.fillLightColor = {0.22f, 0.32f, 0.46f, 0.58f};
    lighting.ambientColor = {0.22f, 0.22f, 0.24f, 1.0f};
    lighting.lightingParams = {46.0f, 0.32f, 1.22f, 0.18f};
    model->SetSceneLighting(lighting);

    SceneFog fog{};
    fog.params = {0.0f, 0.0f, 1.0f, 0.0f};
    model->SetSceneFog(fog);

    model->PrepareSkinning({playerModelId_, enemyModelId_});
    if (particlesReady_) {
        impactParticles_.DispatchPendingUpdate();
    }
    model->PreDraw();
    DrawStage();
    player_.Draw(model, camera_, true, true, 1.0f);
    if (!(impactEmitted_ && sceneTime_ > 2.72f)) {
        enemy_.Draw(model, camera_, 1.0f);
    }
    DrawSlash();
    model->PostDraw();
    if (particlesReady_) {
        impactParticles_.Draw(camera_);
    }
}

void GameVictoryScene::DrawStage() {
    Transform floor{};
    floor.position = {0.0f, -0.04f, 0.0f};
    floor.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    floor.scale = {44.0f, 44.0f, 1.0f};
    ctx_->rendering.model->Draw(floorModelId_, floor, camera_);
}

void GameVictoryScene::DrawSlash() {
    const float life = 1.0f - SmoothStep01((sceneTime_ - 1.15f) / 0.62f);
    const float appear = SmoothStep01((sceneTime_ - 0.18f) / 0.22f);
    const float alpha = std::clamp(life * appear, 0.0f, 1.0f);
    if (alpha <= 0.01f || slashModelId_ == 0) {
        return;
    }

    ModelDrawEffect effect{};
    effect.enabled = true;
    effect.additiveBlend = true;
    effect.disableCulling = true;
    effect.color = {1.0f, 0.84f, 0.22f, alpha};
    effect.intensity = 0.24f * alpha;
    effect.fresnelPower = 0.35f;
    effect.noiseAmount = 0.0f;
    effect.time = sceneTime_;
    ctx_->rendering.model->SetDrawEffect(effect);

    Transform slash{};
    slash.position = {0.08f, 1.12f, 0.05f};
    slash.rotation = MakeQuat(-0.24f, 0.92f, -0.62f);
    slash.scale = {4.05f, 0.98f, 1.0f};
    ctx_->rendering.model->Draw(slashModelId_, slash, camera_);

    Transform after{};
    after.position = {0.34f, 0.96f, 0.28f};
    after.rotation = MakeQuat(-0.18f, 0.86f, -0.42f);
    after.scale = {3.30f, 0.52f, 1.0f};
    ctx_->rendering.model->Draw(slashModelId_, after, camera_);
    ctx_->rendering.model->ClearDrawEffect();
}

void GameVictoryScene::DrawOverlay(float screenWidth, float screenHeight) {
    const float impactFlash =
        (std::max)(0.0f, 1.0f - std::fabs(sceneTime_ - kImpactTime) / 0.18f);
    const float opening = 1.0f - SmoothStep01(sceneTime_ / 0.36f);
    const float closing =
        SmoothStep01((sceneTime_ - (kDuration - 0.48f)) / 0.48f);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f, 0.30f * opening + 0.92f * closing));
    if (impactFlash > 0.01f) {
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                 Color(1.0f, 0.82f, 0.32f, 0.38f * impactFlash));
    }
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight * 0.16f,
             Color(0.0f, 0.0f, 0.0f, 0.24f));
    DrawRect(0.0f, screenHeight * 0.84f, screenWidth, screenHeight * 0.16f,
             Color(0.0f, 0.0f, 0.0f, 0.34f));
}

void GameVictoryScene::DrawRect(float x, float y, float w, float h,
                                const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.textureId = 0;
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void GameVictoryScene::Finish() {
    if (finishStarted_) {
        return;
    }
    finishStarted_ = true;
    if (ctx_ != nullptr && ctx_->rendering.postProcessSystem != nullptr) {
        ctx_->rendering.postProcessSystem->SetProfile(PostProcessProfile{});
    }
    sceneManager_->ChangeScene(std::make_unique<BattleResultScene>(
        BattleResultScene::ResultKind::Clear, clearTime_, inputCalibration_,
        combatDifficulty_));
}
