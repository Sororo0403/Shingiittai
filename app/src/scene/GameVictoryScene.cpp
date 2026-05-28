#include "GameVictoryScene.h"
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
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

using namespace DirectX;

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kDuration = 4.15f;
constexpr float kImpactTime = 0.92f;
constexpr float kExplosionFreezeTime = 1.18f;
constexpr float kPierceAnimDuration = 1.55f;
constexpr float kResultBlurDelay = 0.26f;
constexpr float kResultBlurDuration = 0.72f;
constexpr float kResultTextDelay = 0.06f;
constexpr XMFLOAT3 kPlayerPierceStartPos{0.0f, 0.0f, 2.05f};
constexpr XMFLOAT3 kPlayerAfterPos{0.0f, 0.0f, -1.32f};
constexpr XMFLOAT3 kEnemyStartPos{0.0f, 0.0f, 0.0f};
constexpr float kFreezeEnemyDefeatRatio = 1.0f;

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

XMFLOAT3 Lerp3(const XMFLOAT3 &a, const XMFLOAT3 &b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t};
}

uint32_t Hash2D(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return h ^ (h >> 16u);
}

uint32_t CreateVictoryEnemyTexture(TextureManager *texture) {
    constexpr uint32_t kSize = 512;
    std::vector<uint8_t> pixels(static_cast<size_t>(kSize) * kSize * 4u);
    for (uint32_t y = 0; y < kSize; ++y) {
        for (uint32_t x = 0; x < kSize; ++x) {
            const uint32_t h = Hash2D(x / 3u, y / 3u, 0x914Au);
            const float noise = static_cast<float>(h & 255u) / 255.0f;
            const float broad =
                static_cast<float>(Hash2D(x / 13u, y / 13u, 0x915Bu) & 255u) /
                255.0f;
            const float t = std::clamp(noise * 0.62f + broad * 0.38f, 0.0f, 1.0f);
            const XMFLOAT3 base{0.30f, 0.24f, 0.12f};
            const XMFLOAT3 accent{0.82f, 0.66f, 0.28f};
            const float scratch =
                (Hash2D(x, y, 0x916Cu) & 63u) == 0u ? 0.10f : 0.0f;
            const size_t index = (static_cast<size_t>(y) * kSize + x) * 4u;
            pixels[index + 0] = static_cast<uint8_t>(
                std::clamp(base.x + (accent.x - base.x) * t + scratch, 0.0f,
                           1.0f) *
                255.0f);
            pixels[index + 1] = static_cast<uint8_t>(
                std::clamp(base.y + (accent.y - base.y) * t + scratch * 0.45f,
                           0.0f, 1.0f) *
                255.0f);
            pixels[index + 2] = static_cast<uint8_t>(
                std::clamp(base.z + (accent.z - base.z) * t, 0.0f, 1.0f) *
                255.0f);
            pixels[index + 3] = 255u;
        }
    }
    return texture->CreateFromRgbaPixels(kSize, kSize, pixels.data());
}

