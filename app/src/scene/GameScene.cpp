#include "GameScene.h"
#include "AppSceneServices.h"
#include "compat/ParticleCompat.h"
#include "BattleResultScene.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "Material.h"
#include "Model.h"
#include "ModelManager.h"
#include "SoundManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include "PostProcessSystem.h"
#include "SceneManager.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <vector>

using namespace DirectX;

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kEnemyAttackFrontLockMinDistance = 1.45f;
constexpr float kEnemyAttackFrontLockMaxDistance = 3.35f;
constexpr float kEnemyAttackFrontLockHalfWidth = 1.20f;
constexpr float kBladeClashWinGuardBreakLead = 0.52f;
constexpr float kBladeClashWinGuardBreakImpactTime = 0.24f;
constexpr float kBladeClashWinActionSlow = 0.95f;
constexpr float kBladeClashGuardBreakSlowStart = 0.21f;
constexpr float kBladeClashGuardBreakSlowEnd = 0.36f;
constexpr float kBladeClashGuardBreakSlowScale = 0.18f;
constexpr float kBladeClashGuardBreakRecoilDistance = 0.72f;
constexpr float kBladeClashGuardBreakDrop = 0.16f;
constexpr float kBladeClashGuardBreakLift = 0.32f;
constexpr float kBladeClashGuardBreakPose = 0.48f;

PostProcessProfile GetPostProcessProfile(const SceneContext *ctx) {
    if (ctx == nullptr || ctx->rendering.postProcessSystem == nullptr) {
        return {};
    }
    return ctx->rendering.postProcessSystem->GetProfile();
}

void SetPostProcessProfile(const SceneContext *ctx,
                           const PostProcessProfile &profile) {
    if (ctx == nullptr || ctx->rendering.postProcessSystem == nullptr) {
        return;
    }
    ctx->rendering.postProcessSystem->SetProfile(profile);
}

void ApplyBattlePostProcess(const SceneContext *ctx, float radialBlurStrength,
                            float vignetteStrength, float sceneDimStrength,
                            float centerY = 0.48f,
                            int32_t sampleCount = 20,
                            float vignetteScale = 11.0f,
                            float vignettePower = 1.15f) {
    PostProcessProfile profile = GetPostProcessProfile(ctx);
    profile.colorGrade.mode = PostProcessColorMode::None;
    profile.vignette.enabled = true;
    profile.vignette.strength = vignetteStrength;
    profile.vignette.scale = vignetteScale;
    profile.vignette.power = vignettePower;
    profile.radialBlur.center[0] = 0.5f;
    profile.radialBlur.center[1] = centerY;
    profile.radialBlur.sampleCount = sampleCount;
    profile.radialBlur.strength = radialBlurStrength;
    profile.sceneDim.strength = sceneDimStrength;
    SetPostProcessProfile(ctx, profile);
}

void ClearBattlePostProcess(const SceneContext *ctx) {
    SetPostProcessProfile(ctx, PostProcessProfile{});
}

struct SharedBattleModels {
    bool initialized = false;
    uint32_t arenaNoiseTextureId = 0;
    uint32_t arenaFloorModelId = 0;
    uint32_t arenaLowPolyTerrainModelId = 0;
    uint32_t arenaDistantTerrainModelId = 0;
    uint32_t arenaHazardSpireModelId = 0;
    uint32_t arenaHazardGlowRingModelId = 0;
    uint32_t arenaCityTowerModelId = 0;
    uint32_t arenaCityWindowModelId = 0;
    uint32_t arenaGiantBodyModelId = 0;
    uint32_t arenaGiantHeadModelId = 0;
    uint32_t arenaCenterDiskModelId = 0;
    uint32_t arenaSpokeModelId = 0;
    uint32_t arenaInnerRingModelId = 0;
    uint32_t arenaOuterRingModelId = 0;
    uint32_t arenaColumnModelId = 0;
    uint32_t arenaColumnCapModelId = 0;
    uint32_t arenaDomeModelId = 0;
    uint32_t arenaBarrierRingModelId = 0;
};

SharedBattleModels gSharedBattleModels;

Material MakeArenaMaterial(const XMFLOAT4 &color, bool useTexture = false,
                           float reflection = 0.34f,
                           float roughness = 0.48f) {
    Material material{};
    material.color = color;
    material.enableTexture = useTexture ? 1 : 0;
    material.reflectionStrength = reflection;
    material.reflectionFresnelStrength = reflection * 0.42f;
    material.reflectionRoughness = roughness;
    return material;
}

XMFLOAT4 MakeQuat(float pitch, float yaw, float roll) {
    XMFLOAT4 q{};
    XMStoreFloat4(&q, XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
    return q;
}

float DistanceSq(const XMFLOAT3 &a, const XMFLOAT3 &b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

uint32_t Hash2D(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return h ^ (h >> 16u);
}

uint32_t CreateProceduralTexture(TextureManager *texture, uint32_t width,
                                 uint32_t height,
                                 const XMFLOAT3 &baseColor,
                                 const XMFLOAT3 &accentColor, uint32_t seed,
                                 float grainStrength) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t h = Hash2D(x / 3u, y / 3u, seed);
            const float noise = static_cast<float>(h & 255u) / 255.0f;
            const float streak =
                static_cast<float>(Hash2D(x / 19u, y / 7u, seed + 17u) & 255u) /
                255.0f;
            const float t = std::clamp(noise * grainStrength +
                                           streak * (1.0f - grainStrength),
                                       0.0f, 1.0f);
            const float fine =
                static_cast<float>(Hash2D(x, y, seed + 31u) & 63u) / 255.0f;
            XMFLOAT3 color{
                std::clamp(baseColor.x + (accentColor.x - baseColor.x) * t + fine,
                           0.0f, 1.0f),
                std::clamp(baseColor.y + (accentColor.y - baseColor.y) * t + fine,
                           0.0f, 1.0f),
                std::clamp(baseColor.z + (accentColor.z - baseColor.z) * t + fine,
                           0.0f, 1.0f),
            };
            const size_t index = (static_cast<size_t>(y) * width + x) * 4u;
            pixels[index + 0] = static_cast<uint8_t>(color.x * 255.0f);
            pixels[index + 1] = static_cast<uint8_t>(color.y * 255.0f);
            pixels[index + 2] = static_cast<uint8_t>(color.z * 255.0f);
            pixels[index + 3] = 255u;
        }
    }
    return texture->CreateFromRgbaPixels(width, height, pixels.data());
}

uint32_t AppCreateRustedMetalTexture(TextureManager *texture, uint32_t width,
                                     uint32_t height) {
    return CreateProceduralTexture(texture, width, height,
                                   {0.23f, 0.22f, 0.20f},
                                   {0.70f, 0.30f, 0.12f}, 0x914Au, 0.62f);
}

uint32_t AppCreateArenaStoneTexture(TextureManager *texture, uint32_t width,
                                    uint32_t height) {
    return CreateProceduralTexture(texture, width, height,
                                   {0.07f, 0.08f, 0.09f},
                                   {0.22f, 0.24f, 0.22f}, 0x51C3u, 0.48f);
}

XMFLOAT3 Lerp(const XMFLOAT3 &a, const XMFLOAT3 &b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t};
}

float SmoothStep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float GetChargeStanceSettleTime(ActionKind kind) {
    switch (kind) {
    case ActionKind::Smash:
        return 0.46f;
    case ActionKind::Sweep:
        return 0.42f;
    case ActionKind::BladeClash:
    case ActionKind::Wave:
        return 0.48f;
    default:
        return 0.42f;
    }
}

bool IsChargeStanceSettled(ActionKind kind, ActionStep step, float timer) {
    if (step == ActionStep::Hold) {
        return true;
    }
    if (step != ActionStep::Charge) {
        return false;
    }
    return timer >= GetChargeStanceSettleTime(kind);
}

void ApplyWeatheredMetalMaterials(ModelManager *modelManager, uint32_t modelId,
                                  uint32_t rustTextureId,
                                  const std::vector<XMFLOAT4> &palette,
                                  float reflection, float fresnel,
                                  float roughness) {
    if (!modelManager || palette.empty()) {
        return;
    }

    Model *model = modelManager->GetModel(modelId);
    if (!model) {
        return;
    }

    size_t colorIndex = 0;
    for (ModelSubMesh &subMesh : model->subMeshes) {
        subMesh.textureId = rustTextureId;

        Material material = modelManager->GetMaterial(subMesh.materialId);
        material.enableTexture = 1;
        material.baseColorTextureId = rustTextureId;
        material.color = palette[colorIndex % palette.size()];
        material.reflectionStrength = reflection;
        material.reflectionFresnelStrength = fresnel;
        material.reflectionRoughness = roughness;
        material.enableDissolve = 0.0f;
        material.dissolveEdgeColor = {1.0f, 0.44f, 0.14f, 1.0f};
        modelManager->SetMaterial(subMesh.materialId, material);
        ++colorIndex;
    }
}

void ApplyRustedRobotMaterials(ModelManager *modelManager, uint32_t modelId,
                               uint32_t rustTextureId) {
    if (!modelManager) {
        return;
    }

    Model *model = modelManager->GetModel(modelId);
    if (!model) {
        return;
    }

    model->textureId = rustTextureId;
    for (ModelSubMesh &subMesh : model->subMeshes) {
        subMesh.textureId = rustTextureId;

        Material material = modelManager->GetMaterial(subMesh.materialId);
        material.enableTexture = 1;
        material.baseColorTextureId = rustTextureId;
        material.color = {0.35f, 0.33f, 0.28f, 1.0f};
        XMStoreFloat4x4(&material.uvTransform,
                        XMMatrixTranspose(XMMatrixIdentity()));
        material.reflectionStrength = 0.045f;
        material.reflectionFresnelStrength = 0.012f;
        material.reflectionRoughness = 0.94f;
        material.enableDissolve = 0.0f;
        material.dissolveEdgeColor = {0.68f, 0.24f, 0.08f, 0.46f};
        modelManager->SetMaterial(subMesh.materialId, material);
    }
}

} // namespace

GameScene::~GameScene() = default;

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    ctx_->rendering.dxCommon->ResetClearColor();
    ctx_->rendering.postProcessSystem->SetProfile(PostProcessProfile{});
    combatFeedback_.Initialize(ctx_->rendering.postProcessSystem);

    float aspect = static_cast<float>(ctx_->systems.winApp->GetWidth()) /
                   static_cast<float>(ctx_->systems.winApp->GetHeight());

    camera_.Initialize(aspect);    camera_.UpdateMatrices();
    camera_.SetPerspectiveFovDeg(currentFovDeg_);

    DirectXCommon *dx = ctx_->rendering.dxCommon;
    ModelManager *model = ctx_->rendering.model;
    TextureManager *texture = ctx_->rendering.texture;

    uint32_t playerModel =
        model->Load(L"app/resources/models/player/player.glb");
    uint32_t swordModel = model->Load(L"app/resources/models/player/sword.glb");
    uint32_t enemyModel = model->Load(L"app/resources/models/boss/boss.gltf");
    uint32_t bulletModel =
        ctx_->rendering.model->Load(L"app/resources/models/bullet/bullet.obj");
    particleTextureId_ = texture->Load(L"app/resources/sprites/smoke.png");
    const uint32_t enemyRustTextureId =
        AppCreateRustedMetalTexture(texture, 512, 512);
    const uint32_t worldRustTextureId =
        AppCreateRustedMetalTexture(texture, 768, 768);
    const uint32_t arenaStoneTextureId =
        AppCreateArenaStoneTexture(texture, 1024, 1024);
    ApplyWeatheredMetalMaterials(model, playerModel, worldRustTextureId,
                                 {{0.22f, 0.31f, 0.56f, 0.98f},
                                  {0.11f, 0.17f, 0.32f, 0.98f},
                                  {0.30f, 0.24f, 0.20f, 0.98f}},
                                 0.12f, 0.06f, 0.78f);
    ApplyWeatheredMetalMaterials(model, swordModel, worldRustTextureId,
                                 {{0.34f, 0.46f, 0.74f, 1.0f},
                                  {0.13f, 0.23f, 0.46f, 1.0f},
                                  {0.34f, 0.25f, 0.21f, 1.0f}},
                                 0.24f, 0.12f, 0.64f);
    ApplyRustedRobotMaterials(model, enemyModel, enemyRustTextureId);
    ApplyWeatheredMetalMaterials(model, bulletModel, worldRustTextureId,
                                 {{0.95f, 0.72f, 0.36f, 1.0f},
                                  {0.70f, 0.78f, 0.76f, 1.0f},
                                  {0.48f, 0.24f, 0.12f, 1.0f}},
                                 0.76f, 0.58f, 0.26f);
    if (!gSharedBattleModels.initialized) {
        gSharedBattleModels.arenaNoiseTextureId = arenaStoneTextureId;
        gSharedBattleModels.arenaFloorModelId = model->CreatePlane(
            arenaStoneTextureId,
        MakeArenaMaterial({0.070f, 0.085f, 0.105f, 1.0f}, true, 0.025f,
                          0.84f));
        gSharedBattleModels.arenaLowPolyTerrainModelId =
            model->CreatePlane(
                arenaStoneTextureId,
                MakeArenaMaterial({0.075f, 0.10f, 0.13f, 1.0f}, true, 0.016f,
                                  0.80f));
        gSharedBattleModels.arenaDistantTerrainModelId =
            model->CreatePlane(
                arenaStoneTextureId,
                MakeArenaMaterial({0.12f, 0.18f, 0.22f, 1.0f}, true, 0.02f,
                                  0.88f));
        gSharedBattleModels.arenaHazardSpireModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.10f, 0.15f, 0.15f, 1.0f}, true, 0.025f,
                              0.88f),
            7, 0.04f, 0.92f, 8.8f);
        gSharedBattleModels.arenaHazardGlowRingModelId = model->CreateRing(
            0, MakeArenaMaterial({0.55f, 0.88f, 0.96f, 0.42f}, false, 0.0f,
                                 0.46f),
            48, 2.45f, 0.34f);
        gSharedBattleModels.arenaCityTowerModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.18f, 0.24f, 0.29f, 1.0f}, true, 0.025f,
                              0.76f),
            4, 0.72f, 0.72f, 1.0f);
        gSharedBattleModels.arenaCityWindowModelId = model->CreatePlane(
            0, MakeArenaMaterial({0.72f, 0.90f, 0.96f, 0.58f}, false, 0.0f,
                                 0.40f));
        gSharedBattleModels.arenaGiantBodyModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.055f, 0.10f, 0.11f, 1.0f}, true, 0.012f,
                              0.92f),
            9, 0.78f, 1.18f, 5.8f);
        gSharedBattleModels.arenaGiantHeadModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.05f, 0.09f, 0.10f, 1.0f}, true, 0.01f,
                              0.94f),
            8, 0.92f, 1.05f, 1.15f);
        gSharedBattleModels.arenaCenterDiskModelId = model->CreateRing(
            arenaStoneTextureId,
            MakeArenaMaterial({0.72f, 0.58f, 0.30f, 1.0f}, false, 0.12f,
                              0.30f),
            96, 1.95f, 0.0f);
        gSharedBattleModels.arenaSpokeModelId = model->CreatePlane(
            arenaStoneTextureId,
        MakeArenaMaterial({0.28f, 0.21f, 0.12f, 1.0f}, false, 0.06f,
                          0.58f));
        gSharedBattleModels.arenaInnerRingModelId = model->CreateRing(
            arenaStoneTextureId,
            MakeArenaMaterial({0.64f, 0.60f, 0.44f, 1.0f}, false, 0.12f,
                              0.32f),
            96, 4.9f, 4.35f);
        gSharedBattleModels.arenaOuterRingModelId = model->CreateRing(
            arenaStoneTextureId,
            MakeArenaMaterial({0.68f, 0.28f, 0.18f, 1.0f}, false, 0.10f,
                              0.38f),
            128, 12.3f, 11.6f);
        gSharedBattleModels.arenaColumnModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.22f, 0.26f, 0.30f, 1.0f}, true, 0.045f,
                              0.68f),
            24, 0.26f, 0.38f, 5.4f);
        gSharedBattleModels.arenaColumnCapModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.62f, 0.30f, 0.18f, 1.0f}, false, 0.10f,
                              0.38f),
            32, 0.68f, 0.78f, 0.24f);
        gSharedBattleModels.arenaDomeModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.09f, 0.14f, 0.28f, 0.78f}, true, 0.00f,
                              0.72f),
            128, 4.5f, 13.5f, 8.8f);
        gSharedBattleModels.arenaBarrierRingModelId = model->CreateRing(
            arenaStoneTextureId,
            MakeArenaMaterial({0.24f, 0.30f, 0.46f, 0.42f}, false, 0.00f,
                              0.34f),
            128, 13.1f, 12.9f);
        gSharedBattleModels.initialized = true;
    }

    arenaNoiseTextureId_ = gSharedBattleModels.arenaNoiseTextureId;
    arenaFloorModelId_ = gSharedBattleModels.arenaFloorModelId;
    arenaLowPolyTerrainModelId_ =
        gSharedBattleModels.arenaLowPolyTerrainModelId;
    arenaDistantTerrainModelId_ =
        gSharedBattleModels.arenaDistantTerrainModelId;
    arenaHazardSpireModelId_ = gSharedBattleModels.arenaHazardSpireModelId;
    arenaHazardGlowRingModelId_ =
        gSharedBattleModels.arenaHazardGlowRingModelId;
    arenaCityTowerModelId_ = gSharedBattleModels.arenaCityTowerModelId;
    arenaCityWindowModelId_ = gSharedBattleModels.arenaCityWindowModelId;
    arenaGiantBodyModelId_ = gSharedBattleModels.arenaGiantBodyModelId;
    arenaGiantHeadModelId_ = gSharedBattleModels.arenaGiantHeadModelId;
    arenaCenterDiskModelId_ = gSharedBattleModels.arenaCenterDiskModelId;
    arenaSpokeModelId_ = gSharedBattleModels.arenaSpokeModelId;
    arenaInnerRingModelId_ = gSharedBattleModels.arenaInnerRingModelId;
    arenaOuterRingModelId_ = gSharedBattleModels.arenaOuterRingModelId;
    arenaColumnModelId_ = gSharedBattleModels.arenaColumnModelId;
    arenaColumnCapModelId_ = gSharedBattleModels.arenaColumnCapModelId;
    arenaDomeModelId_ = gSharedBattleModels.arenaDomeModelId;
    arenaBarrierRingModelId_ = gSharedBattleModels.arenaBarrierRingModelId;
    sparkParticles_.Initialize(dx, ctx_->rendering.srv, texture, particleTextureId_, 2048);
    sparkParticles_.SetEmission(1, 1000.0f);
    sparkParticles_.SetEmitterRadius(0.08f);
    explosionParticles_.Initialize(dx, ctx_->rendering.srv, texture, particleTextureId_,
                                   2048);
    explosionParticles_.SetEmission(1, 1000.0f);
    explosionParticles_.SetEmitterRadius(0.25f);
    smokeParticles_.Initialize(dx, ctx_->rendering.srv, texture, particleTextureId_, 1024);
    smokeParticles_.SetEmission(1, 1000.0f);
    smokeParticles_.SetEmitterRadius(0.40f);

    swordFlashParticles_.Initialize(dx, ctx_->rendering.srv, texture, particleTextureId_,
                                    256);
    swordFlashParticles_.SetEmission(1, 1000.0f);
    swordFlashParticles_.SetEmitterRadius(0.06f);

    swordTrailRenderer_.Initialize(dx);
    swordTrailRenderer_.Reset();
    swordSlashArcRenderer_.Initialize(dx);
    swordSlashArcRenderer_.Reset();
    bladeClashPreviousSlashStates_.fill(false);

    player_.Initialize(playerModel, swordModel);
    player_.SetInputCalibration(inputCalibration_);
    playerModelId_ = playerModel;
    swordModelId_ = swordModel;
    enemy_.Initialize(enemyModel, bulletModel);
    enemyModelId_ = enemyModel;
    enemyProjectileModelId_ = bulletModel;
    if (ctx_->systems.sound != nullptr) {
        slashSoundId_ =
            ctx_->systems.sound->Load(L"app/resources/sounds/slash_hero.wav");
        enemyReleaseSoundId_ =
            ctx_->systems.sound->Load(L"app/resources/sounds/enemy_release_snap.wav");
        hitSoundId_ =
            ctx_->systems.sound->Load(L"app/resources/sounds/hit_impact.wav");
        counterSoundId_ =
            ctx_->systems.sound->Load(L"app/resources/sounds/counter_burst.wav");
        damageSoundId_ =
            ctx_->systems.sound->Load(L"app/resources/sounds/damage_heavy.wav");
        explosionSoundId_ =
            ctx_->systems.sound->Load(L"app/resources/sounds/explosion_boss.wav");
        soundsLoaded_ = true;
    }
    cameraYaw_ = 0.0f;
    cameraPitch_ = 0.0f;
    isLockOn_ = true;
    rushChargeAssistStrength_ = 4.0f;
    rushChargeAssistMaxStep_ = 6.0f;
    rushActiveAssistStrength_ = 5.0f;
    rushActiveAssistMaxStep_ = 8.0f;
    rushLeadDistance_ = 2.5f;
    currentFovDeg_ = normalFovDeg_;
    targetFovDeg_ = normalFovDeg_;

    const DirectX::XMFLOAT3 &playerPos = player_.GetTransform().position;
    const DirectX::XMFLOAT3 &enemyPos = enemy_.GetTransform().position;
    lockOnOrbitCameraPos_ = {playerPos.x, playerPos.y + lockOnOrbitHeight_,
                             playerPos.z - lockOnOrbitRadius_};
    lockOnLookAt_ = playerViewCamera_
                        ? DirectX::XMFLOAT3{enemyPos.x,
                                            enemyPos.y +
                                                playerViewLockOnLookHeight_,
                                            enemyPos.z}
                        : DirectX::XMFLOAT3{
                              playerPos.x * lockOnLookPlayerWeight_ +
                                  enemyPos.x * lockOnLookEnemyWeight_,
                              (playerPos.y + cameraLookHeight_) * 0.52f +
                                  (enemyPos.y + 1.30f) * 0.48f,
                              playerPos.z * lockOnLookPlayerWeight_ +
                                  enemyPos.z * lockOnLookEnemyWeight_};
    if (Model *playerModelData = model->GetModel(playerModelId_)) {
        if (!playerModelData->animations.empty()) {
            model->PlayAnimation(playerModelId_,
                                 playerModelData->currentAnimation, true);
        }
    }
    SyncEnemyAnimation();
    UpdateSceneLighting();
    battleElapsedTime_ = 0.0f;
    battleIntroActive_ = true;
    battleIntroTimer_ = 0.0f;
    battleIntroRevealEmitted_ = false;
    phaseTransitionWasActive_ = false;
    phaseTransitionReleaseEmitted_ = false;
    phaseTransitionLoopTimer_ = 0.0f;
    battleResultRequested_ = false;
    victorySequenceActive_ = false;
    victorySequenceTimer_ = 0.0f;
    victoryClearTime_ = 0.0f;
    defeatSequenceActive_ = false;
    defeatSequenceTimer_ = 0.0f;
    defeatImpactEmitted_ = false;
    player_.SetDefeatPoseRatio(0.0f);
    counterCinematicActive_ = false;
    counterCinematicTimer_ = 0.0f;
    bladeClashActive_ = false;
    bladeClashGauge_ = 0.0f;
    bladeClashTimer_ = 0.0f;
    bladeClashCameraPush_ = 0.0f;
    bladeClashImpactPulse_ = 0.0f;
    bladeClashEnemySurgeTimer_ = 0.0f;
    bladeClashChainTimer_ = 0.0f;
    bladeClashSlashChain_ = 0;
    enemyLastStandPrimed_ = false;
    bladeClashFinal_ = false;
    bladeClashFinalBarrageStep_ = 0;
    bladeClashFinishActive_ = false;
    bladeClashFinishPlayerWon_ = false;
    bladeClashFinishImpactEmitted_ = false;
    bladeClashFinishGuardBreakEmitted_ = false;
    bladeClashFinishSkidEmitted_ = false;
    bladeClashFinishWallImpactEmitted_ = false;
    bladeClashFinishPendingEnemyTransition_ = false;
    bladeClashFinishTimer_ = 0.0f;
    bladeClashFinishCenter_ = {0.0f, 0.0f, 0.0f};
    bladeClashFinishPlayerStart_ = {0.0f, 0.0f, 0.0f};
    bladeClashFinishPlayerEnd_ = {0.0f, 0.0f, 0.0f};
    bladeClashFinishEnemyStart_ = {0.0f, 0.0f, 0.0f};
    bladeClashFinishEnemyEnd_ = {0.0f, 0.0f, 0.0f};
    enemyAnimationFrozen_ = false;
    enemyRedPunishUncounterable_ = false;
    previousCombatSlashStates_.fill(false);
    previousSwordSoundStates_.fill(false);
    handTrackingStartRequested_ = false;
    player_.SetCameraSwordSlashSuppressed(false);
    if (inputCalibration_.controlType == InputControlType::Hand) {
        if (AppSceneServices::HasHandTrackingStart()) {
            AppSceneServices::RequestHandTrackingStart();
            handTrackingStartRequested_ = true;
        }
    }
    hud_.Initialize(*ctx_);
    enemy_.FaceTargetImmediately(player_.GetTransform().position);
    ApplyEnemyIntroDissolve(0.0f);
}