void ApplyVictoryEnemyMaterial(ModelManager *modelManager, uint32_t modelId,
                               uint32_t textureId) {
    if (modelManager == nullptr || modelId == 0 || textureId == 0) {
        return;
    }

    Model *model = modelManager->GetModel(modelId);
    if (model == nullptr) {
        return;
    }

    const XMFLOAT4 palette[] = {
        {0.66f, 0.52f, 0.28f, 1.0f},
        {0.48f, 0.38f, 0.20f, 1.0f},
        {0.76f, 0.64f, 0.36f, 1.0f},
    };
    model->textureId = textureId;
    size_t colorIndex = 0;
    for (ModelSubMesh &subMesh : model->subMeshes) {
        subMesh.textureId = textureId;

        Material material = modelManager->GetMaterial(subMesh.materialId);
        material.enableTexture = 1;
        material.baseColorTextureId = textureId;
        material.color = palette[colorIndex % std::size(palette)];
        material.color.w = 1.0f;
        XMStoreFloat4x4(&material.uvTransform,
                        XMMatrixTranspose(XMMatrixIdentity()));
        material.reflectionStrength = 0.25f;
        material.reflectionFresnelStrength = 0.11f;
        material.reflectionRoughness = 0.46f;
        material.roughness = 0.46f;
        material.blendMode = static_cast<int32_t>(BlendMode::Opaque);
        material.depthWrite = 1;
        material.alphaCutoff = 0.5f;
        material.customParams = {0.0f, 0.0f, 0.0f, 0.0f};
        material.customParams2 = {0.0f, 0.0f, 0.0f, 0.0f};
        material.customParams3 = {0.0f, 0.0f, 0.0f, 0.0f};
        modelManager->SetMaterial(subMesh.materialId, material);
        ++colorIndex;
    }
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

PostProcessProfile MakeVictoryPostProcessProfile(float radialBlurStrength) {
    PostProcessProfile profile{};
    profile.vignette.enabled = true;
    profile.vignette.strength = 0.64f;
    profile.vignette.radius = 0.58f;
    profile.vignette.scale = 18.0f;
    profile.radialBlur.center[0] = 0.5f;
    profile.radialBlur.center[1] = 0.50f;
    profile.radialBlur.sampleCount = radialBlurStrength > 0.0f ? 32 : 1;
    profile.radialBlur.strength = radialBlurStrength;
    profile.sceneDim.strength = 0.16f;
    profile.bloom.enabled = true;
    profile.bloom.intensity = 0.38f;
    profile.bloom.threshold = 0.42f;
    return profile;
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
    resultTimer_ = 0.0f;
    currentScore_ = ComputeScore(clearTime_, combatDifficulty_);
    impactEmitted_ = false;
    resultMode_ = false;

    const float aspect = static_cast<float>(ctx_->systems.winApp->GetWidth()) /
                         static_cast<float>(ctx_->systems.winApp->GetHeight());
    camera_.Initialize(aspect);
    camera_.SetClipRange(0.05f, 90.0f);

    if (ctx_->rendering.postProcessSystem != nullptr) {
        ctx_->rendering.postProcessSystem->SetProfile(
            MakeVictoryPostProcessProfile(0.0f));
    }
    if (ctx_->rendering.dxCommon != nullptr) {
        ctx_->rendering.dxCommon->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }

    ModelManager *model = ctx_->rendering.model;
    playerModelId_ = model->Load(L"app/resources/models/player/player.glb");
    swordModelId_ = model->Load(L"app/resources/models/player/sword.glb");
    enemyModelId_ = model->Load(L"app/resources/models/boss/boss.gltf");
    enemyTextureId_ = CreateVictoryEnemyTexture(ctx_->rendering.texture);
    ApplyVictoryEnemyMaterial(model, enemyModelId_, enemyTextureId_);

    player_.Initialize(playerModelId_, swordModelId_);
    player_.SetInputCalibration(inputCalibration_);
    player_.SetCinematicBladeClashPose(kPlayerAfterPos, kPi, 1.0f, true);
    explosionEnemyTransform_ = Transform{};

    enemy_.SetDifficulty(combatDifficulty_);
    enemy_.Initialize(enemyModelId_);
    enemy_.ApplyVictoryDefeatPose(kFreezeEnemyDefeatRatio, kEnemyStartPos,
                                  kPlayerAfterPos);

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
                                    12000);
        particlesReady_ = true;
    }

    missionCompleteLabel_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/mission_complete.png");
    clearTimeLabel_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/clear_time.png");
    scoreTitleLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/score_title.png");
    for (int i = 0; i < 10; ++i) {
        digitImages_[static_cast<size_t>(i)] =
            LoadTextureImage(L"app/resources/ui/result/mplus/glyphs/char_" +
                             std::to_wstring(i) + L".png");
    }
    colonImage_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/glyphs/char_colon.png");
    dotImage_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/glyphs/char_dot.png");
    dashImage_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/glyphs/char_dash.png");
    secondImage_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/glyphs/char_s.png");
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
    if (!resultMode_ && (skip || sceneTime_ >= kDuration)) {
        BeginResult();
        return;
    }
    if (resultMode_) {
        resultTimer_ += deltaTime;
        UpdateResultPostProcess();
    }
}

void GameVictoryScene::Draw() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());
    UpdateCamera(w, h);
    DrawWorld();
    if (resultMode_) {
        return;
    }

    ctx_->rendering.sprite->PreDraw();
    DrawOverlay(w, h);
    if (resultMode_) {
        DrawResultOverlay(w, h);
    }
    ctx_->rendering.sprite->PostDraw();
}

void GameVictoryScene::DrawTransparent() {}