void GameScene::Update() {
    Input *input = ctx_->systems.input;
    const float baseDeltaTime = ctx_->frame.deltaTime;
    if (battleIntroActive_) {
        UpdateBattleIntro(baseDeltaTime);
        return;
    }
    if (victorySequenceActive_) {
        sceneLightTime_ += baseDeltaTime;
        combatFeedback_.Update(baseDeltaTime, sceneLightTime_);
        hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(),
                    enemy_.GetMaxHP());
        UpdateVictorySequence(baseDeltaTime);
        const float victoryPoseRatio = std::clamp(
            (victorySequenceTimer_ - 0.78f) / 2.35f, 0.0f, 1.0f);
        enemy_.ApplyVictoryDefeatPose(
            victoryPoseRatio, victoryEnemyStartPos_,
            player_.GetTransform().position);
        UpdateBattleCamera();
        camera_.UpdateMatrices();
        ctx_->rendering.model->UpdateAnimation(playerModelId_, baseDeltaTime * 0.08f);
        ctx_->rendering.model->UpdateAnimation(enemyModelId_, baseDeltaTime * 0.002f);
        ApplyEnemyProceduralAnimation();
        sparkParticles_.Update(baseDeltaTime);
        explosionParticles_.Update(baseDeltaTime);
        smokeParticles_.Update(baseDeltaTime);
        return;
    }
    if (defeatSequenceActive_) {
        sceneLightTime_ += baseDeltaTime;
        combatFeedback_.Update(baseDeltaTime, sceneLightTime_);
        UpdateDefeatSequence(baseDeltaTime);
        UpdateBattleCamera();
        camera_.UpdateMatrices();
        ctx_->rendering.model->UpdateAnimation(playerModelId_, baseDeltaTime * 0.035f);
        ctx_->rendering.model->UpdateAnimation(enemyModelId_, baseDeltaTime * 0.28f);
        ApplyEnemyProceduralAnimation();
        sparkParticles_.Update(baseDeltaTime);
        explosionParticles_.Update(baseDeltaTime);
        smokeParticles_.Update(baseDeltaTime);
        return;
    }
    if (enemy_.IsPhaseTransitionActive() && !bladeClashFinishActive_) {
        UpdatePhaseTransitionCinematic(baseDeltaTime);
        return;
    }
    phaseTransitionWasActive_ = false;
    phaseTransitionReleaseEmitted_ = false;

    combatFeedback_.Update(baseDeltaTime, sceneLightTime_);
    UpdateBattlePostProcessState(baseDeltaTime);
    if (bladeClashFinishActive_) {
        float bladeClashFinishDeltaTime = baseDeltaTime;
        if (bladeClashFinishPlayerWon_ &&
            bladeClashFinishTimer_ >= kBladeClashGuardBreakSlowStart &&
            bladeClashFinishTimer_ < kBladeClashGuardBreakSlowEnd) {
            const float slowT =
                std::clamp((bladeClashFinishTimer_ -
                            kBladeClashGuardBreakSlowStart) /
                               (kBladeClashGuardBreakSlowEnd -
                                kBladeClashGuardBreakSlowStart),
                           0.0f, 1.0f);
            const float snapHold = std::sinf(slowT * kPi);
            const float slowScale =
                1.0f - (1.0f - kBladeClashGuardBreakSlowScale) * snapHold;
            bladeClashFinishDeltaTime *= slowScale;
        }
        bladeClashFinishTimer_ += bladeClashFinishDeltaTime;
        const float bladeClashWinActionTimer =
            bladeClashFinishPlayerWon_
                ? (std::max)(0.0f, bladeClashFinishTimer_ -
                                        kBladeClashWinGuardBreakLead) /
                      kBladeClashWinActionSlow
                : bladeClashFinishTimer_;
        if (bladeClashFinishPlayerWon_ &&
            !bladeClashFinishGuardBreakEmitted_ &&
            bladeClashFinishTimer_ >= kBladeClashWinGuardBreakImpactTime) {
            bladeClashFinishGuardBreakEmitted_ = true;
            XMFLOAT3 breakCenter = enemy_.GetTransform().position;
            breakCenter.y += 1.22f;
            EmitParticleBurst(explosionParticles_, 
                breakCenter, 92, 0.86f,
                AppParticleBurstStyle::Explosion,
                {1.0f, 0.86f, 0.42f, 0.72f},
                {-bladeClashDirection_.x, 0.16f, -bladeClashDirection_.z},
                1.72f);
            EmitParticleBurst(sparkParticles_, 
                breakCenter, 128, 0.72f, AppParticleBurstStyle::Sparks,
                {1.0f, 0.92f, 0.48f, 0.88f},
                {bladeClashDirection_.z, 0.18f, -bladeClashDirection_.x},
                2.25f);
            EmitParticleBurst(swordFlashParticles_, 
                breakCenter, 10, 0.58f, AppParticleBurstStyle::Flash,
                {1.0f, 1.0f, 0.82f, 0.66f},
                {-bladeClashDirection_.x, 0.0f, -bladeClashDirection_.z},
                0.34f);
            EmitParticleBurst(smokeParticles_, 
                breakCenter, 24, 0.62f, AppParticleBurstStyle::Smoke,
                {0.36f, 0.32f, 0.26f, 0.42f},
                {-bladeClashDirection_.x, 0.06f, -bladeClashDirection_.z},
                0.76f);
            CombatFeedbackEvent guardBreakFeedback{};
            guardBreakFeedback.type =
                CombatFeedbackEventType::BladeClashGuardBreak;
            guardBreakFeedback.position = breakCenter;
            guardBreakFeedback.direction =
                {-bladeClashDirection_.x, 0.0f, -bladeClashDirection_.z};
            guardBreakFeedback.power = bladeClashFinal_ ? 16.0f : 10.5f;
            DispatchCombatFeedback(guardBreakFeedback);
        }
        if (!bladeClashFinishPlayerWon_ && !bladeClashFinishSkidEmitted_ &&
            bladeClashFinishTimer_ >= 0.78f) {
            bladeClashFinishSkidEmitted_ = true;
            bladeClashFinishImpactEmitted_ = true;
            // constexpr float clashLossDamage = 25.0f;
            XMFLOAT3 sweepCenter = player_.GetTransform().position;
            sweepCenter.y += 1.02f;
            EmitParticleBurst(explosionParticles_, 
                sweepCenter, 92, 1.24f,
                AppParticleBurstStyle::SlashLine,
                {1.0f, 0.28f, 0.06f, 0.96f},
                {bladeClashDirection_.z, -0.04f, -bladeClashDirection_.x},
                1.92f);
            EmitParticleBurst(swordFlashParticles_, 
                sweepCenter, 14, 0.92f, AppParticleBurstStyle::Flash,
                {1.0f, 0.56f, 0.16f, 0.82f},
                {-bladeClashDirection_.x, 0.0f, -bladeClashDirection_.z},
                0.48f);
            EmitParticleBurst(sparkParticles_, 
                sweepCenter, 46, 0.42f, AppParticleBurstStyle::Sparks,
                {1.0f, 0.46f, 0.12f, 0.82f},
                {-bladeClashDirection_.x, 0.08f, -bladeClashDirection_.z},
                1.48f);
            EmitParticleBurst(smokeParticles_, 
                sweepCenter, 18, 0.44f, AppParticleBurstStyle::Smoke,
                {0.42f, 0.31f, 0.24f, 0.44f},
                {-bladeClashDirection_.x, 0.04f, -bladeClashDirection_.z},
                0.62f);
            playerHitCooldown_ = 0.82f;
        }
        if (!bladeClashFinishPlayerWon_ && bladeClashFinishSkidEmitted_ &&
            bladeClashFinishTimer_ >= 1.24f && bladeClashFinishTimer_ < 1.28f) {
            XMFLOAT3 skidCenter = player_.GetTransform().position;
            skidCenter.y += 0.28f;
            EmitParticleBurst(explosionParticles_, 
                skidCenter, 36, 0.56f,
                AppParticleBurstStyle::SlashLine,
                {1.0f, 0.42f, 0.10f, 0.38f},
                {bladeClashDirection_.z, 0.02f, -bladeClashDirection_.x},
                0.92f);
            EmitParticleBurst(smokeParticles_, 
                skidCenter, 38, 0.64f, AppParticleBurstStyle::Smoke,
                {0.34f, 0.28f, 0.24f, 0.44f},
                {-bladeClashDirection_.x, 0.03f, -bladeClashDirection_.z},
                0.62f);
        }
        if (!bladeClashFinishPlayerWon_ && bladeClashFinishSkidEmitted_ &&
            !bladeClashFinishWallImpactEmitted_ &&
            bladeClashFinishTimer_ >= 1.58f) {
            bladeClashFinishWallImpactEmitted_ = true;
            XMFLOAT3 crashCenter = player_.GetTransform().position;
            crashCenter.y += 0.86f;
            EmitParticleBurst(explosionParticles_, 
                crashCenter, 110, 0.96f,
                AppParticleBurstStyle::Explosion,
                {1.0f, 0.36f, 0.10f, 0.78f},
                {-bladeClashDirection_.x, 0.12f, -bladeClashDirection_.z},
                2.35f);
            EmitParticleBurst(sparkParticles_, 
                crashCenter, 72, 0.58f, AppParticleBurstStyle::Sparks,
                {1.0f, 0.56f, 0.16f, 0.86f},
                {bladeClashDirection_.z, 0.10f, -bladeClashDirection_.x},
                2.10f);
            EmitParticleBurst(swordFlashParticles_, 
                crashCenter, 12, 0.82f, AppParticleBurstStyle::Flash,
                {1.0f, 0.82f, 0.48f, 0.58f},
                {-bladeClashDirection_.x, 0.0f, -bladeClashDirection_.z},
                0.40f);
            XMFLOAT3 dustCenter = crashCenter;
            dustCenter.y -= 0.52f;
            EmitParticleBurst(smokeParticles_, 
                dustCenter, 50, 0.94f, AppParticleBurstStyle::Smoke,
                {0.38f, 0.32f, 0.28f, 0.58f},
                {-bladeClashDirection_.x, 0.05f, -bladeClashDirection_.z},
                1.25f);
            CombatFeedbackEvent crashFeedback{};
            crashFeedback.type = CombatFeedbackEventType::PlayerDamaged;
            crashFeedback.position = crashCenter;
            crashFeedback.direction =
                {-bladeClashDirection_.x, 0.0f, -bladeClashDirection_.z};
            crashFeedback.power = 16.0f;
            DispatchCombatFeedback(crashFeedback);
        }
        if (bladeClashFinishPlayerWon_ && bladeClashFinal_) {
            const float leanT =
                std::clamp(bladeClashWinActionTimer / 0.36f, 0.0f, 1.0f);
            const float launchT =
                std::clamp((bladeClashWinActionTimer - 0.36f) / 0.82f, 0.0f,
                           1.0f);
            const float bounceT =
                std::clamp((bladeClashWinActionTimer - 1.10f) / 0.54f, 0.0f,
                           1.0f);
            const float slideT =
                std::clamp((bladeClashWinActionTimer - 1.64f) / 0.66f, 0.0f,
                           1.0f);
            const float settleT =
                std::clamp((bladeClashWinActionTimer - 2.30f) / 0.42f, 0.0f,
                           1.0f);
            const float leanEase = leanT * leanT * (3.0f - 2.0f * leanT);
            const float launchEase = 1.0f - std::pow(1.0f - launchT, 4.0f);
            const float bounceEase = 1.0f - std::pow(1.0f - bounceT, 3.0f);
            const float slideEase =
                1.0f - std::pow(1.0f - slideT, 3.0f);
            const float settleEase =
                settleT * settleT * (3.0f - 2.0f * settleT);
            const float travel =
                0.42f * leanEase + 10.40f * launchEase +
                4.70f * bounceEase + 5.25f * slideEase +
                0.43f * settleEase;
            const float firstArc = std::sinf(launchT * kPi) * 1.42f;
            const float secondArc = std::sinf(bounceT * kPi) * 0.72f;
            XMFLOAT3 enemyPos = {
                bladeClashFinishEnemyStart_.x + bladeClashDirection_.x * travel,
                bladeClashFinishEnemyStart_.y + firstArc + secondArc -
                    0.16f * slideEase - 0.24f * settleEase,
                bladeClashFinishEnemyStart_.z + bladeClashDirection_.z * travel};
            const float enemyYaw =
                std::atan2f(-bladeClashDirection_.x, -bladeClashDirection_.z) +
                std::sinf(bounceT * kPi) * 0.18f * (1.0f - slideEase);
            enemy_.SetCinematicTransform(enemyPos, enemyYaw);

            if (slideT > 0.02f && slideT < 0.96f) {
                XMFLOAT3 trailCenter = enemyPos;
                trailCenter.y += 0.18f;
                EmitParticleBurst(smokeParticles_, 
                    trailCenter, 4, 0.34f, AppParticleBurstStyle::Smoke,
                    {0.34f, 0.29f, 0.24f, 0.30f},
                    {-bladeClashDirection_.x, 0.04f, -bladeClashDirection_.z},
                    0.48f);
                EmitParticleBurst(sparkParticles_, 
                    trailCenter, 3, 0.16f, AppParticleBurstStyle::Sparks,
                    {1.0f, 0.66f, 0.18f, 0.36f},
                    {bladeClashDirection_.z, 0.02f, -bladeClashDirection_.x},
                    0.54f);
            }
        }
        if (bladeClashFinishPlayerWon_ && !bladeClashFinal_ &&
            !bladeClashFinishImpactEmitted_ &&
            bladeClashWinActionTimer >= 0.28f) {
            bladeClashFinishImpactEmitted_ = true;
            XMFLOAT3 cutCenter = enemy_.GetTransform().position;
            cutCenter.y += 1.18f;
            EmitParticleBurst(explosionParticles_, 
                cutCenter, 104, 0.86f,
                AppParticleBurstStyle::SlashLine,
                {1.0f, 0.90f, 0.58f, 0.66f},
                {bladeClashDirection_.z, 0.12f, -bladeClashDirection_.x},
                1.72f);
            EmitParticleBurst(explosionParticles_, 
                cutCenter, 42, 0.68f,
                AppParticleBurstStyle::Explosion,
                {1.0f, 0.78f, 0.34f, 0.50f},
                {bladeClashDirection_.x, 0.16f, bladeClashDirection_.z},
                1.18f);
            EmitParticleBurst(swordFlashParticles_, 
                cutCenter, 8, 0.42f, AppParticleBurstStyle::Flash,
                {1.0f, 0.96f, 0.80f, 0.56f},
                {bladeClashDirection_.z, 0.0f, -bladeClashDirection_.x},
                0.26f);
            EmitParticleBurst(sparkParticles_, 
                cutCenter, 46, 0.26f, AppParticleBurstStyle::Sparks,
                {1.0f, 0.82f, 0.28f, 0.58f},
                {bladeClashDirection_.z, 0.16f, -bladeClashDirection_.x},
                1.08f);
            EmitParticleBurst(smokeParticles_, 
                cutCenter, 18, 0.52f, AppParticleBurstStyle::Smoke,
                {0.38f, 0.32f, 0.25f, 0.34f},
                {-bladeClashDirection_.x, 0.10f, -bladeClashDirection_.z},
                0.62f);
            CombatFeedbackEvent slashFeedback{};
            slashFeedback.type = CombatFeedbackEventType::BladeClashPierce;
            slashFeedback.position = cutCenter;
            slashFeedback.direction =
                {bladeClashDirection_.z, 0.0f, -bladeClashDirection_.x};
            slashFeedback.power = 9.5f;
            DispatchCombatFeedback(slashFeedback);
        }
        if (bladeClashFinishPlayerWon_ && bladeClashFinal_ &&
            bladeClashFinalBarrageStep_ == 0 &&
            bladeClashWinActionTimer >= 0.18f) {
            bladeClashFinalBarrageStep_ = 1;
            XMFLOAT3 pressureCenter = bladeClashCenter_;
            pressureCenter.y += 0.16f;
            EmitParticleBurst(swordFlashParticles_, 
                pressureCenter, 7, 0.38f, AppParticleBurstStyle::Flash,
                {1.0f, 1.0f, 0.88f, 0.46f}, bladeClashDirection_, 0.24f);
            EmitParticleBurst(explosionParticles_, 
                pressureCenter, 70, 0.72f,
                AppParticleBurstStyle::SpiritSparkle,
                {1.0f, 0.96f, 0.78f, 0.54f}, {0.0f, 1.0f, 0.0f}, 0.88f);
            CombatFeedbackEvent pressureFeedback{};
            pressureFeedback.type = CombatFeedbackEventType::CounterSuccess;
            pressureFeedback.position = pressureCenter;
            pressureFeedback.direction = bladeClashDirection_;
            pressureFeedback.power = 8.5f;
            DispatchCombatFeedback(pressureFeedback);
        }
        if (bladeClashFinishPlayerWon_ && bladeClashFinal_ &&
            !bladeClashFinishImpactEmitted_ &&
            bladeClashWinActionTimer >= 0.36f) {
            bladeClashFinishImpactEmitted_ = true;
            XMFLOAT3 blastCenter = enemy_.GetTransform().position;
            blastCenter.y += 1.10f;
            EmitParticleBurst(explosionParticles_, 
                blastCenter, 270, 1.26f,
                AppParticleBurstStyle::Explosion,
                {1.0f, 0.96f, 0.70f, 0.88f}, bladeClashDirection_, 2.65f);
            EmitParticleBurst(sparkParticles_, 
                blastCenter, 170, 0.76f, AppParticleBurstStyle::Sparks,
                {1.0f, 0.66f, 0.14f, 0.86f}, bladeClashDirection_, 2.42f);
            EmitParticleBurst(swordFlashParticles_, 
                blastCenter, 14, 0.68f, AppParticleBurstStyle::Flash,
                {1.0f, 1.0f, 0.86f, 0.72f}, bladeClashDirection_, 0.48f);
            CombatFeedbackEvent blowbackFeedback{};
            blowbackFeedback.type = CombatFeedbackEventType::CounterSuccess;
            blowbackFeedback.position = blastCenter;
            blowbackFeedback.direction = bladeClashDirection_;
            blowbackFeedback.power = 22.0f;
            DispatchCombatFeedback(blowbackFeedback);
        }
        if (bladeClashFinishPlayerWon_ && bladeClashFinal_ &&
            !bladeClashFinishSkidEmitted_ &&
            bladeClashWinActionTimer >= 1.10f) {
            bladeClashFinishSkidEmitted_ = true;
            XMFLOAT3 skidCenter = enemy_.GetTransform().position;
            skidCenter.y += 0.22f;
            EmitParticleBurst(explosionParticles_, 
                skidCenter, 112, 0.82f,
                AppParticleBurstStyle::Explosion,
                {1.0f, 0.58f, 0.14f, 0.66f},
                {bladeClashDirection_.x, 0.22f, bladeClashDirection_.z},
                1.62f);
            EmitParticleBurst(sparkParticles_, 
                skidCenter, 88, 0.54f, AppParticleBurstStyle::Sparks,
                {1.0f, 0.72f, 0.18f, 0.84f},
                {bladeClashDirection_.z, 0.10f, -bladeClashDirection_.x},
                1.42f);
            EmitParticleBurst(smokeParticles_, 
                skidCenter, 86, 1.02f, AppParticleBurstStyle::Smoke,
                {0.42f, 0.34f, 0.26f, 0.58f}, bladeClashDirection_, 1.26f);
        }
        if (bladeClashFinishPlayerWon_ && bladeClashFinal_ &&
            !bladeClashFinishWallImpactEmitted_ &&
            bladeClashWinActionTimer >= 2.28f) {
            bladeClashFinishWallImpactEmitted_ = true;
            XMFLOAT3 crashCenter = enemy_.GetTransform().position;
            crashCenter.y += 0.42f;
            EmitParticleBurst(explosionParticles_, 
                crashCenter, 240, 1.36f,
                AppParticleBurstStyle::Explosion,
                {1.0f, 0.48f, 0.10f, 0.78f}, bladeClashDirection_, 2.70f);
            EmitParticleBurst(sparkParticles_, 
                crashCenter, 150, 0.86f, AppParticleBurstStyle::Sparks,
                {1.0f, 0.76f, 0.22f, 0.86f},
                {-bladeClashDirection_.x, 0.12f, -bladeClashDirection_.z},
                2.10f);
            EmitParticleBurst(smokeParticles_, 
                crashCenter, 124, 1.36f, AppParticleBurstStyle::Smoke,
                {0.36f, 0.30f, 0.26f, 0.62f}, bladeClashDirection_, 1.56f);
        }
        if (bladeClashFinishPlayerWon_ && !bladeClashFinal_ &&
            !bladeClashFinishSkidEmitted_ &&
            bladeClashWinActionTimer >= 0.76f) {
            bladeClashFinishSkidEmitted_ = true;
            XMFLOAT3 skidCenter = {
                enemy_.GetTransform().position.x + bladeClashDirection_.x * 6.8f,
                player_.GetTransform().position.y + 0.54f,
                enemy_.GetTransform().position.z + bladeClashDirection_.z * 6.8f};
            EmitParticleBurst(explosionParticles_, 
                skidCenter, 78, 0.92f,
                AppParticleBurstStyle::SlashLine,
                {1.0f, 0.78f, 0.32f, 0.58f},
                {bladeClashDirection_.z, 0.02f, -bladeClashDirection_.x},
                1.46f);
            EmitParticleBurst(sparkParticles_, 
                skidCenter, 48, 0.46f, AppParticleBurstStyle::Sparks,
                {1.0f, 0.70f, 0.20f, 0.70f},
                {-bladeClashDirection_.x, 0.04f, -bladeClashDirection_.z},
                1.08f);
            EmitParticleBurst(smokeParticles_, 
                skidCenter, 52, 0.94f, AppParticleBurstStyle::Smoke,
                {0.36f, 0.30f, 0.25f, 0.52f},
                {bladeClashDirection_.x, 0.08f, bladeClashDirection_.z},
                1.08f);
        }
        if (bladeClashFinishTimer_ >= bladeClashFinishDuration_) {
            const bool shouldResolveEnemyTransition =
                bladeClashFinishPendingEnemyTransition_;
            const bool wasFinalBladeClash = bladeClashFinal_;
            bladeClashFinishActive_ = false;
            bladeClashFinal_ = false;
            bladeClashFinishPendingEnemyTransition_ = false;
            player_.SetBladeClashPose(false);
            player_.SetDefeatPoseRatio(0.0f);
            if (shouldResolveEnemyTransition) {
                enemy_.ResolveDeferredDamageTransitions();
                enemyHitCooldown_ = 0.22f;
            }
            if (!shouldResolveEnemyTransition && wasFinalBladeClash) {
                enemyHitCooldown_ = 0.35f;
            }
        }
    }
    const float gameplayDeltaTime = baseDeltaTime * ComputeGameplayTimeScale();
    const float playerDeltaTime = gameplayDeltaTime;
    float enemyDeltaTime =
        bladeClashActive_
            ? 0.0f
            : counterCinematicActive_
            ? (baseDeltaTime *
               (std::min)(counterTimeScale_, ComputeGameplayTimeScale()))
            : gameplayDeltaTime;
    if (enemy_.GetBossPhase() == BossPhase::Phase3 &&
        !enemy_.IsPhaseTransitionActive() && !bladeClashActive_ &&
        !counterCinematicActive_) {
        enemyDeltaTime *= 1.08f;
    }
    UpdateCamera(input);

    ctx_->rendering.model->UpdateAnimation(playerModelId_, playerDeltaTime);

    bool forceRangedReflectMove = false;
    for (const auto &wave : enemy_.GetWaves()) {
        if (wave.isAlive && !wave.isReflected) {
            forceRangedReflectMove = true;
            break;
        }
    }
    if (bladeClashFinishActive_ && bladeClashFinishPlayerWon_) {
        const float bladeClashWinActionTimer =
            (std::max)(0.0f, bladeClashFinishTimer_ -
                                  kBladeClashWinGuardBreakLead) /
            kBladeClashWinActionSlow;
        if (bladeClashWinActionTimer <= 0.0f) {
            const float guardT = std::clamp(
                bladeClashFinishTimer_ / kBladeClashWinGuardBreakLead, 0.0f,
                1.0f);
            const float brace = guardT < 0.10f ? guardT / 0.10f : 1.0f;
            const float strain =
                std::sinf(std::clamp(guardT / 0.42f, 0.0f,
                                     1.0f) *
                          kPi);
            const float collapseT = std::clamp(
                (bladeClashFinishTimer_ - kBladeClashWinGuardBreakImpactTime) /
                    (kBladeClashWinGuardBreakLead -
                     kBladeClashWinGuardBreakImpactTime),
                0.0f, 1.0f);
            const float collapseEase =
                1.0f - std::pow(1.0f - collapseT, 4.0f);
            const float snap =
                std::sinf(std::clamp(
                              (bladeClashFinishTimer_ -
                               kBladeClashWinGuardBreakImpactTime) /
                                  0.08f,
                              0.0f, 1.0f) *
                          kPi);
            const float finishYaw =
                std::atan2f(bladeClashDirection_.x, bladeClashDirection_.z);
            XMFLOAT3 bracePos = bladeClashFinishPlayerStart_;
            bracePos.x += bladeClashDirection_.x *
                          (0.07f * brace + 0.13f * strain);
            bracePos.z += bladeClashDirection_.z *
                          (0.07f * brace + 0.13f * strain);
            player_.SetCinematicBladeClashPose(
                bracePos, finishYaw,
                std::clamp(0.38f + 0.14f * strain + 0.10f * snap,
                           0.0f, 1.0f));
            player_.SetDefeatPoseRatio(0.0f);

            XMFLOAT3 enemyPos = {
                bladeClashFinishEnemyStart_.x +
                    bladeClashDirection_.x *
                        (0.13f * snap +
                         kBladeClashGuardBreakRecoilDistance * collapseEase),
                bladeClashFinishEnemyStart_.y + 0.09f * snap +
                    (kBladeClashGuardBreakLift -
                     kBladeClashGuardBreakDrop * 0.25f) *
                        collapseEase,
                bladeClashFinishEnemyStart_.z +
                    bladeClashDirection_.z *
                        (0.13f * snap +
                         kBladeClashGuardBreakRecoilDistance * collapseEase)};
            const float enemyYaw =
                std::atan2f(-bladeClashDirection_.x, -bladeClashDirection_.z);
            const float side = bladeClashDirection_.x >= 0.0f ? 1.0f : -1.0f;
            enemy_.SetCinematicTransform(enemyPos, enemyYaw,
                                         -kBladeClashGuardBreakPose *
                                             collapseEase,
                                         side *
                                             (0.085f * snap +
                                              0.090f * collapseEase));
        } else if (bladeClashFinal_) {
            const float braceT =
                std::clamp(bladeClashWinActionTimer / 0.36f, 0.0f, 1.0f);
            const float settleT =
                std::clamp((bladeClashWinActionTimer - 1.10f) / 0.70f, 0.0f,
                           1.0f);
            const float braceEase = 1.0f - std::pow(1.0f - braceT, 3.0f);
            const float settleEase = settleT * settleT * (3.0f - 2.0f * settleT);
            XMFLOAT3 finishPos = {
                bladeClashFinishPlayerStart_.x +
                    bladeClashDirection_.x * (0.74f * braceEase -
                                              0.34f * settleEase),
                bladeClashFinishPlayerStart_.y,
                bladeClashFinishPlayerStart_.z +
                    bladeClashDirection_.z * (0.74f * braceEase -
                                              0.34f * settleEase)};
            const float finishYaw =
                std::atan2f(bladeClashDirection_.x, bladeClashDirection_.z);
            player_.SetCinematicBladeClashPose(
                finishPos, finishYaw,
                std::clamp(0.84f - 0.20f * settleEase, 0.0f, 1.0f));
            player_.SetDefeatPoseRatio(0.0f);
        } else {
            const float windupT =
                std::clamp(bladeClashWinActionTimer / 0.12f, 0.0f, 1.0f);
            const float cutT = std::clamp(
                (bladeClashWinActionTimer - 0.03f) / 0.18f, 0.0f, 1.0f);
            const float slideT = std::clamp(
                (bladeClashWinActionTimer - 0.11f) / 0.44f, 0.0f, 1.0f);
            const float settleT = std::clamp(
                (bladeClashWinActionTimer - 0.50f) / 0.34f, 0.0f, 1.0f);
            const float windupEase =
                windupT * windupT * (3.0f - 2.0f * windupT);
            const float cutEase = 1.0f - std::pow(1.0f - cutT, 4.0f);
            const float slideEase = 1.0f - std::pow(1.0f - slideT, 2.0f);
            const float settleEase =
                settleT * settleT * (3.0f - 2.0f * settleT);
            const float dashEase =
                std::clamp(0.86f * cutEase + 0.24f * slideEase, 0.0f, 1.0f);
            XMFLOAT3 dashPos = Lerp(bladeClashFinishPlayerStart_,
                                    bladeClashFinishPlayerEnd_, dashEase);
            const float anticipation =
                std::sinf(windupEase * kPi) * (1.0f - cutEase);
            const float lift =
                std::sinf(cutT * kPi) * 0.10f * (1.0f - settleEase);
            dashPos.x -= bladeClashDirection_.x * 0.42f * anticipation;
            dashPos.y += lift;
            dashPos.z -= bladeClashDirection_.z * 0.42f * anticipation;
            player_.LockPosition(dashPos);

            const float slashPose =
                std::sinf(std::clamp((bladeClashWinActionTimer - 0.06f) / 0.38f,
                                     0.0f, 1.0f) *
                          kPi);
            const float finishYaw =
                std::atan2f(bladeClashDirection_.x, bladeClashDirection_.z);
            player_.SetCinematicBladeClashPose(
                dashPos, finishYaw,
                std::clamp(0.58f + 0.42f * slashPose - 0.16f * settleEase,
                           0.0f, 1.0f));

            const float enemyHitT = std::clamp(
                (bladeClashWinActionTimer - 0.08f) / 0.14f, 0.0f, 1.0f);
            const float enemyBreakT = std::clamp(
                (bladeClashWinActionTimer - 0.18f) / 0.38f, 0.0f, 1.0f);
            const float enemySlamT = std::clamp(
                (bladeClashWinActionTimer - 0.48f) / 0.38f, 0.0f, 1.0f);
            const float enemySlideT = std::clamp(
                (bladeClashWinActionTimer - 0.78f) / 0.52f, 0.0f, 1.0f);
            const float enemyHitEase =
                1.0f - std::pow(1.0f - enemyHitT, 5.0f);
            const float enemyBreakEase =
                enemyBreakT * enemyBreakT * (3.0f - 2.0f * enemyBreakT);
            const float enemySlamEase =
                1.0f - std::pow(1.0f - enemySlamT, 4.0f);
            const float enemySlideEase =
                1.0f - std::pow(1.0f - enemySlideT, 2.0f);
            const float hitPop = std::sinf(enemyHitT * kPi);
            const float breakArc = std::sinf(enemyBreakT * kPi);
            const float slamArc = std::sinf(enemySlamT * kPi);
            const float recoil =
                kBladeClashGuardBreakRecoilDistance +
                1.12f * enemyHitEase + 3.45f * enemyBreakEase +
                5.90f * enemySlamEase + 1.95f * enemySlideEase;
            const float side =
                bladeClashDirection_.x >= 0.0f ? 1.0f : -1.0f;
            const XMFLOAT3 right = {
                bladeClashDirection_.z, 0.0f, -bladeClashDirection_.x};
            const float sideDrift =
                side * (0.42f * hitPop + 0.92f * enemySlamEase -
                        0.18f * enemySlideEase);
            XMFLOAT3 enemyPos = {
                bladeClashFinishEnemyStart_.x + bladeClashDirection_.x * recoil +
                    right.x * sideDrift,
                bladeClashFinishEnemyStart_.y +
                    kBladeClashGuardBreakLift * (1.0f - 0.22f * enemySlamEase) -
                    kBladeClashGuardBreakDrop + hitPop * 0.50f +
                    breakArc * 1.12f + slamArc * 0.58f -
                    0.82f * enemySlideEase,
                bladeClashFinishEnemyStart_.z + bladeClashDirection_.z * recoil +
                    right.z * sideDrift};
            const float enemyYaw =
                std::atan2f(-bladeClashDirection_.x, -bladeClashDirection_.z);
            const float twistYaw =
                enemyYaw +
                side * (0.46f * hitPop + 0.98f * enemySlamEase -
                        0.22f * enemySlideEase);
            const float pitch =
                -0.18f * kBladeClashGuardBreakPose - 0.28f * hitPop -
                0.90f * enemySlamEase + 0.18f * enemySlideEase;
            const float roll =
                side * (0.36f * hitPop + 0.92f * enemySlamEase -
                        0.20f * enemySlideEase);
            enemy_.SetCinematicTransform(enemyPos, twistYaw, pitch, roll);

            if (enemySlamT > 0.02f && enemySlideT < 0.82f) {
                XMFLOAT3 burstCenter = enemyPos;
                burstCenter.y += 0.26f;
                EmitParticleBurst(smokeParticles_, 
                    burstCenter, 5, 0.30f, AppParticleBurstStyle::Smoke,
                    {0.34f, 0.29f, 0.24f, 0.24f},
                    {-bladeClashDirection_.x, 0.06f, -bladeClashDirection_.z},
                    0.52f);
                EmitParticleBurst(sparkParticles_, 
                    burstCenter, 4, 0.14f, AppParticleBurstStyle::Sparks,
                    {1.0f, 0.66f, 0.18f, 0.32f},
                    {right.x * side, 0.08f, right.z * side}, 0.64f);
            }
        }
    } else if (bladeClashFinishActive_) {
        const float leanT =
            std::clamp(bladeClashFinishTimer_ / 0.78f, 0.0f, 1.0f);
        const float recoilT =
            std::clamp((bladeClashFinishTimer_ - 0.78f) / 0.36f, 0.0f, 1.0f);
        const float slideT =
            std::clamp((bladeClashFinishTimer_ - 1.12f) / 0.52f, 0.0f, 1.0f);
        const float leanEase = leanT * leanT * (3.0f - 2.0f * leanT);
        const float recoilEase = 1.0f - std::pow(1.0f - recoilT, 3.0f);
        const float slideEase =
            slideT < 0.18f
                ? 0.06f *
                      std::pow(std::clamp(slideT / 0.18f, 0.0f, 1.0f), 2.0f)
                : slideT < 0.74f
                      ? 0.06f +
                            0.88f *
                                (1.0f -
                                 std::pow(1.0f -
                                              std::clamp((slideT - 0.18f) /
                                                             0.56f,
                                                         0.0f, 1.0f),
                                          5.0f))
                      : 0.94f +
                            0.06f *
                                (std::clamp((slideT - 0.74f) / 0.26f, 0.0f,
                                            1.0f) *
                                 std::clamp((slideT - 0.74f) / 0.26f, 0.0f,
                                            1.0f) *
                                 (3.0f -
                                  2.0f *
                                      std::clamp((slideT - 0.74f) / 0.26f,
                                                 0.0f, 1.0f)));
        const float retreat =
            0.24f * leanEase + 0.56f * recoilEase +
            (15.80f - 0.80f) * slideEase;
        XMFLOAT3 lossPos = {
            bladeClashFinishPlayerStart_.x - bladeClashDirection_.x * retreat,
            bladeClashFinishPlayerStart_.y,
            bladeClashFinishPlayerStart_.z - bladeClashDirection_.z * retreat};
        lossPos.y += std::sin(recoilT * kPi) * 0.16f;
        lossPos.y += std::sin(slideT * kPi) * 0.92f;
        player_.LockPosition(lossPos);
        if (bladeClashFinishSkidEmitted_) {
            const float faceEnemyYaw =
                std::atan2f(bladeClashDirection_.x, bladeClashDirection_.z);
            const float awayYaw =
                std::atan2f(-bladeClashDirection_.x, -bladeClashDirection_.z);
            player_.SetYaw(slideT > 0.001f ? awayYaw : faceEnemyYaw);
            player_.SetBladeClashPose(false);
            player_.SetDefeatPoseRatio(
                std::clamp(0.20f + 0.58f * recoilEase + 0.32f * slideEase,
                           0.0f, 1.0f));
        } else {
            player_.SetDefeatPoseRatio(0.0f);
            player_.SetBladeClashPose(true, 0.0f);
        }
    } else {
        const XMFLOAT3 playerPosBeforeMove = player_.GetTransform().position;
        const bool lockPlayerAtEnemyFrontBeforeMove =
            ShouldLockPlayerAtEnemyAttackFront(playerPosBeforeMove);
        player_.SetCameraSwordSlashSuppressed(false);
        player_.Update(input, playerDeltaTime, enemy_.GetTransform().position,
                       cameraYaw_, forceRangedReflectMove, baseDeltaTime,
                       false);
        if (lockPlayerAtEnemyFrontBeforeMove) {
            player_.LockPosition(playerPosBeforeMove);
        } else if (ShouldLockPlayerAtEnemyAttackFront(
                       player_.GetTransform().position)) {
            player_.LockPosition(player_.GetTransform().position);
        }
    }
    UpdateSwordVfx(baseDeltaTime);
    if (soundsLoaded_ && ctx_->systems.sound != nullptr) {
        const auto slashStates = player_.GetSwordSlashStates();
        for (size_t i = 0; i < slashStates.size(); ++i) {
            if (slashStates[i] && !previousSwordSoundStates_[i]) {
                ctx_->systems.sound->Play(slashSoundId_);
            }
        }
        previousSwordSoundStates_ = slashStates;
    }
    hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(), enemy_.GetMaxHP());
    sceneLightTime_ += baseDeltaTime;
    battleElapsedTime_ += baseDeltaTime;

    if (!bladeClashFinishActive_) {
        const ActionKind previousEnemyActionKind = enemy_.GetActionKind();
        const ActionStep previousEnemyActionStep = enemy_.GetActionStep();
        enemy_.Update(BuildPlayerCombatObservation(), enemyDeltaTime);
        const ActionKind currentEnemyActionKind = enemy_.GetActionKind();
        const ActionStep currentEnemyActionStep = enemy_.GetActionStep();
        if (currentEnemyActionKind != previousEnemyActionKind ||
            currentEnemyActionStep != previousEnemyActionStep) {
            EmitEnemyActionParticles(currentEnemyActionKind,
                                     currentEnemyActionStep);
            const bool isEnemyAttackRelease =
                (currentEnemyActionKind == ActionKind::Smash ||
                 currentEnemyActionKind == ActionKind::Sweep ||
                 currentEnemyActionKind == ActionKind::Wave) &&
                currentEnemyActionStep == ActionStep::Active;
            if (isEnemyAttackRelease) {
                if (soundsLoaded_ && ctx_->systems.sound != nullptr) {
                    ctx_->systems.sound->Play(enemyReleaseSoundId_);
                }
            }
        }
    }
    UpdateSceneLighting();

    SyncEnemyAnimation();
    const bool bladeClashFinishWinAnim =
        bladeClashFinishActive_ && bladeClashFinishPlayerWon_;
    SetEnemyAnimationFrozen(
        counterCinematicActive_ ||
        (bladeClashFinishActive_ && bladeClashFinishPlayerWon_ &&
         !bladeClashFinishWinAnim));
    if (!enemyAnimationFrozen_) {
        float enemyAnimationDeltaTime = enemyDeltaTime;
        const ActionKind enemyActionKind = enemy_.GetActionKind();
        const ActionStep enemyActionStep = enemy_.GetActionStep();
        const bool bladeClashStartupAnim =
            enemyActionKind == ActionKind::BladeClash &&
            enemyActionStep == ActionStep::Charge;
        if (bladeClashActive_ || bladeClashFinishWinAnim ||
            bladeClashStartupAnim) {
            UpdateBladeClashEnemyAnimation(baseDeltaTime);
            if (bladeClashStartupAnim) {
                enemyAnimationDeltaTime = 0.0f;
            }
        } else if (enemyActionKind == ActionKind::Smash ||
            enemyActionKind == ActionKind::Sweep ||
            enemyActionKind == ActionKind::Wave) {
            if (IsChargeStanceSettled(enemyActionKind, enemyActionStep,
                                      enemy_.GetActionTimerForPresentation())) {
                enemyAnimationDeltaTime *= 0.035f;
            } else if (enemyActionStep == ActionStep::Active) {
                enemyAnimationDeltaTime *= 4.6f;
            } else if (enemyActionStep == ActionStep::Recovery) {
                enemyAnimationDeltaTime *= 0.72f;
            }
        }
        if (!bladeClashActive_) {
            ctx_->rendering.model->UpdateAnimation(enemyModelId_, enemyAnimationDeltaTime);
        }
    }
    ApplyEnemyProceduralAnimation();

    UpdateBattleCamera();
    camera_.UpdateMatrices();

    player_.SetCameraSwordSlashSuppressed(false);
    UpdateCombat(gameplayDeltaTime);
    if (enemy_.IsPhaseTransitionActive() && !phaseTransitionWasActive_) {
        phaseTransitionWasActive_ = true;
        phaseTransitionReleaseEmitted_ = false;
        phaseTransitionLoopTimer_ = 0.0f;
        EmitPhaseTransitionStartEffects();
    }
    if (!battleResultRequested_ && !bladeClashFinishActive_) {
        if (enemy_.GetHP() <= 0.0f) {
            BeginVictorySequence();
            return;
        }
        if (player_.GetHP() <= 0.0f) {
            BeginDefeatSequence();
            return;
        }
    }
    if (counterCinematicActive_) {
        counterCinematicTimer_ -= baseDeltaTime;
        if (counterCinematicTimer_ <= 0.0f) {
            counterCinematicTimer_ = 0.0f;
            counterCinematicActive_ = false;
            SetEnemyAnimationFrozen(false);
        }
    }
    sparkParticles_.Update(baseDeltaTime);
    explosionParticles_.Update(baseDeltaTime);
    smokeParticles_.Update(baseDeltaTime);
    swordFlashParticles_.Update(baseDeltaTime);
}

void GameScene::Draw() {
    const bool bladeClashWinFinish =
        bladeClashFinishActive_ && bladeClashFinishPlayerWon_;
    if (!bladeClashWinFinish) {
        ctx_->rendering.model->PrepareSkinning(
            {playerModelId_, enemyModelId_});
        GPUParticleSystem::DispatchPendingUpdates(
            {&smokeParticles_, &sparkParticles_, &explosionParticles_,
             &swordFlashParticles_});
    }

    ctx_->rendering.model->PreDraw();
    if (bladeClashWinFinish) {
        DrawBladeClashFinishBackdrop();
    } else {
        DrawArena();
    }
    const float playerVisualScale = bladeClashWinFinish ? 0.68f : 1.0f;
    const float enemyVisualScale = bladeClashWinFinish ? 1.32f : 1.0f;
    player_.Draw(ctx_->rendering.model, camera_,
                 !playerViewCamera_ || bladeClashFinishActive_,
                 bladeClashFinishActive_, playerVisualScale);
    if (!(victorySequenceActive_ && victoryFinalExplosionEmitted_)) {
        enemy_.Draw(ctx_->rendering.model, camera_, enemyVisualScale);
    }
    ctx_->rendering.model->PostDraw();
    swordTrailRenderer_.Draw(camera_);
    swordSlashArcRenderer_.Draw(camera_);

    if (!bladeClashWinFinish) {
        GPUParticleSystem::DrawBatch(
            {&smokeParticles_, &sparkParticles_, &explosionParticles_,
             &swordFlashParticles_},
            camera_);
    }
    DrawVictoryFlash();
    DrawDefeatFlash();
    DrawBattleIntroFlash();
}