void GameVictoryScene::DrawPostProcessOverlay() {
    if (!resultMode_) {
        return;
    }

    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());
    ctx_->rendering.sprite->PreDraw();
    DrawOverlay(w, h);
    DrawResultOverlay(w, h);
    ctx_->rendering.sprite->PostDraw();
}

void GameVictoryScene::UpdateCinematic(float deltaTime) {
    const float rawPierceT =
        std::clamp(sceneTime_ / kPierceAnimDuration, 0.0f, 1.0f);
    const float pierceT = SmoothStep01(rawPierceT);
    const float thrustPulse =
        std::sinf(rawPierceT * kPi) *
        (0.35f + 0.65f * (1.0f - SmoothStep01(rawPierceT)));
    const XMFLOAT3 playerPos =
        Lerp3(kPlayerPierceStartPos, kPlayerAfterPos, pierceT);
    const float playerYaw = kPi + 0.08f * thrustPulse;
    const float poseRatio = 0.38f + 0.62f * pierceT;
    player_.SetCinematicBladeClashPose(playerPos, playerYaw, poseRatio, true);

    const float enemyDefeatRatio = 0.24f + 0.76f * SmoothStep01(rawPierceT);
    enemy_.ApplyVictoryDefeatPose(enemyDefeatRatio, kEnemyStartPos,
                                  kPlayerAfterPos);

    if (!impactEmitted_ && sceneTime_ >= kImpactTime) {
        impactEmitted_ = true;
        explosionEnemyTransform_ = enemy_.GetTransform();
        EmitImpactBurst();
    }
    if (particlesReady_ &&
        (!impactEmitted_ || sceneTime_ < kExplosionFreezeTime)) {
        impactParticles_.Update(deltaTime);
    }
}

void GameVictoryScene::EmitImpactBurst() {
    if (!particlesReady_) {
        return;
    }

    const XMFLOAT3 center{kEnemyStartPos.x, kEnemyStartPos.y + 1.08f,
                          kEnemyStartPos.z - 0.02f};

    auto emit = [&](ParticleEmitterSettings settings) {
        settings.position = center;
        settings.emissionType = ParticleEmissionType::Burst;
        impactParticles_.EmitOnce(settings);
    };

    ParticleEmitterSettings fireball{};
    fireball.spawnShape = ParticleSpawnShape::Sphere;
    fireball.burstCount = 3600;
    fireball.maxParticles = 3600;
    fireball.spawnOffsetScale = {1.18f, 0.82f, 1.04f};
    fireball.tintColor = {1.0f, 0.70f, 0.18f, 0.98f};
    fireball.direction = {0.0f, 0.18f, -1.0f};
    fireball.velocityBias = {0.0f, 0.16f, -0.36f};
    fireball.directionalVelocity = 2.40f;
    fireball.radialVelocity = 8.80f;
    fireball.baseLifeTime = 4.80f;
    fireball.lifeTimeRandom = 1.45f;
    fireball.startScale = 0.34f;
    fireball.endScale = 0.18f;
    fireball.scaleRandom = 0.34f;
    fireball.stretch = 3.60f;
    fireball.acceleration = {0.0f, -0.06f, 0.0f};
    fireball.turbulence = 0.42f;
    fireball.damping = 0.955f;
    fireball.fadeInTime = 0.01f;
    fireball.fadeOutTime = 1.90f;
    emit(fireball);

    ParticleEmitterSettings shock{};
    shock.spawnShape = ParticleSpawnShape::Ring;
    shock.burstCount = 2400;
    shock.maxParticles = 2400;
    shock.spawnOffsetScale = {1.65f, 0.22f, 1.65f};
    shock.tintColor = {1.0f, 0.92f, 0.46f, 0.92f};
    shock.direction = {0.0f, 0.08f, -1.0f};
    shock.velocityBias = {0.0f, 0.08f, -0.24f};
    shock.directionalVelocity = 1.70f;
    shock.radialVelocity = 11.20f;
    shock.baseLifeTime = 3.20f;
    shock.lifeTimeRandom = 0.80f;
    shock.startScale = 0.18f;
    shock.endScale = 0.055f;
    shock.scaleRandom = 0.16f;
    shock.stretch = 6.40f;
    shock.acceleration = {0.0f, -0.04f, 0.0f};
    shock.turbulence = 0.18f;
    shock.damping = 0.975f;
    shock.fadeInTime = 0.0f;
    shock.fadeOutTime = 1.18f;
    emit(shock);

    ParticleEmitterSettings pierceJet{};
    pierceJet.spawnShape = ParticleSpawnShape::Box;
    pierceJet.burstCount = 2500;
    pierceJet.maxParticles = 2500;
    pierceJet.spawnOffsetScale = {0.72f, 0.46f, 1.62f};
    pierceJet.tintColor = {1.0f, 0.80f, 0.26f, 0.96f};
    pierceJet.direction = {0.0f, 0.04f, -1.0f};
    pierceJet.velocityBias = {0.0f, 0.06f, -1.18f};
    pierceJet.directionalVelocity = 5.80f;
    pierceJet.radialVelocity = 5.40f;
    pierceJet.baseLifeTime = 5.60f;
    pierceJet.lifeTimeRandom = 1.55f;
    pierceJet.startScale = 0.22f;
    pierceJet.endScale = 0.10f;
    pierceJet.scaleRandom = 0.20f;
    pierceJet.stretch = 8.60f;
    pierceJet.acceleration = {0.0f, -0.06f, 0.0f};
    pierceJet.turbulence = 0.30f;
    pierceJet.damping = 0.968f;
    pierceJet.fadeInTime = 0.0f;
    pierceJet.fadeOutTime = 1.70f;
    emit(pierceJet);

    ParticleEmitterSettings smoke{};
    smoke.spawnShape = ParticleSpawnShape::Sphere;
    smoke.burstCount = 2200;
    smoke.maxParticles = 2200;
    smoke.spawnOffsetScale = {1.42f, 0.88f, 1.22f};
    smoke.tintColor = {0.42f, 0.36f, 0.26f, 0.72f};
    smoke.direction = {0.0f, 0.34f, -0.48f};
    smoke.velocityBias = {0.0f, 0.26f, -0.10f};
    smoke.directionalVelocity = 1.05f;
    smoke.radialVelocity = 3.35f;
    smoke.baseLifeTime = 7.20f;
    smoke.lifeTimeRandom = 2.10f;
    smoke.startScale = 0.42f;
    smoke.endScale = 0.64f;
    smoke.scaleRandom = 0.36f;
    smoke.stretch = 1.35f;
    smoke.acceleration = {0.0f, 0.08f, 0.0f};
    smoke.turbulence = 0.62f;
    smoke.damping = 0.942f;
    smoke.fadeInTime = 0.08f;
    smoke.fadeOutTime = 2.80f;
    smoke.fadeOutPower = 1.35f;
    emit(smoke);
}

void GameVictoryScene::UpdateCamera(float screenWidth, float screenHeight) {
    camera_.SetAspect(screenWidth / (std::max)(screenHeight, 1.0f));
    const float slowT =
        SmoothStep01(std::clamp(sceneTime_ / kPierceAnimDuration, 0.0f, 1.0f));
    const XMFLOAT3 target{0.06f, 0.98f, 0.16f - 0.10f * slowT};
    const XMFLOAT3 eye{-0.92f + 0.10f * slowT, 1.16f,
                       -5.34f + 0.20f * slowT};
    camera_.SetPerspectiveFovDeg(44.0f - 1.4f * slowT);
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

    if (ctx_->rendering.dxCommon != nullptr) {
        ctx_->rendering.dxCommon->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }

    model->PrepareSkinning({playerModelId_, enemyModelId_});
    if (particlesReady_) {
        impactParticles_.DispatchPendingUpdate();
    }
    model->PreDraw();
    player_.Draw(model, camera_, true, true, 1.0f);
    if (!impactEmitted_) {
        enemy_.Draw(model, camera_, 1.0f);
    } else {
        DrawExplodingEnemy();
    }
    DrawSlash();
    model->PostDraw();
    if (particlesReady_ && impactEmitted_) {
        impactParticles_.Draw(camera_);
    }
}

void GameVictoryScene::DrawStage() {
    DrawBattleArena(ctx_->rendering.model, camera_, arenaModels_, sceneTime_);
}