void GameScene::DispatchCombatFeedback(const CombatFeedbackEvent &event) {
    combatFeedback_.PushEvent(event);
    EmitCombatParticles(event);
    if (event.type == CombatFeedbackEventType::PlayerSlashHit) {
        DirectX::XMFLOAT2 slashDirection{0.0f, 0.0f};
        const auto swords = player_.GetSwords();
        if (event.swordIndex < swords.size() && swords[event.swordIndex]) {
            slashDirection = swords[event.swordIndex]->GetSlashDirection();
        }
        swordSlashArcRenderer_.EmitHitLine(event.position, event.direction,
                                           camera_, event.power,
                                           slashDirection);
    }
    if (!soundsLoaded_ || ctx_ == nullptr || ctx_->systems.sound == nullptr) {
        return;
    }

    switch (event.type) {
    case CombatFeedbackEventType::CounterSuccess:
    case CombatFeedbackEventType::BladeClashGuardBreak:
        ctx_->systems.sound->Play(counterSoundId_);
        break;
    case CombatFeedbackEventType::PlayerSlashHit:
    case CombatFeedbackEventType::BladeClashPierce:
        ctx_->systems.sound->Play(hitSoundId_);
        break;
    case CombatFeedbackEventType::PlayerDamaged:
        ctx_->systems.sound->Play(damageSoundId_);
        break;
    case CombatFeedbackEventType::ProjectileReflect:
        ctx_->systems.sound->Play(counterSoundId_);
        break;
    case CombatFeedbackEventType::PlayerGuard:
    default:
        break;
    }
}

void GameScene::UpdateSwordVfx(float deltaTime) {
    swordTrailRenderer_.Update(player_, deltaTime);
    swordSlashArcRenderer_.Update(deltaTime);
}

void GameScene::EmitCombatParticles(const CombatFeedbackEvent &event) {
    XMFLOAT3 position = event.position;
    position.y += 0.08f;

    const XMFLOAT3 direction = event.direction;
    const float power = (std::max)(0.6f, event.power);

    switch (event.type) {
    case CombatFeedbackEventType::PlayerSlashHit:
        EmitParticleBurst(sparkParticles_, position,
                                  static_cast<uint32_t>(34.0f + power * 12.0f),
                                  0.050f, AppParticleBurstStyle::Sparks,
                                  {0.94f, 0.97f, 1.00f, 0.76f}, direction,
                                  2.35f + power * 0.38f);
        EmitParticleBurst(swordFlashParticles_, 
            position, static_cast<uint32_t>(14.0f + power * 4.0f),
            0.088f + power * 0.012f, AppParticleBurstStyle::Flash,
            {1.0f, 0.98f, 1.00f, 0.72f}, direction, 0.38f + power * 0.05f);
        EmitParticleBurst(explosionParticles_, 
            position, static_cast<uint32_t>(54.0f + power * 12.0f),
            0.18f + power * 0.016f, AppParticleBurstStyle::SlashLine,
            {0.94f, 0.90f, 1.00f, 0.82f}, direction, 2.18f + power * 0.30f);
        EmitParticleBurst(smokeParticles_, 
            position, static_cast<uint32_t>(6.0f + power * 2.0f),
            0.13f + power * 0.012f, AppParticleBurstStyle::Smoke,
            {0.96f, 0.97f, 1.00f, 0.16f}, direction, 1.26f + power * 0.12f);
        break;
    case CombatFeedbackEventType::BladeClashPierce:
        EmitParticleBurst(sparkParticles_, position,
                                  static_cast<uint32_t>(64.0f + power * 28.0f),
                                  0.13f, AppParticleBurstStyle::Sparks,
                                  {1.00f, 0.68f, 0.28f, 1.0f}, direction,
                                  1.55f + power * 0.38f);
        break;
    case CombatFeedbackEventType::PlayerGuard:
        EmitParticleBurst(sparkParticles_, position, 92, 0.20f,
                                  AppParticleBurstStyle::Sparks,
                                  {1.00f, 0.74f, 0.36f, 1.0f}, direction, 1.95f);
        EmitParticleBurst(explosionParticles_, position, 14, 0.16f,
                                      AppParticleBurstStyle::Explosion,
                                      {0.92f, 0.50f, 0.22f, 1.0f}, direction,
                                      0.72f);
        EmitParticleBurst(smokeParticles_, position, 10, 0.24f,
                                  AppParticleBurstStyle::Smoke,
                                  {0.42f, 0.36f, 0.31f, 1.0f}, direction, 0.48f);
        break;
    case CombatFeedbackEventType::PlayerDamaged:
        EmitParticleBurst(sparkParticles_, position, 110, 0.24f,
                                  AppParticleBurstStyle::Sparks,
                                  {1.00f, 0.52f, 0.20f, 1.0f}, direction, 2.15f);
        EmitParticleBurst(explosionParticles_, position, 42, 0.30f,
                                      AppParticleBurstStyle::Explosion,
                                      {1.00f, 0.30f, 0.10f, 1.0f}, direction,
                                      1.15f);
        EmitParticleBurst(smokeParticles_, position, 34, 0.40f,
                                  AppParticleBurstStyle::Smoke,
                                  {0.48f, 0.36f, 0.28f, 1.0f}, direction, 0.78f);
        break;
    case CombatFeedbackEventType::CounterSuccess:
    case CombatFeedbackEventType::BladeClashGuardBreak:
        EmitParticleBurst(sparkParticles_, position, 170, 0.34f,
                                  AppParticleBurstStyle::Sparks,
                                  {1.00f, 0.76f, 0.26f, 1.0f}, direction, 2.1f);
        EmitParticleBurst(explosionParticles_, position, 78, 0.46f,
                                      AppParticleBurstStyle::Explosion,
                                      {1.00f, 0.46f, 0.12f, 1.0f}, direction,
                                      1.48f);
        EmitParticleBurst(smokeParticles_, position, 54, 0.56f,
                                  AppParticleBurstStyle::Smoke,
                                  {0.36f, 0.31f, 0.27f, 1.0f}, direction, 0.92f);
        break;
    case CombatFeedbackEventType::ProjectileReflect:
        EmitParticleBurst(sparkParticles_, position, 118, 0.24f,
                                  AppParticleBurstStyle::Sparks,
                                  {0.96f, 0.86f, 0.64f, 1.0f}, direction, 2.15f);
        EmitParticleBurst(explosionParticles_, position, 46, 0.30f,
                                      AppParticleBurstStyle::Explosion,
                                      {0.68f, 0.78f, 0.72f, 1.0f}, direction,
                                      1.12f);
        EmitParticleBurst(smokeParticles_, position, 24, 0.36f,
                                  AppParticleBurstStyle::Smoke,
                                  {0.38f, 0.34f, 0.31f, 1.0f}, direction, 0.66f);
        break;
    default:
        break;
    }
}

void GameScene::EmitEnemyActionParticles(ActionKind kind, ActionStep step) {
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const float yaw = enemy_.GetTelegraphYaw();
    const XMFLOAT3 forward = {std::sinf(yaw), 0.20f, std::cosf(yaw)};

    XMFLOAT3 origin = enemyPos;
    origin.y += 1.05f;

    if (step == ActionStep::Charge || step == ActionStep::Hold) {
        return;
    } else if (step == ActionStep::Active) {
        origin.x += forward.x * 1.10f;
        origin.z += forward.z * 1.10f;
        switch (kind) {
        case ActionKind::Smash:
            EmitParticleBurst(explosionParticles_, 
                origin, 150, 2.10f,
                AppParticleBurstStyle::SpiritSparkle,
                {1.0f, 1.0f, 0.96f, 0.72f}, forward, 1.68f);
            break;
        case ActionKind::Sweep:
            EmitParticleBurst(explosionParticles_, 
                origin, 158, 2.18f,
                AppParticleBurstStyle::SpiritSparkle,
                {1.0f, 1.0f, 0.96f, 0.70f}, forward, 1.58f);
            break;
        case ActionKind::BladeClash:
            EmitParticleBurst(explosionParticles_, 
                origin, 56, 1.05f,
                AppParticleBurstStyle::SpiritSparkle,
                {0.94f, 1.0f, 0.96f, 0.50f}, forward, 0.92f);
            EmitParticleBurst(smokeParticles_, 
                origin, 2, 0.34f, AppParticleBurstStyle::Flash,
                {0.92f, 1.0f, 0.96f, 0.22f}, forward, 0.22f);
            break;
        case ActionKind::Wave:
            EmitParticleBurst(explosionParticles_, 
                origin, 130, 2.02f,
                AppParticleBurstStyle::SpiritSparkle,
                {0.96f, 1.0f, 0.96f, 0.68f}, forward, 1.48f);
            break;
        default:
            break;
        }
    }
}

bool GameScene::ShouldLockPlayerAtEnemyAttackFront(
    const XMFLOAT3 &playerPosition) const {
    const ActionKind kind = enemy_.GetActionKind();
    const ActionStep step = enemy_.GetActionStep();
    const bool isFrontLockAction =
        kind == ActionKind::Smash || kind == ActionKind::Sweep ||
        kind == ActionKind::BladeClash;
    const bool isFrontLockStep = step == ActionStep::Charge ||
                                 step == ActionStep::Hold ||
                                 step == ActionStep::Active;
    if (!isFrontLockAction || !isFrontLockStep) {
        return false;
    }

    const XMFLOAT3 enemyPosition = enemy_.GetTransform().position;
    const float toPlayerX = playerPosition.x - enemyPosition.x;
    const float toPlayerZ = playerPosition.z - enemyPosition.z;
    const float yaw = enemy_.GetTelegraphYaw();
    const float forwardX = std::sinf(yaw);
    const float forwardZ = std::cosf(yaw);
    const float rightX = forwardZ;
    const float rightZ = -forwardX;
    const float forwardDistance = toPlayerX * forwardX + toPlayerZ * forwardZ;
    const float lateralDistance = std::fabs(toPlayerX * rightX +
                                            toPlayerZ * rightZ);

    return forwardDistance >= kEnemyAttackFrontLockMinDistance &&
           forwardDistance <= kEnemyAttackFrontLockMaxDistance &&
           lateralDistance <= kEnemyAttackFrontLockHalfWidth;
}

void GameScene::UpdateBattlePostProcessState(float deltaTime) {
    (void)deltaTime;
    if (ctx_ == nullptr || ctx_->rendering.postProcessSystem == nullptr) {
        return;
    }

    if (bladeClashFinishActive_) {
        const float ratio =
            bladeClashFinishDuration_ > 0.0001f
                ? std::clamp(bladeClashFinishTimer_ / bladeClashFinishDuration_,
                             0.0f, 1.0f)
                : 1.0f;
        const float hold = 1.0f - std::clamp((ratio - 0.76f) / 0.24f,
                                             0.0f, 1.0f);
        float radialBlurStrength =
            bladeClashFinishPlayerWon_ ? 0.018f * hold : 0.020f * hold;
        float vignetteStrength = bladeClashFinishPlayerWon_ ? 0.22f : 0.46f;
        float vignetteScale = 11.0f;
        float vignettePower = 1.15f;
        float sceneDimStrength =
            bladeClashFinishPlayerWon_ ? 0.03f * hold : 0.16f * hold;
        if (bladeClashFinishPlayerWon_) {
            const float guardBreakInT = std::clamp(
                (bladeClashFinishTimer_ -
                 (kBladeClashWinGuardBreakImpactTime - 0.035f)) /
                    0.16f,
                0.0f, 1.0f);
            const float guardBreakHold = 1.0f - std::clamp(
                (bladeClashFinishTimer_ - kBladeClashWinGuardBreakImpactTime) /
                    0.36f,
                0.0f, 1.0f);
            const float guardBreakPulse =
                std::sinf(guardBreakInT * kPi) * 0.10f +
                guardBreakHold * 0.28f;
            radialBlurStrength =
                (std::max)(radialBlurStrength, 0.024f * guardBreakHold);
            vignetteStrength =
                std::clamp((std::max)(vignetteStrength,
                                      0.26f + guardBreakPulse),
                           0.0f, 0.68f);
            vignetteScale = 8.2f;
            vignettePower = 1.25f;
            sceneDimStrength =
                (std::max)(sceneDimStrength, 0.06f * guardBreakHold);

            const float pierceTimer =
                (std::max)(0.0f, bladeClashFinishTimer_ -
                                      kBladeClashWinGuardBreakLead) /
                kBladeClashWinActionSlow;
            const float pierceT =
                std::clamp((pierceTimer - 0.24f) / 0.18f, 0.0f, 1.0f);
            const float pierceHold =
                pierceTimer >= 0.24f
                    ? 1.0f - std::clamp((pierceTimer - 0.32f) / 0.30f, 0.0f,
                                        1.0f)
                    : 0.0f;
            const float piercePulse =
                std::sinf(pierceT * kPi) * 0.020f + pierceHold * 0.026f;
            radialBlurStrength =
                (std::max)(radialBlurStrength, piercePulse);
        }
        ApplyBattlePostProcess(ctx_, radialBlurStrength, vignetteStrength,
                               sceneDimStrength, 0.48f, 18, vignetteScale,
                               vignettePower);
        return;
    }

    PostProcessProfile profile = GetPostProcessProfile(ctx_);
    const bool hasFeedbackPost =
        profile.radialBlur.strength > 0.001f ||
        profile.randomNoise.strength > 0.001f;
    if (!hasFeedbackPost) {
        profile.vignette.enabled = false;
        profile.vignette.strength = 0.0f;
    }
    profile.vignette.scale = 11.0f;
    profile.vignette.power = 1.15f;
    profile.sceneDim.strength = 0.0f;
    SetPostProcessProfile(ctx_, profile);
}

void GameScene::DrawTransparent() {
    if (battleIntroActive_) {
        return;
    }
    hud_.Draw(*ctx_);
}

void GameScene::UpdatePhaseTransitionCinematic(float deltaTime) {
    if (!phaseTransitionWasActive_) {
        phaseTransitionWasActive_ = true;
        phaseTransitionReleaseEmitted_ = false;
        phaseTransitionLoopTimer_ = 0.0f;
        EmitPhaseTransitionStartEffects();
    }

    sceneLightTime_ += deltaTime;
    combatFeedback_.Update(deltaTime, sceneLightTime_);
    UpdateBattlePostProcessState(deltaTime);

    const float ratio = enemy_.GetPhaseTransitionRatio();
    constexpr float kReleaseStart = 0.88f;
    constexpr float kReleaseDuration = 0.05f;
    const float charge = SmoothStep01(ratio / kReleaseStart);
    const float release = SmoothStep01((ratio - kReleaseStart) / kReleaseDuration);
    const float hold = charge * (1.0f - release);
    ApplyBattlePostProcess(ctx_, 0.010f + 0.026f * hold + 0.036f * release,
                           0.30f + 0.42f * hold, 0.10f + 0.18f * hold);

    enemy_.Update(BuildPlayerCombatObservation(), deltaTime);
    ctx_->rendering.model->UpdateAnimation(playerModelId_, deltaTime * 0.025f);
    UpdatePhaseTransitionEnemyAnimation(deltaTime);
    ApplyEnemyProceduralAnimation();
    UpdateSceneLighting();
    hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(), enemy_.GetMaxHP());
    UpdateBattleCamera();
    camera_.UpdateMatrices();

    EmitPhaseTransitionLoopEffects(deltaTime);
    if (!phaseTransitionReleaseEmitted_ && ratio >= kReleaseStart) {
        phaseTransitionReleaseEmitted_ = true;
        EmitPhaseTransitionReleaseEffects();
    }

    sparkParticles_.Update(deltaTime);
    explosionParticles_.Update(deltaTime);
    smokeParticles_.Update(deltaTime);
    swordFlashParticles_.Update(deltaTime);

    if (!enemy_.IsPhaseTransitionActive()) {
        phaseTransitionWasActive_ = false;
        phaseTransitionReleaseEmitted_ = false;
        ClearBattlePostProcess(ctx_);
    }
}

void GameScene::EmitPhaseTransitionStartEffects() {
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 origin{enemyPos.x, enemyPos.y + 1.18f, enemyPos.z};
    EmitParticleBurst(smokeParticles_, origin, 54, 1.00f,
                              AppParticleBurstStyle::Flash,
                              {1.0f, 0.52f, 0.12f, 0.86f},
                              {0.0f, 1.0f, 0.0f}, 0.62f);
    EmitParticleBurst(sparkParticles_, origin, 150, 1.45f,
                              AppParticleBurstStyle::Sparks,
                              {1.0f, 0.72f, 0.24f, 0.96f},
                              {0.0f, 1.0f, 0.0f}, 3.2f);
    if (soundsLoaded_ && ctx_ != nullptr && ctx_->systems.sound != nullptr) {
        ctx_->systems.sound->Play(enemyReleaseSoundId_);
    }
}