void GameVictoryScene::DrawExplodingEnemy() {
    if (!impactEmitted_ || enemyModelId_ == 0 || ctx_->rendering.model == nullptr) {
        return;
    }

    const float age = (std::max)(0.0f, sceneTime_ - kImpactTime);
    const float vanish = SmoothStep01((age - 0.34f) / 1.10f);
    const float alpha = 1.0f - vanish;
    if (alpha <= 0.01f) {
        return;
    }

    Transform enemy = explosionEnemyTransform_;
    const float rupture = 1.0f + 0.075f * (1.0f - vanish);
    enemy.scale.x *= rupture;
    enemy.scale.y *= 0.98f + 0.045f * (1.0f - vanish);
    enemy.scale.z *= rupture;

    ModelDrawEffect blastCore{};
    blastCore.enabled = true;
    blastCore.blendOverride =
        alpha < 0.98f ? ModelDrawEffectBlendOverride::Alpha
                      : ModelDrawEffectBlendOverride::KeepMaterial;
    blastCore.color = {1.0f, 0.74f, 0.22f, 0.82f * alpha};
    blastCore.intensity = 1.10f * alpha;
    blastCore.fresnelPower = 0.70f;
    blastCore.noiseAmount = 0.42f;
    blastCore.surfaceTint = 0.52f;
    blastCore.alphaBoost = 0.64f * alpha;
    blastCore.time = sceneTime_ * 3.0f;
    ctx_->rendering.model->SetDrawEffect(blastCore);
    ctx_->rendering.model->Draw(enemyModelId_, enemy, camera_);

    Transform rim = enemy;
    rim.scale.x *= 1.085f;
    rim.scale.y *= 1.045f;
    rim.scale.z *= 1.085f;
    ModelDrawEffect blastRim{};
    blastRim.enabled = true;
    blastRim.additiveBlend = true;
    blastRim.disableCulling = true;
    blastRim.color = {1.0f, 0.94f, 0.58f, 0.34f * alpha};
    blastRim.intensity = 0.88f * alpha;
    blastRim.fresnelPower = 0.55f;
    blastRim.noiseAmount = 0.22f;
    blastRim.time = sceneTime_ * 4.0f;
    ctx_->rendering.model->SetDrawEffect(blastRim);
    ctx_->rendering.model->Draw(enemyModelId_, rim, camera_);
    ctx_->rendering.model->ClearDrawEffect();
}

void GameVictoryScene::DrawSlash() {
    const float rawPierceT =
        std::clamp(sceneTime_ / kPierceAnimDuration, 0.0f, 1.0f);
    const float pierceT = SmoothStep01(rawPierceT);
    const float life = 1.0f - SmoothStep01((sceneTime_ - 1.70f) / 2.20f);
    const float speedStress = std::sinf(rawPierceT * kPi);
    const float alpha =
        std::clamp((0.32f + 0.34f * speedStress) * life, 0.0f, 0.62f);
    if (alpha <= 0.01f || slashModelId_ == 0) {
        return;
    }

    ModelDrawEffect effect{};
    effect.enabled = true;
    effect.additiveBlend = true;
    effect.disableCulling = true;
    effect.color = {1.0f, 0.84f, 0.28f, alpha};
    effect.intensity = 0.22f * alpha;
    effect.fresnelPower = 0.35f;
    effect.noiseAmount = 0.0f;
    effect.time = sceneTime_;
    ctx_->rendering.model->SetDrawEffect(effect);

    Transform slash{};
    slash.position = {0.0f, 1.08f, 0.80f - 1.42f * pierceT};
    slash.rotation = MakeQuat(-0.04f, 0.0f, -kPi * 0.5f);
    slash.scale = {5.45f, 0.34f + 0.12f * speedStress, 1.0f};
    ctx_->rendering.model->Draw(slashModelId_, slash, camera_);

    Transform after{};
    after.position = {0.0f, 0.82f, 1.26f - 1.72f * pierceT};
    after.rotation = MakeQuat(-0.02f, 0.0f, -kPi * 0.5f);
    after.scale = {4.45f, 0.18f, 1.0f};
    ctx_->rendering.model->Draw(slashModelId_, after, camera_);

    Transform ghost{};
    ghost.position = {0.0f, 1.28f, 1.58f - 1.96f * pierceT};
    ghost.rotation = MakeQuat(-0.08f, 0.0f, -kPi * 0.5f);
    ghost.scale = {3.90f, 0.13f, 1.0f};
    ctx_->rendering.model->Draw(slashModelId_, ghost, camera_);
    ctx_->rendering.model->ClearDrawEffect();
}