void GameScene::EmitPhaseTransitionLoopEffects(float deltaTime) {
    phaseTransitionLoopTimer_ -= deltaTime;
    if (phaseTransitionLoopTimer_ > 0.0f) {
        return;
    }
    phaseTransitionLoopTimer_ = 0.105f;

    const float ratio = enemy_.GetPhaseTransitionRatio();
    constexpr float kReleaseStart = 0.88f;
    constexpr float kReleaseDuration = 0.05f;
    const float charge = SmoothStep01(ratio / kReleaseStart);
    const float release = SmoothStep01((ratio - kReleaseStart) / kReleaseDuration);
    const float hold = charge * (1.0f - release);
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 origin{enemyPos.x, enemyPos.y + 1.28f, enemyPos.z};
    const uint32_t sparkCount =
        static_cast<uint32_t>(28.0f + 44.0f * hold);
    EmitParticleBurst(sparkParticles_, origin, sparkCount, 0.58f + 0.46f * hold,
                              AppParticleBurstStyle::Sparks,
                              {1.0f, 0.28f, 0.04f, 0.86f},
                              {0.0f, 1.0f, 0.0f}, 2.4f + 2.0f * hold);
    if (hold > 0.35f) {
        EmitParticleBurst(smokeParticles_, origin, 3, 0.58f,
                                  AppParticleBurstStyle::Flash,
                                  {1.0f, 0.20f, 0.04f, 0.52f},
                                  {0.0f, 1.0f, 0.0f}, 0.38f);
    }
}

void GameScene::EmitPhaseTransitionReleaseEffects() {
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 origin{enemyPos.x, enemyPos.y + 1.30f, enemyPos.z};
    EmitParticleBurst(explosionParticles_, origin, 110, 1.38f,
                                  AppParticleBurstStyle::Explosion,
                                  {1.0f, 0.30f, 0.05f, 0.92f},
                                  {0.0f, 1.0f, 0.0f}, 3.0f);
    EmitParticleBurst(sparkParticles_, origin, 230, 1.85f,
                              AppParticleBurstStyle::Sparks,
                              {1.0f, 0.86f, 0.30f, 1.0f},
                              {0.0f, 1.0f, 0.0f}, 7.0f);
    EmitParticleBurst(smokeParticles_, origin, 52, 1.42f,
                              AppParticleBurstStyle::Flash,
                              {1.0f, 0.58f, 0.12f, 0.76f},
                              {0.0f, 1.0f, 0.0f}, 1.0f);
    if (soundsLoaded_ && ctx_ != nullptr && ctx_->systems.sound != nullptr) {
        ctx_->systems.sound->Play(explosionSoundId_);
    }
}

void GameScene::UpdateBattleIntro(float deltaTime) {
    battleIntroTimer_ += deltaTime;
    sceneLightTime_ += deltaTime;
    combatFeedback_.Update(deltaTime, sceneLightTime_);

    const float ratio =
        std::clamp(battleIntroTimer_ / battleIntroDuration_, 0.0f, 1.0f);
    const float dissolveProgress =
        std::clamp((battleIntroTimer_ - 0.32f) / 2.02f, 0.0f, 1.0f);
    const float reveal = SmoothStep01(dissolveProgress);
    ApplyEnemyIntroDissolve(reveal);
    ApplyBattlePostProcess(ctx_, 0.035f * (1.0f - ratio),
                           0.16f + 0.06f * ratio, 0.0f, 0.48f, 18);
    if (!battleIntroRevealEmitted_ && battleIntroTimer_ >= 2.36f) {
        battleIntroRevealEmitted_ = true;
        const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
        EmitParticleBurst(swordFlashParticles_, 
            {enemyPos.x, enemyPos.y + 1.45f, enemyPos.z}, 8, 0.38f,
            AppParticleBurstStyle::Flash,
            {1.0f, 0.98f, 0.88f, 0.82f}, {0.0f, 1.0f, 0.0f}, 0.22f);
        EmitParticleBurst(explosionParticles_, 
            {enemyPos.x, enemyPos.y + 1.30f, enemyPos.z}, 156, 2.12f,
            AppParticleBurstStyle::SpiritSparkle,
            {1.0f, 1.0f, 0.96f, 0.68f}, {0.0f, 1.0f, 0.0f}, 1.52f);
        if (soundsLoaded_ && ctx_ != nullptr && ctx_->systems.sound != nullptr) {
            ctx_->systems.sound->Play(enemyReleaseSoundId_);
        }
    }

    ctx_->rendering.model->UpdateAnimation(playerModelId_, deltaTime * 0.04f);
    enemy_.FaceTargetImmediately(player_.GetTransform().position);
    UpdateBattleIntroEnemyAnimation(deltaTime);
    ApplyEnemyProceduralAnimation();
    UpdateSceneLighting();
    UpdateBattleCamera();
    camera_.UpdateMatrices();
    sparkParticles_.Update(deltaTime);
    explosionParticles_.Update(deltaTime);
    smokeParticles_.Update(deltaTime);
    swordFlashParticles_.Update(deltaTime);

    if (battleIntroTimer_ >= battleIntroDuration_) {
        battleIntroActive_ = false;
        battleIntroTimer_ = battleIntroDuration_;
        ApplyEnemyIntroDissolve(1.0f);
        ClearBattlePostProcess(ctx_);
    }
}

void GameScene::ApplyEnemyIntroDissolve(float revealRatio) {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr || enemyModelId_ == 0) {
        return;
    }
    Model *model = ctx_->rendering.model->GetModel(enemyModelId_);
    if (model == nullptr) {
        return;
    }

    const float reveal = std::clamp(revealRatio, 0.0f, 1.0f);
    const bool dissolveActive = battleIntroActive_ && reveal < 0.995f;
    const float liquidRipple =
        dissolveActive ? 0.045f * std::sinf(battleIntroTimer_ * 18.0f) *
                             (1.0f - reveal)
                       : 0.0f;
    const float threshold =
        std::clamp(1.08f - reveal * 1.18f + liquidRipple, 0.0f, 1.0f);
    const float alpha = SmoothStep01(reveal);
    for (ModelSubMesh &subMesh : model->subMeshes) {
        Material material = ctx_->rendering.model->GetMaterial(subMesh.materialId);
        material.enableDissolve = dissolveActive ? 1.0f : 0.0f;
        material.dissolveThreshold = threshold;
        material.dissolveEdgeWidth = 0.11f + 0.11f * (1.0f - reveal);
        material.dissolveEdgeColor = {1.0f, 0.70f, 0.30f, 0.70f};
        material.color.w = dissolveActive ? alpha : 1.0f;
        material.blendMode = dissolveActive
                                 ? static_cast<int32_t>(BlendMode::Transparent)
                                 : static_cast<int32_t>(BlendMode::Opaque);
        material.depthWrite = dissolveActive ? 0 : 1;
        ctx_->rendering.model->SetMaterial(subMesh.materialId, material);
    }
}

void GameScene::BeginVictorySequence() {
    battleResultRequested_ = true;
    victorySequenceActive_ = true;
    victorySequenceTimer_ = 0.0f;
    victoryClearTime_ = battleElapsedTime_;
    victoryFinalExplosionEmitted_ = false;
    victoryEnemyStartPos_ = enemy_.GetTransform().position;
    counterCinematicActive_ = false;
    SetEnemyAnimationFrozen(false);
    if (ctx_ != nullptr) {
        hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(),
                    enemy_.GetMaxHP());
    }

    ApplyBattlePostProcess(ctx_, 0.055f, 0.58f, 0.10f, 0.48f, 24);

    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    EmitParticleBurst(sparkParticles_, 
        {enemyPos.x, enemyPos.y + 1.45f, enemyPos.z}, 220, 1.85f,
        AppParticleBurstStyle::Flash, {1.0f, 0.96f, 0.82f, 0.95f},
        {0.0f, 1.0f, 0.0f}, 0.72f);
    EmitParticleBurst(explosionParticles_, 
        {enemyPos.x, enemyPos.y + 1.25f, enemyPos.z}, 96, 1.35f,
        AppParticleBurstStyle::Explosion,
        {1.0f, 0.84f, 0.32f, 0.72f}, {0.0f, 1.0f, 0.0f}, 1.15f);
}

void GameScene::BeginDefeatSequence() {
    battleResultRequested_ = true;
    defeatSequenceActive_ = true;
    defeatSequenceTimer_ = 0.0f;
    defeatImpactEmitted_ = false;
    counterCinematicActive_ = false;
    SetEnemyAnimationFrozen(false);
    player_.SetDefeatPoseRatio(0.0f);

    ApplyBattlePostProcess(ctx_, 0.040f, 0.62f, 0.18f, 0.54f, 22);

    const XMFLOAT3 playerPos = player_.GetTransform().position;
    EmitParticleBurst(explosionParticles_, 
        {playerPos.x, playerPos.y + 1.05f, playerPos.z}, 180, 1.55f,
        AppParticleBurstStyle::Explosion,
        {1.0f, 0.12f, 0.05f, 0.82f}, {0.0f, 1.0f, 0.0f}, 2.2f);
    EmitParticleBurst(sparkParticles_, 
        {playerPos.x, playerPos.y + 1.18f, playerPos.z}, 260, 1.35f,
        AppParticleBurstStyle::Sparks,
        {1.0f, 0.32f, 0.16f, 0.92f}, {0.0f, 1.0f, 0.0f}, 5.2f);
}

void GameScene::UpdateDefeatSequence(float deltaTime) {
    defeatSequenceTimer_ += deltaTime;
    const float fallStart = 0.34f;
    const float fallDuration = 1.22f;
    const float fallRatio =
        std::clamp((defeatSequenceTimer_ - fallStart) / fallDuration, 0.0f,
                   1.0f);
    player_.SetDefeatPoseRatio(fallRatio);

    const float postProcessRatio =
        std::clamp(defeatSequenceTimer_ / defeatSequenceDuration_, 0.0f, 1.0f);
    ApplyBattlePostProcess(ctx_, 0.040f * (1.0f - postProcessRatio), 0.62f,
                           0.18f + 0.22f * postProcessRatio, 0.54f, 22);

    if (!defeatImpactEmitted_ && defeatSequenceTimer_ >= 1.58f) {
        defeatImpactEmitted_ = true;
        const XMFLOAT3 playerPos = player_.GetTransform().position;
        EmitParticleBurst(smokeParticles_, 
            {playerPos.x, playerPos.y + 0.28f, playerPos.z}, 160, 2.15f,
            AppParticleBurstStyle::Smoke,
            {0.18f, 0.17f, 0.16f, 0.88f}, {0.0f, 1.0f, 0.0f}, 0.85f);
        EmitParticleBurst(sparkParticles_, 
            {playerPos.x, playerPos.y + 0.42f, playerPos.z}, 180, 1.05f,
            AppParticleBurstStyle::Sparks,
            {1.0f, 0.18f, 0.08f, 0.86f}, {0.0f, 1.0f, 0.0f}, 4.2f);
    }

    if (defeatSequenceTimer_ >= defeatSequenceDuration_) {
        defeatSequenceActive_ = false;
        player_.SetDefeatPoseRatio(0.0f);
        ClearBattlePostProcess(ctx_);
        sceneManager_->ChangeScene(std::make_unique<BattleResultScene>(
            BattleResultScene::ResultKind::GameOver, battleElapsedTime_,
            inputCalibration_));
    }
}

void GameScene::UpdateVictorySequence(float deltaTime) {
    victorySequenceTimer_ += deltaTime;
    const float ratio =
        std::clamp(victorySequenceTimer_ / victorySequenceDuration_, 0.0f,
                   1.0f);

    const float stepped = std::floor(ratio * 14.0f) / 14.0f;
    const float blur = (1.0f - stepped) * 0.070f;
    ApplyBattlePostProcess(ctx_, blur, 0.58f, 0.10f + stepped * 0.18f,
                           0.48f, 24);

    if (!victoryFinalExplosionEmitted_ && victorySequenceTimer_ >= 3.90f) {
        victoryFinalExplosionEmitted_ = true;
        const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
        if (soundsLoaded_ && ctx_ != nullptr && ctx_->systems.sound != nullptr) {
            ctx_->systems.sound->Play(explosionSoundId_);
        }
        EmitParticleBurst(explosionParticles_, 
            {enemyPos.x, enemyPos.y + 1.05f, enemyPos.z}, 3200, 8.40f,
            AppParticleBurstStyle::Explosion,
            {1.0f, 0.64f, 0.08f, 1.0f}, {0.0f, 1.0f, 0.0f}, 5.80f);
        EmitParticleBurst(sparkParticles_, 
            {enemyPos.x, enemyPos.y + 1.22f, enemyPos.z}, 3600, 9.20f,
            AppParticleBurstStyle::Sparks,
            {1.0f, 0.98f, 0.58f, 1.0f}, {0.0f, 1.0f, 0.0f}, 12.4f);
        EmitParticleBurst(smokeParticles_, 
            {enemyPos.x, enemyPos.y + 1.12f, enemyPos.z}, 680, 6.80f,
            AppParticleBurstStyle::Flash,
            {1.0f, 0.94f, 0.70f, 1.0f}, {0.0f, 1.0f, 0.0f}, 1.55f);
    }

    if (victorySequenceTimer_ >= victorySequenceDuration_) {
        victorySequenceActive_ = false;
        ClearBattlePostProcess(ctx_);
        sceneManager_->ChangeScene(std::make_unique<BattleResultScene>(
            BattleResultScene::ResultKind::Clear, victoryClearTime_,
            inputCalibration_));
    }
}

void GameScene::DrawVictoryFlash() {
    if (!victorySequenceActive_ || ctx_ == nullptr || ctx_->rendering.sprite == nullptr ||
        ctx_->systems.winApp == nullptr) {
        return;
    }

    const float introPulse =
        victorySequenceTimer_ < 0.82f
            ? 0.56f + 0.44f * std::sin(victorySequenceTimer_ * 12.0f)
            : 0.0f;
    const float earlyFlash =
        (std::max)(0.0f, 1.0f - victorySequenceTimer_ / 0.88f);
    const auto flashPulse = [&](float center, float width, float peak) {
        return (std::max)(
            0.0f, (1.0f - std::fabs(victorySequenceTimer_ - center) / width) *
                      peak);
    };
    const float fallFlash =
        (std::max)(flashPulse(1.35f, 0.20f, 0.70f),
                   flashPulse(2.25f, 0.22f, 0.80f));
    const float preExplosionFlash = flashPulse(3.58f, 0.42f, 1.0f);
    const float blink =
        (std::max)(introPulse * introPulse,
                   (std::max)(fallFlash, preExplosionFlash));
    const float finalFlash =
        victoryFinalExplosionEmitted_
            ? (std::max)(
                  0.0f,
                  1.0f - (victorySequenceTimer_ - 3.90f) / 0.52f)
            : 0.0f;
    const float alpha =
        std::clamp((std::max)(earlyFlash, (std::max)(blink, finalFlash)),
                   0.0f, 1.0f);
    if (alpha <= 0.01f) {
        return;
    }

    Sprite flash{};
    flash.textureId = 0;
    flash.position = {0.0f, 0.0f};
    flash.size = {static_cast<float>(ctx_->systems.winApp->GetWidth()),
                  static_cast<float>(ctx_->systems.winApp->GetHeight())};
    flash.color = {1.0f, 1.0f, 1.0f, alpha};

    ctx_->rendering.sprite->PreDraw();
    ctx_->rendering.sprite->DrawSprite(flash);
    ctx_->rendering.sprite->PostDraw();
}

void GameScene::DrawDefeatFlash() {
    if (!defeatSequenceActive_ || ctx_ == nullptr || ctx_->rendering.sprite == nullptr ||
        ctx_->systems.winApp == nullptr) {
        return;
    }

    const auto pulse = [&](float center, float width, float peak) {
        return (std::max)(
            0.0f, (1.0f - std::fabs(defeatSequenceTimer_ - center) / width) *
                      peak);
    };
    const float red =
        (std::max)(pulse(0.08f, 0.22f, 0.72f), pulse(1.58f, 0.30f, 0.36f));
    const float black =
        std::clamp((defeatSequenceTimer_ - 1.55f) /
                       (defeatSequenceDuration_ - 1.55f),
                   0.0f, 0.72f);
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    ctx_->rendering.sprite->PreDraw();
    if (red > 0.01f) {
        Sprite flash{};
        flash.textureId = 0;
        flash.position = {0.0f, 0.0f};
        flash.size = {w, h};
        flash.color = {1.0f, 0.06f, 0.02f, red};
        ctx_->rendering.sprite->DrawSprite(flash);
    }
    if (black > 0.01f) {
        Sprite fade{};
        fade.textureId = 0;
        fade.position = {0.0f, 0.0f};
        fade.size = {w, h};
        fade.color = {0.0f, 0.0f, 0.0f, black};
        ctx_->rendering.sprite->DrawSprite(fade);
    }
    ctx_->rendering.sprite->PostDraw();
}

void GameScene::DrawBattleIntroFlash() {
    if (!battleIntroActive_ || ctx_ == nullptr || ctx_->rendering.sprite == nullptr ||
        ctx_->systems.winApp == nullptr) {
        return;
    }

    const auto flashPulse = [&](float center, float width, float peak) {
        return (std::max)(
            0.0f, (1.0f - std::fabs(battleIntroTimer_ - center) / width) *
                      peak);
    };
    const float appearFlash = flashPulse(2.36f, 0.24f, 0.58f);
    const float afterGlow = flashPulse(2.68f, 0.52f, 0.22f);
    const float alpha = std::clamp((std::max)(appearFlash, afterGlow), 0.0f,
                                   0.62f);
    if (alpha <= 0.01f) {
        return;
    }

    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());
    ctx_->rendering.sprite->PreDraw();
    Sprite flash{};
    flash.textureId = 0;
    flash.position = {0.0f, 0.0f};
    flash.size = {w, h};
    flash.color = {1.0f, 0.985f, 0.93f, alpha};
    ctx_->rendering.sprite->DrawSprite(flash);
    if (appearFlash > 0.01f) {
        Sprite flare{};
        flare.textureId = 0;
        flare.position = {w * 0.15f, h * 0.39f};
        flare.size = {w * 0.70f, h * 0.18f};
        flare.color = {1.0f, 1.0f, 1.0f, appearFlash * 0.16f};
        ctx_->rendering.sprite->DrawSprite(flare);
    }
    ctx_->rendering.sprite->PostDraw();
}

void GameScene::DrawBladeClashFinishBackdrop() {
    DrawArena();
}

void GameScene::DrawDistantHazardBackdrop() {
    ModelManager *model = ctx_->rendering.model;
    const float pulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 1.8f);

    ModelDrawEffect cityEffect{};
    cityEffect.enabled = true;
    cityEffect.additiveBlend = false;
    cityEffect.disableCulling = true;
    cityEffect.color = {0.16f, 0.20f, 0.24f, 0.90f};
    cityEffect.intensity = 0.12f;
    cityEffect.fresnelPower = 0.95f;
    cityEffect.noiseAmount = 0.0f;
    cityEffect.time = sceneLightTime_ * 0.45f;
    model->SetDrawEffect(cityEffect);
    std::vector<Transform> cityTowers;
    cityTowers.reserve(4u * 3u * 17u);
    for (int side = 0; side < 4; ++side) {
        const bool alongX = side < 2;
        const float sign = (side == 0 || side == 2) ? 1.0f : -1.0f;
        for (int row = 0; row < 3; ++row) {
            for (int i = 0; i < 17; ++i) {
                const float lane = (static_cast<float>(i) - 8.0f) * 4.15f;
                const float depthLane = 48.0f + static_cast<float>(row) * 4.1f;
                const float height =
                    2.4f + static_cast<float>((i * 5 + row * 7 + side) % 10) *
                               0.45f +
                    static_cast<float>(row) * 0.55f;
                Transform tower{};
                tower.position = alongX
                                     ? XMFLOAT3{lane, -0.68f,
                                               sign * depthLane + 8.0f}
                                     : XMFLOAT3{sign * depthLane, -0.68f,
                                               lane + 8.0f};
                tower.rotation =
                    MakeQuat(0.0f, alongX ? 0.0f : kPi * 0.5f, 0.0f);
                tower.scale = {0.62f + static_cast<float>((i + row) % 3) * 0.16f,
                               height,
                               0.78f + static_cast<float>((i * 3 + row) % 2) *
                                           0.24f};
                cityTowers.push_back(tower);
            }
        }
    }
    if (!cityTowers.empty()) {
        model->DrawInstanced(arenaCityTowerModelId_, cityTowers.data(),
                             static_cast<uint32_t>(cityTowers.size()), camera_);
    }
    model->ClearDrawEffect();

    ModelDrawEffect blockEffect = cityEffect;
    blockEffect.color = {0.07f, 0.10f, 0.13f, 0.92f};
    blockEffect.intensity = 0.18f;
    model->SetDrawEffect(blockEffect);
    std::vector<Transform> cityBlocks;
    cityBlocks.reserve(23u);
    for (int i = 0; i < 15; ++i) {
        const float offset = static_cast<float>(i - 7);
        Transform wall{};
        wall.position = {offset * 1.72f, -0.76f,
                         72.0f + std::fabs(offset) * 0.42f};
        wall.rotation = MakeQuat(0.0f, 0.0f, 0.0f);
        wall.scale = {1.28f + 0.12f * static_cast<float>(i % 3),
                      5.3f + static_cast<float>((i * 5) % 5) * 0.72f,
                      1.08f};
        cityBlocks.push_back(wall);
    }
    for (int side = 0; side < 2; ++side) {
        const float sign = side == 0 ? -1.0f : 1.0f;
        for (int step = 0; step < 4; ++step) {
            Transform brace{};
            brace.position = {sign * (9.2f + static_cast<float>(step) * 2.25f),
                              2.25f + static_cast<float>(step) * 0.62f,
                              69.4f + static_cast<float>(step) * 1.55f};
            brace.rotation = MakeQuat(0.0f, sign * 0.12f, 0.0f);
            brace.scale = {2.8f - static_cast<float>(step) * 0.22f, 0.30f,
                           0.78f};
            cityBlocks.push_back(brace);
        }
    }
    if (!cityBlocks.empty()) {
        model->DrawInstanced(arenaCityTowerModelId_, cityBlocks.data(),
                             static_cast<uint32_t>(cityBlocks.size()), camera_);
    }
    model->ClearDrawEffect();

    ModelDrawEffect lightEffect{};
    lightEffect.enabled = true;
    lightEffect.additiveBlend = true;
    lightEffect.disableCulling = true;
    lightEffect.color = {0.96f, 0.58f, 0.28f, 0.18f};
    lightEffect.intensity = 0.035f + 0.025f * pulse;
    lightEffect.fresnelPower = 0.8f;
    lightEffect.noiseAmount = 0.0f;
    lightEffect.time = sceneLightTime_;
    model->SetDrawEffect(lightEffect);
    std::vector<Transform> cityWindows;
    cityWindows.reserve(4u * 11u);
    for (int side = 0; side < 4; ++side) {
        const bool alongX = side < 2;
        const float sign = (side == 0 || side == 2) ? 1.0f : -1.0f;
        for (int i = 0; i < 11; ++i) {
            const float lane = (static_cast<float>(i) - 5.0f) * 4.0f;
            Transform panel{};
            panel.position = alongX
                                 ? XMFLOAT3{lane, 1.15f + static_cast<float>(i % 4) * 0.58f,
                                           sign * 47.35f + 8.0f}
                                 : XMFLOAT3{sign * 47.35f,
                                           1.15f + static_cast<float>(i % 4) * 0.58f,
                                           lane + 8.0f};
            panel.rotation =
                MakeQuat(0.0f, alongX ? 0.0f : kPi * 0.5f, 0.0f);
            panel.scale = {0.16f, 1.05f + static_cast<float>(i % 2) * 0.40f,
                           1.0f};
            cityWindows.push_back(panel);
        }
    }
    if (!cityWindows.empty()) {
        model->DrawInstanced(arenaCityWindowModelId_, cityWindows.data(),
                             static_cast<uint32_t>(cityWindows.size()), camera_);
    }
    model->ClearDrawEffect();
}

void GameScene::DrawArena() {
    ModelManager *model = ctx_->rendering.model;
    const float pulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 2.2f);
    constexpr float kPatternSpacing = 3.55f;
    constexpr float kLaneSpacing = 4.25f;

    DrawDistantHazardBackdrop();

    Transform floor{};
    floor.position = {0.0f, -0.04f, 0.0f};
    floor.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    floor.scale = {180.0f, 180.0f, 1.0f};
    model->Draw(arenaFloorModelId_, floor, camera_);

    ModelDrawEffect fieldEffect{};
    fieldEffect.enabled = true;
    fieldEffect.additiveBlend = false;
    fieldEffect.disableCulling = true;
    fieldEffect.color = {0.085f, 0.075f, 0.060f, 0.115f};
    fieldEffect.intensity = 0.004f;
    fieldEffect.fresnelPower = 0.7f;
    fieldEffect.noiseAmount = 0.0f;
    fieldEffect.time = sceneLightTime_;
    model->SetDrawEffect(fieldEffect);

    std::vector<Transform> fieldTiles;
    fieldTiles.reserve(480u);
    for (int z = -14; z <= 14; ++z) {
        for (int x = -14; x <= 14; ++x) {
            if ((std::abs(x) + std::abs(z)) % 2 != 0) {
                continue;
            }
            Transform tile{};
            tile.position = {static_cast<float>(x) * kPatternSpacing, 0.004f,
                             static_cast<float>(z) * kPatternSpacing};
            tile.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
            tile.scale = {2.26f, 2.26f, 1.0f};
            fieldTiles.push_back(tile);
        }
    }

    Transform center{};
    center.position = {0.0f, 0.012f, 0.0f};
    center.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    center.scale = {4.8f, 4.8f, 1.0f};
    fieldTiles.push_back(center);

    for (int i = -13; i <= 13; ++i) {
        Transform laneX{};
        laneX.position = {0.0f, 0.016f,
                          static_cast<float>(i) * kLaneSpacing};
        laneX.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        laneX.scale = {168.0f, i == 0 ? 0.080f : 0.034f, 1.0f};
        fieldTiles.push_back(laneX);

        Transform laneZ{};
        laneZ.position = {static_cast<float>(i) * kLaneSpacing, 0.017f,
                          0.0f};
        laneZ.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        laneZ.scale = {i == 0 ? 0.080f : 0.034f, 168.0f, 1.0f};
        fieldTiles.push_back(laneZ);
    }
    if (!fieldTiles.empty()) {
        model->DrawInstanced(arenaSpokeModelId_, fieldTiles.data(),
                             static_cast<uint32_t>(fieldTiles.size()), camera_);
    }
    model->ClearDrawEffect();

    ModelDrawEffect lineEffect{};
    lineEffect.enabled = true;
    lineEffect.additiveBlend = true;
    lineEffect.disableCulling = true;
    lineEffect.color = {0.22f, 0.10f, 0.045f, 0.026f};
    lineEffect.intensity = 0.0015f + 0.0015f * pulse;
    lineEffect.fresnelPower = 0.75f;
    lineEffect.noiseAmount = 0.0f;
    lineEffect.time = sceneLightTime_;
    model->SetDrawEffect(lineEffect);
    std::vector<Transform> glowLines;
    glowLines.reserve(62u);
    for (int i = -15; i <= 15; ++i) {
        Transform lightX{};
        lightX.position = {0.0f, 0.034f,
                           static_cast<float>(i) * kPatternSpacing};
        lightX.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        lightX.scale = {160.0f, 0.016f, 1.0f};
        glowLines.push_back(lightX);

        Transform lightZ{};
        lightZ.position = {static_cast<float>(i) * kPatternSpacing, 0.035f,
                           0.0f};
        lightZ.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        lightZ.scale = {0.016f, 160.0f, 1.0f};
        glowLines.push_back(lightZ);
    }
    if (!glowLines.empty()) {
        model->DrawInstanced(arenaCityWindowModelId_, glowLines.data(),
                             static_cast<uint32_t>(glowLines.size()), camera_);
    }
    model->ClearDrawEffect();
}