void GameVictoryScene::DrawOverlay(float screenWidth, float screenHeight) {
    const float impactFlash =
        (std::max)(0.0f, 1.0f - std::fabs(sceneTime_ - kImpactTime) / 0.12f);
    const float opening = 1.0f - SmoothStep01(sceneTime_ / 0.36f);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f, 0.30f * opening));
    if (impactFlash > 0.01f) {
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                 Color(1.0f, 0.82f, 0.32f, 0.20f * impactFlash));
    }
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight * 0.16f,
             Color(0.0f, 0.0f, 0.0f, 0.24f));
    DrawRect(0.0f, screenHeight * 0.84f, screenWidth, screenHeight * 0.16f,
             Color(0.0f, 0.0f, 0.0f, 0.34f));
}

void GameVictoryScene::BeginResult() {
    if (resultMode_) {
        return;
    }
    resultMode_ = true;
    resultTimer_ = 0.0f;
    UpdateResultPostProcess();
}

void GameVictoryScene::UpdateResultPostProcess() {
    if (ctx_ != nullptr && ctx_->rendering.postProcessSystem != nullptr) {
        const float blurT =
            SmoothStep01((resultTimer_ - kResultBlurDelay) / kResultBlurDuration);
        ctx_->rendering.postProcessSystem->SetProfile(
            MakeVictoryPostProcessProfile(0.060f * blurT));
    }
}

void GameVictoryScene::DrawResultOverlay(float screenWidth, float screenHeight) {
    const float fade =
        SmoothStep01((resultTimer_ - kResultTextDelay) / 0.48f);

    const float titleScale =
        std::clamp(screenWidth * 0.58f /
                       (std::max)(missionCompleteLabel_.width, 1.0f),
                   0.50f, 0.88f);
    const float titleW = missionCompleteLabel_.width * titleScale;
    DrawImage(missionCompleteLabel_, (screenWidth - titleW) * 0.5f,
              screenHeight * 0.055f, titleScale, fade);

    const float panelT =
        SmoothStep01((resultTimer_ - (kResultTextDelay + 0.08f)) / 0.46f);
    const float panelW = std::clamp(screenWidth * 0.66f, 740.0f, 1040.0f);
    const float panelH = std::clamp(screenHeight * 0.36f, 292.0f, 390.0f);
    const float panelX = (screenWidth - panelW) * 0.5f;
    const float panelY = screenHeight * 0.34f + (1.0f - panelT) * 22.0f;

    DrawRect(panelX + 14.0f, panelY + 16.0f, panelW, panelH,
             Color(0.0f, 0.0f, 0.0f, 0.32f * panelT));
    DrawRect(panelX, panelY, panelW, panelH,
             Color(0.018f, 0.021f, 0.026f, 0.82f * panelT));
    DrawFrame(panelX, panelY, panelW, panelH, 2.0f,
              Color(0.95f, 0.72f, 0.28f, 0.74f * panelT));

    const float scoreLabelScale =
        std::clamp((panelW * 0.20f) /
                       (std::max)(scoreTitleLabel_.width, 1.0f),
                   0.50f, 0.86f);
    const float scoreLabelW = scoreTitleLabel_.width * scoreLabelScale;
    DrawImage(scoreTitleLabel_, panelX + (panelW - scoreLabelW) * 0.5f,
              panelY + panelH * 0.12f, scoreLabelScale, 0.92f * panelT);

    const int animatedScore = static_cast<int>(
        std::round(static_cast<float>(currentScore_) *
                   SmoothStep01((resultTimer_ - (kResultTextDelay + 0.22f)) /
                                0.92f)));
    const std::string score = FormatScore(animatedScore);
    const float scoreScale =
        std::clamp((panelW * 0.48f) / MeasureTextLine(score, 1.0f), 1.24f,
                   1.88f);
    DrawTextLine(score, panelX + panelW * 0.5f, panelY + panelH * 0.41f,
                 scoreScale, panelT);

    const float timeLabelScale =
        std::clamp((panelW * 0.16f) /
                       (std::max)(clearTimeLabel_.width, 1.0f),
                   0.36f, 0.58f);
    DrawImage(clearTimeLabel_, panelX + panelW * 0.35f,
              panelY + panelH * 0.76f, timeLabelScale, 0.72f * panelT);

    const std::string time = FormatTime(clearTime_);
    const float timeScale =
        std::clamp((panelW * 0.22f) / MeasureTextLine(time, 1.0f), 0.38f,
                   0.58f);
    DrawTextLineLeft(time, panelX + panelW * 0.53f, panelY + panelH * 0.75f,
                     timeScale, 0.86f * panelT);
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

void GameVictoryScene::DrawImage(const Image &image, float x, float y,
                                 float scale, float alpha) {
    if (image.textureId == 0 || image.width <= 0.0f || image.height <= 0.0f) {
        return;
    }

    Sprite sprite{};
    sprite.textureId = image.textureId;
    sprite.position = {x, y};
    sprite.size = {image.width * scale, image.height * scale};
    sprite.color = {1.0f, 1.0f, 1.0f, alpha};
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void GameVictoryScene::DrawFrame(float x, float y, float w, float h,
                                 float thickness, const XMFLOAT4 &color) {
    DrawRect(x, y, w, thickness, color);
    DrawRect(x, y + h - thickness, w, thickness, color);
    DrawRect(x, y, thickness, h, color);
    DrawRect(x + w - thickness, y, thickness, h, color);
}

void GameVictoryScene::DrawTextLine(const std::string &text, float centerX,
                                    float y, float scale, float alpha) {
    const float x = centerX - MeasureTextLine(text, scale) * 0.5f;
    DrawTextLineLeft(text, x, y, scale, alpha);
}

void GameVictoryScene::DrawTextLineLeft(const std::string &text, float x,
                                        float y, float scale, float alpha) {
    for (char c : text) {
        if (c == ' ') {
            x += GetCharAdvance(c) * scale;
            continue;
        }
        const Image *image = FindCharImage(c);
        if (image == nullptr) {
            continue;
        }
        DrawImage(*image, x, y, scale, alpha);
        x += GetCharAdvance(c) * scale;
    }
}

float GameVictoryScene::GetCharAdvance(char c) const {
    if (c >= '0' && c <= '9') {
        return 44.0f;
    }
    if (c == ':') {
        return 28.0f;
    }
    if (c == '.') {
        return 22.0f;
    }
    if (c == 's' || c == 'S') {
        return 36.0f;
    }
    if (c == '-') {
        return 34.0f;
    }
    if (c == ' ') {
        return 18.0f;
    }
    return 40.0f;
}

float GameVictoryScene::MeasureTextLine(const std::string &text,
                                        float scale) const {
    float width = 0.0f;
    for (char c : text) {
        if (c == ' ') {
            width += GetCharAdvance(c) * scale;
            continue;
        }
        if (FindCharImage(c) != nullptr) {
            width += GetCharAdvance(c) * scale;
        }
    }
    return (std::max)(0.0f, width);
}

const GameVictoryScene::Image *GameVictoryScene::FindCharImage(char c) const {
    if (c >= '0' && c <= '9') {
        return &digitImages_[static_cast<size_t>(c - '0')];
    }
    if (c == ':') {
        return &colonImage_;
    }
    if (c == '.') {
        return &dotImage_;
    }
    if (c == '-') {
        return &dashImage_;
    }
    if (c == 's' || c == 'S') {
        return &secondImage_;
    }
    return nullptr;
}

GameVictoryScene::Image
GameVictoryScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width =
        static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
    return image;
}

int GameVictoryScene::ComputeScore(float clearTime, float difficulty) const {
    const float p = std::clamp(difficulty / 9.0f, 0.0f, 1.0f);
    const float smooth = p * p * (3.0f - 2.0f * p);
    const float effectiveDifficulty =
        std::clamp(difficulty + 0.55f + 0.45f * smooth, 0.0f, 9.0f);
    const float difficultyBonus =
        1.0f + effectiveDifficulty * 0.22f +
        effectiveDifficulty * effectiveDifficulty * 0.035f;
    const float safeClearTime = (std::max)(clearTime, 0.0f);
    const float timeBonus = 180.0f / (safeClearTime + 30.0f);
    return (std::max)(
        1, static_cast<int>(
               std::round(1000.0f * difficultyBonus * timeBonus)));
}

std::string GameVictoryScene::FormatTime(float seconds) const {
    const int centiseconds =
        static_cast<int>(std::round((std::max)(0.0f, seconds) * 100.0f));
    const int minutes = centiseconds / 6000;
    const int sec = (centiseconds / 100) % 60;
    const int centi = centiseconds % 100;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << minutes << ':'
        << std::setw(2) << sec << '.' << std::setw(2) << centi << 's';
    return oss.str();
}

std::string GameVictoryScene::FormatScore(int score) const {
    return std::to_string((std::max)(0, score));
}
