#include "GameScene.h"
#include "AppSceneServices.h"
#include "BattleResultScene.h"
#include "BladeClashCinematic.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "Material.h"
#include "Model.h"
#include "ModelManager.h"
#include "PostProcessSystem.h"
#include "SceneManager.h"
#include "SoundManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "TitleScene.h"
#include "TutorialSelectScene.h"
#include "WeaponSelectScene.h"
#include "WinApp.h"
#include <Xinput.h>
#include "compat/ParticleCompat.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <vector>

using namespace DirectX;

namespace {
constexpr float kPi = 3.14159265f;
namespace Clash = BladeClashCinematic;
constexpr float kSlashSoundVolume = 0.48f;
constexpr float kEnemyReleaseSoundVolume = 0.42f;
constexpr float kHitSoundVolume = 0.46f;
constexpr float kCounterSoundVolume = 0.44f;
constexpr float kDamageSoundVolume = 0.46f;
constexpr float kExplosionSoundVolume = 0.40f;
constexpr int kTutorialTextLeftRight = 0;
constexpr int kTutorialTextRightSword = 1;
constexpr int kTutorialTextWait = 2;
constexpr int kTutorialTextVertical = 3;
constexpr int kTutorialTextHorizontal = 4;
constexpr int kTutorialTextRelease = 5;
constexpr int kTutorialTextSuccess = 6;
constexpr int kTutorialTextMiss = 7;
constexpr int kTutorialTextComplete = 8;
constexpr int kTutorialTextExcellent = 9;
constexpr int kTutorialStepLeftRight = 0;
constexpr int kTutorialStepRightSword = 1;
constexpr int kTutorialStepRedSmash = 2;
constexpr int kTutorialStepGreenSmash = 3;
constexpr int kTutorialStepRedSweep = 4;
constexpr int kTutorialStepGreenSweep = 5;
constexpr int kTutorialStepPractice = 6;
constexpr float kTutorialEntryFadeDuration = 0.64f;
constexpr float kTutorialEntryBlackHold = 0.14f;
constexpr float kTutorialExitFadeDuration = 0.42f;
constexpr int kDebugBladeClashKey = DIK_F6;
constexpr uint16_t kHandCameraPreviewPort = 5006;
constexpr float kHandCameraPreviewStaleSeconds = 0.75f;

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
                            float centerY = 0.48f, int32_t sampleCount = 20,
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
    uint32_t chargeWeakPointModelId = 0;
};

SharedBattleModels gSharedBattleModels;

Material MakeArenaMaterial(const XMFLOAT4 &color, bool useTexture = false,
                           float reflection = 0.34f, float roughness = 0.48f) {
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

float BillboardYawToCamera(const XMFLOAT3 &position,
                           const XMFLOAT3 &cameraPos) {
    return std::atan2f(cameraPos.x - position.x, cameraPos.z - position.z);
}

float DistanceSq(const XMFLOAT3 &a, const XMFLOAT3 &b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

SwordCounterAxis RequiredVisualCounterAxisForAction(ActionKind kind) {
    switch (kind) {
    case ActionKind::Smash:
        return SwordCounterAxis::Vertical;
    case ActionKind::Sweep:
        return SwordCounterAxis::Horizontal;
    default:
        return SwordCounterAxis::None;
    }
}

uint32_t Hash2D(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return h ^ (h >> 16u);
}

uint32_t CreateProceduralTexture(TextureManager *texture, uint32_t width,
                                 uint32_t height, const XMFLOAT3 &baseColor,
                                 const XMFLOAT3 &accentColor, uint32_t seed,
                                  float grainStrength,
                                  bool isotropicPattern = false) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t h = Hash2D(x / 3u, y / 3u, seed);
            const float noise = static_cast<float>(h & 255u) / 255.0f;
            float pattern = 0.0f;
            if (isotropicPattern) {
                const float broad =
                    static_cast<float>(Hash2D(x / 13u, y / 13u, seed + 17u) &
                                       255u) /
                    255.0f;
                const float mid =
                    static_cast<float>(Hash2D(x / 7u, y / 7u, seed + 23u) &
                                       255u) /
                    255.0f;
                const float remaining =
                    (std::max)(1.0f - grainStrength, 0.0f);
                pattern = broad * remaining * 0.58f + mid * remaining * 0.42f;
            } else {
                pattern =
                    static_cast<float>(
                        Hash2D(x / 19u, y / 7u, seed + 17u) & 255u) /
                    255.0f * (1.0f - grainStrength);
            }
            const float t =
                std::clamp(noise * grainStrength + pattern, 0.0f, 1.0f);
            const float fine =
                static_cast<float>(Hash2D(x, y, seed + 31u) & 63u) / 255.0f;
            XMFLOAT3 color{
                std::clamp(baseColor.x + (accentColor.x - baseColor.x) * t +
                               fine,
                           0.0f, 1.0f),
                std::clamp(baseColor.y + (accentColor.y - baseColor.y) * t +
                               fine,
                           0.0f, 1.0f),
                std::clamp(baseColor.z + (accentColor.z - baseColor.z) * t +
                               fine,
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
                                   {0.23f, 0.22f, 0.20f}, {0.70f, 0.30f, 0.12f},
                                   0x914Au, 0.62f, true);
}

uint32_t AppCreateArenaStoneTexture(TextureManager *texture, uint32_t width,
                                    uint32_t height) {
    return CreateProceduralTexture(texture, width, height,
                                   {0.07f, 0.08f, 0.09f}, {0.22f, 0.24f, 0.22f},
                                   0x51C3u, 0.48f);
}

XMFLOAT3 Lerp(const XMFLOAT3 &a, const XMFLOAT3 &b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t};
}

float SmoothStep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void ApplyBattleIntroClearColor(DirectXCommon *dxCommon, float introTimer) {
    if (dxCommon == nullptr) {
        return;
    }

    constexpr float kReleaseFlashTime = 2.36f;
    constexpr float kReleaseFlashWidth = 0.24f;
    constexpr XMFLOAT4 kReleasedClear{0.180f, 0.180f, 0.185f, 1.0f};
    if (introTimer < kReleaseFlashTime) {
        dxCommon->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    const XMFLOAT4 base = kReleasedClear;
    const float pulse = std::clamp(
        1.0f - std::fabs(introTimer - kReleaseFlashTime) / kReleaseFlashWidth,
        0.0f, 1.0f);
    const float brighten = pulse * 0.16f;
    dxCommon->SetClearColor(base.x + (1.0f - base.x) * brighten,
                            base.y + (1.0f - base.y) * brighten,
                            base.z + (1.0f - base.z) * brighten, base.w);
}

void ApplyReleasedClearColor(DirectXCommon *dxCommon) {
    if (dxCommon == nullptr) {
        return;
    }
    dxCommon->SetClearColor(0.220f, 0.220f, 0.225f, 1.0f);
}

void ApplyReleasedPostProcess(const SceneContext *ctx) {
    if (ctx == nullptr || ctx->rendering.postProcessSystem == nullptr) {
        return;
    }

    PostProcessProfile profile = GetPostProcessProfile(ctx);
    profile.vignette.enabled = false;
    profile.vignette.strength = 0.0f;
    profile.radialBlur.center[0] = 0.5f;
    profile.radialBlur.center[1] = 0.48f;
    profile.radialBlur.sampleCount = 18;
    profile.radialBlur.strength = 0.0f;
    profile.sceneDim.strength = 0.0f;
    SetPostProcessProfile(ctx, profile);
}

float GetChargeStanceSettleTime(ActionKind kind) {
    switch (kind) {
    case ActionKind::Smash:
        return 0.46f;
    case ActionKind::Sweep:
        return 0.42f;
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

GameScene::~GameScene() { StopBattleBgm(); }

void GameScene::SetReadyPreviewHeat(float heat) {
    readyPreviewHeat_ = std::clamp(heat, 0.0f, 1.0f);
}

void GameScene::StartBattleBgm() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr ||
        titleDemoMode_ || backgroundOnlyMode_ || readyPreviewMode_ ||
        battleBgmVoiceHandle_ != SoundManager::kInvalidVoiceHandle) {
        return;
    }

    const wchar_t *bgmPath = tutorialMode_
                                 ? L"app/resources/audio/bgm/bgm_TutorialTheme.wav"
                                 : L"app/resources/audio/bgm/bgm_Battle.wav";
    battleBgmSoundId_ = ctx_->systems.sound->LoadOrCreateSilent(bgmPath);
    battleBgmVoiceHandle_ = ctx_->systems.sound->Play(battleBgmSoundId_, 0.36f,
                                                      true);
}

void GameScene::StopBattleBgm() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr ||
        battleBgmVoiceHandle_ == SoundManager::kInvalidVoiceHandle) {
        return;
    }

    ctx_->systems.sound->Stop(battleBgmVoiceHandle_);
    battleBgmVoiceHandle_ = SoundManager::kInvalidVoiceHandle;
}

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    ctx_->rendering.dxCommon->ResetClearColor();
    if (ctx_->rendering.postProcessSystem != nullptr) {
        ctx_->rendering.postProcessSystem->SetProfile(PostProcessProfile{});
    }
    combatFeedback_.Initialize((titleDemoMode_ || backgroundOnlyMode_ ||
                                readyPreviewMode_)
                                   ? nullptr
                                   : ctx_->rendering.postProcessSystem);

    float aspect = static_cast<float>(ctx_->systems.winApp->GetWidth()) /
                   static_cast<float>(ctx_->systems.winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.UpdateMatrices();
    camera_.SetPerspectiveFovDeg(currentFovDeg_);

    DirectXCommon *dx = ctx_->rendering.dxCommon;
    ModelManager *model = ctx_->rendering.model;
    TextureManager *texture = ctx_->rendering.texture;

    uint32_t playerModel =
        model->Load(L"app/resources/models/player/player.glb");
    uint32_t swordModel = model->Load(L"app/resources/models/player/sword.glb");
    uint32_t enemyModel = model->Load(L"app/resources/models/boss/boss.gltf");
    particleTextureId_ =
        texture->Load(L"app/resources/effects/particles/smoke.png");
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
    if (!gSharedBattleModels.initialized) {
        gSharedBattleModels.arenaNoiseTextureId = arenaStoneTextureId;
        gSharedBattleModels.arenaFloorModelId =
            model->CreatePlane(arenaStoneTextureId,
                               MakeArenaMaterial({0.070f, 0.085f, 0.105f, 1.0f},
                                                 true, 0.025f, 0.84f));
        gSharedBattleModels.arenaLowPolyTerrainModelId = model->CreatePlane(
            arenaStoneTextureId, MakeArenaMaterial({0.075f, 0.10f, 0.13f, 1.0f},
                                                   true, 0.016f, 0.80f));
        gSharedBattleModels.arenaDistantTerrainModelId = model->CreatePlane(
            arenaStoneTextureId,
            MakeArenaMaterial({0.12f, 0.18f, 0.22f, 1.0f}, true, 0.02f, 0.88f));
        gSharedBattleModels.arenaHazardSpireModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.10f, 0.15f, 0.15f, 1.0f}, true, 0.025f, 0.88f),
            7, 0.04f, 0.92f, 8.8f);
        gSharedBattleModels.arenaHazardGlowRingModelId = model->CreateRing(
            0,
            MakeArenaMaterial({0.55f, 0.88f, 0.96f, 0.42f}, false, 0.0f, 0.46f),
            48, 2.45f, 0.34f);
        gSharedBattleModels.arenaCityTowerModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.18f, 0.24f, 0.29f, 1.0f}, true, 0.025f, 0.76f),
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
            MakeArenaMaterial({0.05f, 0.09f, 0.10f, 1.0f}, true, 0.01f, 0.94f),
            8, 0.92f, 1.05f, 1.15f);
        gSharedBattleModels.arenaCenterDiskModelId = model->CreateRing(
            arenaStoneTextureId,
            MakeArenaMaterial({0.72f, 0.58f, 0.30f, 1.0f}, false, 0.12f, 0.30f),
            96, 1.95f, 0.0f);
        gSharedBattleModels.arenaSpokeModelId = model->CreatePlane(
            arenaStoneTextureId, MakeArenaMaterial({0.28f, 0.21f, 0.12f, 1.0f},
                                                   false, 0.06f, 0.58f));
        gSharedBattleModels.arenaInnerRingModelId = model->CreateRing(
            arenaStoneTextureId,
            MakeArenaMaterial({0.64f, 0.60f, 0.44f, 1.0f}, false, 0.12f, 0.32f),
            96, 4.9f, 4.35f);
        gSharedBattleModels.arenaOuterRingModelId = model->CreateRing(
            arenaStoneTextureId,
            MakeArenaMaterial({0.68f, 0.28f, 0.18f, 1.0f}, false, 0.10f, 0.38f),
            128, 12.3f, 11.6f);
        gSharedBattleModels.arenaColumnModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.22f, 0.26f, 0.30f, 1.0f}, true, 0.045f, 0.68f),
            24, 0.26f, 0.38f, 5.4f);
        gSharedBattleModels.arenaColumnCapModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.62f, 0.30f, 0.18f, 1.0f}, false, 0.10f, 0.38f),
            32, 0.68f, 0.78f, 0.24f);
        gSharedBattleModels.arenaDomeModelId = model->CreateCylinder(
            arenaStoneTextureId,
            MakeArenaMaterial({0.09f, 0.14f, 0.28f, 0.78f}, true, 0.00f, 0.72f),
            128, 4.5f, 13.5f, 8.8f);
        gSharedBattleModels.arenaBarrierRingModelId =
            model->CreateRing(arenaStoneTextureId,
                              MakeArenaMaterial({0.24f, 0.30f, 0.46f, 0.42f},
                                                false, 0.00f, 0.34f),
                              128, 13.1f, 12.9f);
        gSharedBattleModels.chargeWeakPointModelId =
            model->CreatePlane(0, MakeArenaMaterial({1.0f, 0.96f, 0.78f, 0.92f},
                                                    false, 0.02f, 0.20f));
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
    chargeWeakPointModelId_ = gSharedBattleModels.chargeWeakPointModelId;
    sparkParticles_.Initialize(dx, ctx_->rendering.srv, texture,
                               particleTextureId_, 2048);
    sparkParticles_.SetEmission(1, 1000.0f);
    sparkParticles_.SetEmitterRadius(0.08f);
    explosionParticles_.Initialize(dx, ctx_->rendering.srv, texture,
                                   particleTextureId_, 2048);
    explosionParticles_.SetEmission(1, 1000.0f);
    explosionParticles_.SetEmitterRadius(0.25f);
    smokeParticles_.Initialize(dx, ctx_->rendering.srv, texture,
                               particleTextureId_, 1024);
    smokeParticles_.SetEmission(1, 1000.0f);
    smokeParticles_.SetEmitterRadius(0.40f);

    swordFlashParticles_.Initialize(dx, ctx_->rendering.srv, texture,
                                    particleTextureId_, 256);
    swordFlashParticles_.SetEmission(1, 1000.0f);
    swordFlashParticles_.SetEmitterRadius(0.06f);
    swordTrailRenderer_.Initialize(dx);
    swordTrailRenderer_.Reset();
    swordSlashArcRenderer_.Initialize(dx);
    swordSlashArcRenderer_.Reset();

    player_.Initialize(playerModel, swordModel);
    player_.SetInputCalibration(inputCalibration_);
    playerModelId_ = playerModel;
    swordModelId_ = swordModel;
    enemy_.SetDifficulty(combatDifficulty_);
    enemy_.Initialize(enemyModel);
    enemyModelId_ = enemyModel;
    if (ctx_->systems.sound != nullptr) {
        slashSoundId_ = ctx_->systems.sound->Load(
            L"app/resources/audio/sfx/slash_hero.wav");
        enemyReleaseSoundId_ = ctx_->systems.sound->Load(
            L"app/resources/audio/sfx/enemy_release_snap.wav");
        hitSoundId_ = ctx_->systems.sound->Load(
            L"app/resources/audio/sfx/hit_impact.wav");
        counterSoundId_ = ctx_->systems.sound->Load(
            L"app/resources/audio/sfx/counter_burst.wav");
        damageSoundId_ = ctx_->systems.sound->Load(
            L"app/resources/audio/sfx/damage_heavy.wav");
        explosionSoundId_ = ctx_->systems.sound->Load(
            L"app/resources/audio/sfx/explosion_boss.wav");
        soundsLoaded_ = true;
    }
    StartBattleBgm();
    cameraYaw_ = 0.0f;
    cameraPitch_ = 0.0f;
    isLockOn_ = true;
    currentFovDeg_ = normalFovDeg_;
    targetFovDeg_ = normalFovDeg_;

    const DirectX::XMFLOAT3 &playerPos = player_.GetTransform().position;
    const DirectX::XMFLOAT3 &enemyPos = enemy_.GetTransform().position;
    lockOnOrbitCameraPos_ = {playerPos.x, playerPos.y + lockOnOrbitHeight_,
                             playerPos.z - lockOnOrbitRadius_};
    lockOnLookAt_ =
        playerViewCamera_
            ? DirectX::XMFLOAT3{enemyPos.x,
                                enemyPos.y + playerViewLockOnLookHeight_,
                                enemyPos.z}
            : DirectX::XMFLOAT3{playerPos.x * lockOnLookPlayerWeight_ +
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
    backgroundBuildTimer_ = 0.0f;
    tutorialTimer_ = 0.0f;
    tutorialEntryFadeTimer_ = 0.0f;
    tutorialExitFadeTimer_ = 0.0f;
    tutorialAttackDelay_ = 1.8f;
    tutorialSuccessTimer_ = 0.0f;
    tutorialMissTimer_ = 0.0f;
    tutorialExcellentTimer_ = 0.0f;
    tutorialRedWaitTimer_ = 0.0f;
    tutorialGreenCutTimer_ = 0.0f;
    tutorialStep_ = 0;
    tutorialAttackIndex_ = 0;
    tutorialAttackInProgress_ = false;
    tutorialCounterSuccess_ = false;
    tutorialExitRequested_ = false;
    tutorialExitToSelect_ = false;
    victorySequenceActive_ = false;
    victorySequenceTimer_ = 0.0f;
    victoryClearTime_ = 0.0f;
    defeatSequenceActive_ = false;
    defeatSequenceTimer_ = 0.0f;
    defeatImpactEmitted_ = false;
    player_.SetDefeatPoseRatio(0.0f);
    counterCinematicActive_ = false;
    counterCinematicTimer_ = 0.0f;
    enemyAnimationFrozen_ = false;
    enemyRedPunishUncounterable_ = false;
    previousCombatSlashStates_.fill(false);
    bladeClashActive_ = false;
    bladeClashTimer_ = 0.0f;
    bladeClashGauge_ = 0.0f;
    bladeClashPreviousSlashStates_.fill(false);
    bladeClashCameraPush_ = 0.0f;
    bladeClashImpactPulse_ = 0.0f;
    bladeClashEnemySurgeTimer_ = 0.0f;
    bladeClashChainTimer_ = 0.0f;
    bladeClashSlashChain_ = 0;
    bladeClashFinishActive_ = false;
    bladeClashFinishPlayerWon_ = false;
    bladeClashFinishImpactEmitted_ = false;
    bladeClashFinishSkidEmitted_ = false;
    bladeClashFinishGuardBreakEmitted_ = false;
    bladeClashFinishWallImpactEmitted_ = false;
    bladeClashFinishPendingEnemyTransition_ = false;
    bladeClashFinishTimer_ = 0.0f;
    bladeClashFinishCenter_ = {0.0f, 0.0f, 0.0f};
    bladeClashFinishPlayerStart_ = {0.0f, 0.0f, 0.0f};
    bladeClashFinishPlayerEnd_ = {0.0f, 0.0f, 0.0f};
    bladeClashFinishEnemyStart_ = {0.0f, 0.0f, 0.0f};
    previousSwordSoundStates_.fill(false);
    enemyCueParticleTimer_ = 0.0f;
    handTrackingStartRequested_ = false;
    paused_ = false;
    pauseMenuIndex_ = 0;
    player_.SetCameraSwordSlashSuppressed(false);
    if (inputCalibration_.controlType == InputControlType::Hand) {
        if (AppSceneServices::HasHandTrackingStart()) {
            handTrackingStartRequested_ =
                AppSceneServices::RequestHandTrackingStart();
        }
        if (handTrackingStartRequested_) {
            cameraPreviewReceiver_.Initialize(ctx_->rendering.texture,
                                              kHandCameraPreviewPort);
        }
    }
    hud_.Initialize(*ctx_);
    if (tutorialMode_) {
        LoadTutorialImages();
        tutorialEntryFadeTimer_ = -kTutorialEntryBlackHold;
        enemy_.SetTutorialPosition({0.0f, 0.0f, 3.10f});
        enemy_.FaceTargetImmediately(player_.GetTransform().position);
        enemy_.ResetTutorialState();
        battleIntroActive_ = false;
        backgroundBuildTimer_ = 1.2f;
        player_.LockPosition({0.0f, 0.0f, 0.0f});
        UpdateBattleCamera();
        camera_.UpdateMatrices();
        UpdateSceneLighting();
    } else if (!titleDemoMode_ && !backgroundOnlyMode_ && !readyPreviewMode_) {
        LoadPauseMenuImages();
    }
    enemy_.FaceTargetImmediately(player_.GetTransform().position);
    ApplyEnemyIntroDissolve(0.0f);
    if (backgroundOnlyMode_ || readyPreviewMode_) {
        battleIntroActive_ = false;
        backgroundBuildTimer_ = 1.2f;
        if (readyPreviewMode_) {
            player_.UpdateDemo(0.0f, {0.0f, 0.0f, 4.0f});
            player_.LockPosition({0.0f, 0.0f, 0.0f});
            ApplyEnemyIntroDissolve(1.0f);
            UpdateReadyPreviewEnemyAnimation();
            ApplyEnemyProceduralAnimation();
            UpdateReadyPreviewCamera(0.0f);
        } else {
            UpdateBackgroundCamera(0.0f);
        }
        UpdateSceneLighting();
    }
}

void GameScene::Update() {
    Input *input = titleDemoMode_ ? nullptr : ctx_->systems.input;
    const float baseDeltaTime = ctx_->frame.deltaTime;
    if (readyPreviewMode_) {
        sceneLightTime_ += baseDeltaTime;
        backgroundBuildTimer_ = 1.2f;
        player_.UpdateDemo(0.0f, {0.0f, 0.0f, 4.0f});
        player_.LockPosition({0.0f, 0.0f, 0.0f});
        UpdateReadyPreviewEnemyAnimation();
        ApplyEnemyProceduralAnimation();
        UpdateSceneLighting();
        UpdateReadyPreviewCamera(baseDeltaTime);
        EmitReadyPreviewHeatParticles(baseDeltaTime);
        sparkParticles_.Update(baseDeltaTime);
        explosionParticles_.Update(baseDeltaTime);
        smokeParticles_.Update(baseDeltaTime);
        return;
    }
    if (backgroundOnlyMode_) {
        sceneLightTime_ += baseDeltaTime;
        backgroundBuildTimer_ =
            (std::min)(backgroundBuildTimer_ + baseDeltaTime, 1.2f);
        UpdateSceneLighting();
        UpdateBackgroundCamera(baseDeltaTime);
        return;
    }
    if (tutorialMode_) {
        UpdateTutorial(baseDeltaTime);
        return;
    }
    if (paused_) {
        UpdatePauseMenu(input);
        return;
    }
    if (battleIntroActive_) {
        if (input != nullptr &&
            (input->IsKeyTrigger(DIK_SPACE) ||
             input->IsKeyTrigger(DIK_ESCAPE))) {
            FinishBattleIntro();
            return;
        }
        UpdateBattleIntro(baseDeltaTime);
        return;
    }
    if (input != nullptr && input->IsKeyTrigger(DIK_ESCAPE)) {
        OpenPauseMenu();
        return;
    }
    if (victorySequenceActive_) {
        sceneLightTime_ += baseDeltaTime;
        combatFeedback_.Update(baseDeltaTime, sceneLightTime_);
        hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(), enemy_.GetMaxHP());
        UpdateVictorySequence(baseDeltaTime);
        const float victoryPoseRatio =
            std::clamp((victorySequenceTimer_ - 0.78f) / 2.35f, 0.0f, 1.0f);
        enemy_.ApplyVictoryDefeatPose(victoryPoseRatio, victoryEnemyStartPos_,
                                      player_.GetTransform().position);
        UpdateBattleCamera();
        camera_.UpdateMatrices();
        ctx_->rendering.model->UpdateAnimation(playerModelId_,
                                               baseDeltaTime * 0.08f);
        ctx_->rendering.model->UpdateAnimation(enemyModelId_,
                                               baseDeltaTime * 0.002f);
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
        ctx_->rendering.model->UpdateAnimation(playerModelId_,
                                               baseDeltaTime * 0.035f);
        ctx_->rendering.model->UpdateAnimation(enemyModelId_,
                                               baseDeltaTime * 0.28f);
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
    if (input != nullptr && input->IsKeyTrigger(kDebugBladeClashKey)) {
        TriggerDebugBladeClash();
    }
    if (battleIntroRevealEmitted_) {
        ApplyReleasedClearColor(ctx_->rendering.dxCommon);
    } else {
        ctx_->rendering.dxCommon->ResetClearColor();
    }
    phaseTransitionWasActive_ = false;
    phaseTransitionReleaseEmitted_ = false;

    combatFeedback_.Update(baseDeltaTime, sceneLightTime_);
    if (bladeClashFinishActive_) {
        sceneLightTime_ += baseDeltaTime;
        hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(), enemy_.GetMaxHP());
        UpdateBladeClashFinish(baseDeltaTime);
        UpdateCamera(input);
        const float playerAnimScale =
            bladeClashFinishPlayerWon_ ? 0.08f : 0.04f;
        ctx_->rendering.model->UpdateAnimation(playerModelId_,
                                               baseDeltaTime * playerAnimScale);
        if (bladeClashFinishPlayerWon_) {
            UpdateBladeClashEnemyAnimation(baseDeltaTime);
        } else {
            ctx_->rendering.model->UpdateAnimation(enemyModelId_,
                                                   baseDeltaTime * 0.08f);
        }
        UpdateSwordVfx(baseDeltaTime);
        UpdateSceneLighting();
        ApplyEnemyProceduralAnimation();
        UpdateBattleCamera();
        camera_.UpdateMatrices();
        sparkParticles_.Update(baseDeltaTime);
        explosionParticles_.Update(baseDeltaTime);
        smokeParticles_.Update(baseDeltaTime);
        swordFlashParticles_.Update(baseDeltaTime);
        return;
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
        !enemy_.IsPhaseTransitionActive() && !counterCinematicActive_ &&
        !bladeClashActive_) {
        enemyDeltaTime *= 1.08f;
    }
    UpdateCamera(input);

    ctx_->rendering.model->UpdateAnimation(playerModelId_, playerDeltaTime);

    if (titleDemoMode_) {
        player_.UpdateDemo(playerDeltaTime, enemy_.GetTransform().position);
    } else {
        const bool lockPlayerForFarWarpSlash =
            enemy_.ShouldLockPlayerForFarWarpSlash();
        player_.Update(input, playerDeltaTime, enemy_.GetTransform().position,
                       cameraYaw_, baseDeltaTime, false,
                       lockPlayerForFarWarpSlash);
    }
    UpdateSwordVfx(baseDeltaTime);
    UpdateHandCameraPreview(baseDeltaTime);
    if (soundsLoaded_ && ctx_->systems.sound != nullptr) {
        const auto slashStates = player_.GetSwordSlashStates();
        for (size_t i = 0; i < slashStates.size(); ++i) {
            if (slashStates[i] && !previousSwordSoundStates_[i]) {
                ctx_->systems.sound->Play(slashSoundId_, kSlashSoundVolume);
            }
        }
        previousSwordSoundStates_ = slashStates;
    }
    hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(), enemy_.GetMaxHP());
    sceneLightTime_ += baseDeltaTime;
    battleElapsedTime_ += baseDeltaTime;

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
             currentEnemyActionKind == ActionKind::BladeClash) &&
            currentEnemyActionStep == ActionStep::Active;
        if (isEnemyAttackRelease) {
            if (soundsLoaded_ && ctx_->systems.sound != nullptr) {
                ctx_->systems.sound->Play(enemyReleaseSoundId_,
                                          kEnemyReleaseSoundVolume);
            }
        }
    }
    UpdateSceneLighting();

    SyncEnemyAnimation();
    SetEnemyAnimationFrozen(counterCinematicActive_);
    if (!enemyAnimationFrozen_) {
        float enemyAnimationDeltaTime = enemyDeltaTime;
        const ActionKind enemyActionKind = enemy_.GetActionKind();
        const ActionStep enemyActionStep = enemy_.GetActionStep();
        if (bladeClashActive_) {
            UpdateBladeClashEnemyAnimation(baseDeltaTime);
            enemyAnimationDeltaTime = 0.0f;
        } else if (enemyActionKind == ActionKind::Smash ||
            enemyActionKind == ActionKind::Sweep ||
            enemyActionKind == ActionKind::BladeClash) {
            if (IsChargeStanceSettled(enemyActionKind, enemyActionStep,
                                      enemy_.GetActionTimerForPresentation())) {
                enemyAnimationDeltaTime *= 0.035f;
            } else if (enemyActionStep == ActionStep::Active) {
                enemyAnimationDeltaTime *= 4.6f;
            } else if (enemyActionStep == ActionStep::Recovery) {
                enemyAnimationDeltaTime *= 0.72f;
            }
        }
        ctx_->rendering.model->UpdateAnimation(enemyModelId_,
                                               enemyAnimationDeltaTime);
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
    EmitEnemyCueParticles(baseDeltaTime);
    sparkParticles_.Update(baseDeltaTime);
    explosionParticles_.Update(baseDeltaTime);
    smokeParticles_.Update(baseDeltaTime);
    swordFlashParticles_.Update(baseDeltaTime);
}

void GameScene::TriggerDebugBladeClash() {
    if (bladeClashActive_ || bladeClashFinishActive_ || enemy_.GetHP() <= 0.0f ||
        player_.GetHP() <= 0.0f) {
        return;
    }

    counterCinematicActive_ = false;
    counterCinematicTimer_ = 0.0f;
    enemyRedPunishUncounterable_ = false;
    playerHitCooldown_ = 0.0f;
    enemyHitCooldown_ = 0.0f;

    enemy_.BeginDebugBladeClash(player_.GetTransform().position);
    SyncEnemyAnimation();
    EmitEnemyActionParticles(ActionKind::BladeClash, ActionStep::Active);
    if (soundsLoaded_ && ctx_->systems.sound != nullptr) {
        ctx_->systems.sound->Play(enemyReleaseSoundId_,
                                  kEnemyReleaseSoundVolume);
    }
    BeginBladeClash(0);
}

bool GameScene::IsTutorialOperationStepComplete() const {
    const auto swords = player_.GetSwords();
    const auto slashStates = player_.GetSwordSlashStates();
    auto hasSlash = [&](size_t index) {
        return index < slashStates.size() && slashStates[index] &&
               index < swords.size() && swords[index] != nullptr;
    };

    if (tutorialStep_ == kTutorialStepLeftRight) {
        if (!hasSlash(0)) {
            return false;
        }
        const XMFLOAT2 &dir = swords[0]->GetSlashDirection();
        return dir.x > 0.45f;
    }

    if (tutorialStep_ == kTutorialStepRightSword) {
        if (!hasSlash(1)) {
            return false;
        }
        const XMFLOAT2 &dir = swords[1]->GetSlashDirection();
        return dir.x * dir.x + dir.y * dir.y > 0.20f;
    }

    return false;
}

void GameScene::AdvanceTutorialOperationStep() {
    ++tutorialStep_;
    tutorialSuccessTimer_ = 1.15f;
    tutorialMissTimer_ = 0.0f;
    tutorialExcellentTimer_ = 0.0f;
    tutorialRedWaitTimer_ = 0.0f;
    tutorialGreenCutTimer_ = 0.0f;
    tutorialAttackDelay_ =
        tutorialStep_ >= kTutorialStepRedSmash ? 2.0f : 0.0f;
    tutorialAttackInProgress_ = false;
    tutorialCounterSuccess_ = false;
    enemy_.ResetTutorialState();
    enemy_.SetTutorialPosition({0.0f, 0.0f, 3.10f});
    enemy_.FaceTargetImmediately(player_.GetTransform().position);
}

void GameScene::UpdateTutorial(float deltaTime) {
    Input *input = ctx_->systems.input;
    if (tutorialExitRequested_) {
        tutorialExitFadeTimer_ =
            (std::min)(tutorialExitFadeTimer_ + deltaTime,
                       kTutorialExitFadeDuration);
        if (tutorialExitFadeTimer_ >= kTutorialExitFadeDuration) {
            if (tutorialExitToSelect_) {
                sceneManager_->ChangeScene(
                    std::make_unique<TutorialSelectScene>());
            } else {
                sceneManager_->ChangeScene(
                    std::make_unique<WeaponSelectScene>());
            }
        }
        return;
    }

    if (input != nullptr) {
        if (tutorialStep_ >= kTutorialStepPractice &&
            input->IsKeyTrigger(DIK_SPACE)) {
            tutorialExitRequested_ = true;
            tutorialExitToSelect_ = false;
            tutorialExitFadeTimer_ = 0.0f;
            return;
        }
        if (input->IsKeyTrigger(DIK_ESCAPE)) {
            tutorialExitRequested_ = true;
            tutorialExitToSelect_ = true;
            tutorialExitFadeTimer_ = 0.0f;
            return;
        }
    }

    tutorialTimer_ += deltaTime;
    tutorialEntryFadeTimer_ =
        (std::min)(tutorialEntryFadeTimer_ + deltaTime,
                   kTutorialEntryFadeDuration);
    sceneLightTime_ += deltaTime;
    backgroundBuildTimer_ = 1.2f;
    combatFeedback_.Update(deltaTime, sceneLightTime_);
    UpdateBattlePostProcessState(deltaTime);

    UpdateCamera(input);
    player_.Update(input, deltaTime, enemy_.GetTransform().position,
                   cameraYaw_, deltaTime, false);
    player_.LockPosition({0.0f, 0.0f, 0.0f});
    player_.SetCameraSwordSlashSuppressed(false);
    UpdateSwordVfx(deltaTime);
    UpdateHandCameraPreview(deltaTime);

    tutorialSuccessTimer_ =
        (std::max)(0.0f, tutorialSuccessTimer_ - deltaTime);
    tutorialMissTimer_ = (std::max)(0.0f, tutorialMissTimer_ - deltaTime);
    tutorialExcellentTimer_ =
        (std::max)(0.0f, tutorialExcellentTimer_ - deltaTime);

    if (tutorialStep_ < kTutorialStepRedSmash) {
        if (tutorialSuccessTimer_ <= 0.0f &&
            IsTutorialOperationStepComplete()) {
            AdvanceTutorialOperationStep();
        }

        enemy_.ResetTutorialState();
        enemy_.SetTutorialPosition({0.0f, 0.0f, 3.10f});
        enemy_.FaceTargetImmediately(player_.GetTransform().position);
        ctx_->rendering.model->UpdateAnimation(playerModelId_, deltaTime);
        ctx_->rendering.model->UpdateAnimation(enemyModelId_, deltaTime);
        ApplyEnemyProceduralAnimation();
        UpdateSceneLighting();
        UpdateBattleCamera();
        camera_.UpdateMatrices();
        sparkParticles_.Update(deltaTime);
        explosionParticles_.Update(deltaTime);
        smokeParticles_.Update(deltaTime);
        swordFlashParticles_.Update(deltaTime);
        return;
    }

    auto tutorialAttackKind = [&]() {
        if (tutorialStep_ == kTutorialStepRedSmash ||
            tutorialStep_ == kTutorialStepGreenSmash) {
            return ActionKind::Smash;
        }
        if (tutorialStep_ == kTutorialStepRedSweep ||
            tutorialStep_ == kTutorialStepGreenSweep) {
            return ActionKind::Sweep;
        }
        return (tutorialAttackIndex_ % 2 == 0) ? ActionKind::Smash
                                               : ActionKind::Sweep;
    };
    auto isRedWaitStep = [&]() {
        return tutorialStep_ == kTutorialStepRedSmash ||
               tutorialStep_ == kTutorialStepRedSweep;
    };
    auto isGreenCutStep = [&]() {
        return tutorialStep_ == kTutorialStepGreenSmash ||
               tutorialStep_ == kTutorialStepGreenSweep;
    };

    if (!tutorialAttackInProgress_ && tutorialExcellentTimer_ <= 0.0f) {
        tutorialAttackDelay_ -= deltaTime;
        if (tutorialAttackDelay_ <= 0.0f) {
            enemy_.ResetTutorialState();
            enemy_.SetTutorialPosition({0.0f, 0.0f, 3.10f});
            enemy_.FaceTargetImmediately(player_.GetTransform().position);
            const ActionKind attackKind = tutorialAttackKind();
            enemy_.BeginTutorialAttack(attackKind);
            EmitEnemyActionParticles(attackKind, ActionStep::Charge);
            tutorialAttackInProgress_ = true;
            tutorialCounterSuccess_ = false;
            tutorialRedWaitTimer_ = isRedWaitStep() ? 3.0f : 0.0f;
            tutorialGreenCutTimer_ = 0.0f;
            counterCinematicActive_ = false;
            counterCinematicTimer_ = 0.0f;
            SetEnemyAnimationFrozen(false);
        }
    }

    const auto swordSlashStatesBeforeCombat = player_.GetSwordSlashStates();
    bool tutorialSlashStarted = false;
    for (size_t i = 0; i < swordSlashStatesBeforeCombat.size(); ++i) {
        if (swordSlashStatesBeforeCombat[i] &&
            !previousCombatSlashStates_[i]) {
            tutorialSlashStarted = true;
            break;
        }
    }

    if (tutorialAttackInProgress_ && isRedWaitStep() &&
        tutorialSlashStarted) {
        tutorialMissTimer_ = 0.0f;
        tutorialExcellentTimer_ = 0.0f;
        tutorialRedWaitTimer_ = 0.0f;
        tutorialGreenCutTimer_ = 0.0f;
        tutorialAttackDelay_ = 1.15f;
        tutorialAttackInProgress_ = false;
        tutorialCounterSuccess_ = false;
        enemyRedPunishUncounterable_ = false;
        enemy_.ResetTutorialState();
        enemy_.SetTutorialPosition({0.0f, 0.0f, 3.10f});
        enemy_.FaceTargetImmediately(player_.GetTransform().position);
        previousCombatSlashStates_ = swordSlashStatesBeforeCombat;
    }

    const ActionKind previousEnemyActionKind = enemy_.GetActionKind();
    const ActionStep previousEnemyActionStep = enemy_.GetActionStep();
    const bool holdRedCue =
        isRedWaitStep() && tutorialRedWaitTimer_ > 0.0f &&
        (enemy_.GetReleaseAnticipationRatio() > 0.0f ||
         enemy_.GetActionStep() == ActionStep::Active);
    const bool holdGreenCue =
        isGreenCutStep() && tutorialGreenCutTimer_ > 0.0f &&
        (enemy_.GetReleaseAnticipationRatio() > 0.0f ||
         enemy_.GetActionStep() == ActionStep::Active);
    const float enemyTutorialDeltaTime =
        tutorialExcellentTimer_ > 0.0f
            ? 0.0f
            : holdGreenCue
            ? 0.0f
            : isRedWaitStep()
            ? (holdRedCue ? 0.0f : deltaTime * 0.65f)
            : deltaTime;
    enemy_.UpdateTutorial(BuildPlayerCombatObservation(),
                          enemyTutorialDeltaTime);
    const ActionKind currentEnemyActionKind = enemy_.GetActionKind();
    const ActionStep currentEnemyActionStep = enemy_.GetActionStep();
    const bool greenCueVisible =
        isGreenCutStep() && !enemyRedPunishUncounterable_ &&
        (enemy_.GetReleaseAnticipationRatio() > 0.0f ||
         currentEnemyActionStep == ActionStep::Active);
    if (tutorialAttackInProgress_ && greenCueVisible &&
        tutorialGreenCutTimer_ <= 0.0f) {
        tutorialGreenCutTimer_ = 3.0f;
    }
    if (tutorialAttackInProgress_ && isRedWaitStep()) {
        tutorialRedWaitTimer_ =
            (std::max)(0.0f, tutorialRedWaitTimer_ - deltaTime);
        if (tutorialRedWaitTimer_ <= 0.0f) {
            tutorialExcellentTimer_ = 1.10f;
            if (tutorialStep_ == kTutorialStepRedSmash) {
                tutorialStep_ = kTutorialStepGreenSmash;
            } else if (tutorialStep_ == kTutorialStepRedSweep) {
                tutorialStep_ = kTutorialStepGreenSweep;
            }
            tutorialGreenCutTimer_ = 0.0f;
            tutorialAttackDelay_ = 1.55f;
            tutorialAttackInProgress_ = false;
            tutorialCounterSuccess_ = false;
            enemyRedPunishUncounterable_ = false;
            enemy_.ResetTutorialState();
            enemy_.SetTutorialPosition({0.0f, 0.0f, 3.10f});
            enemy_.FaceTargetImmediately(player_.GetTransform().position);
            previousCombatSlashStates_ = swordSlashStatesBeforeCombat;
        }
    }
    if (currentEnemyActionKind != previousEnemyActionKind ||
        currentEnemyActionStep != previousEnemyActionStep) {
        EmitEnemyActionParticles(currentEnemyActionKind, currentEnemyActionStep);
        if ((currentEnemyActionKind == ActionKind::Smash ||
             currentEnemyActionKind == ActionKind::Sweep) &&
            currentEnemyActionStep == ActionStep::Active &&
            soundsLoaded_ && ctx_->systems.sound != nullptr) {
            ctx_->systems.sound->Play(enemyReleaseSoundId_,
                                      kEnemyReleaseSoundVolume);
        }
    }

    ctx_->rendering.model->UpdateAnimation(playerModelId_, deltaTime);
    SyncEnemyAnimation();
    SetEnemyAnimationFrozen(counterCinematicActive_);
    if (!enemyAnimationFrozen_) {
        float enemyAnimationDeltaTime = deltaTime;
        const ActionKind enemyActionKind = enemy_.GetActionKind();
        const ActionStep enemyActionStep = enemy_.GetActionStep();
        if (enemyActionKind == ActionKind::Smash ||
            enemyActionKind == ActionKind::Sweep ||
            enemyActionKind == ActionKind::BladeClash) {
            if (IsChargeStanceSettled(enemyActionKind, enemyActionStep,
                                      enemy_.GetActionTimerForPresentation())) {
                enemyAnimationDeltaTime *= 0.035f;
            } else if (enemyActionStep == ActionStep::Active) {
                enemyAnimationDeltaTime *= 4.6f;
            } else if (enemyActionStep == ActionStep::Recovery) {
                enemyAnimationDeltaTime *= 0.72f;
            }
        }
        ctx_->rendering.model->UpdateAnimation(enemyModelId_,
                                               enemyAnimationDeltaTime);
    }
    ApplyEnemyProceduralAnimation();
    UpdateSceneLighting();
    UpdateBattleCamera();
    camera_.UpdateMatrices();

    const bool wasCounterActive = counterCinematicActive_;
    if (isRedWaitStep() || tutorialExcellentTimer_ > 0.0f ||
        (isGreenCutStep() && tutorialGreenCutTimer_ <= 0.0f)) {
        previousCombatSlashStates_ = swordSlashStatesBeforeCombat;
    } else {
        UpdateCombat(deltaTime);
    }
    if (!tutorialCounterSuccess_ && !wasCounterActive &&
        counterCinematicActive_) {
        tutorialCounterSuccess_ = true;
        tutorialExcellentTimer_ = isGreenCutStep() ? 1.25f : 0.0f;
        tutorialSuccessTimer_ = isGreenCutStep() ? 0.0f : 1.45f;
        tutorialMissTimer_ = 0.0f;
        tutorialGreenCutTimer_ = 0.0f;
        ++tutorialAttackIndex_;
        if (tutorialStep_ == kTutorialStepGreenSmash) {
            tutorialStep_ = kTutorialStepRedSweep;
        } else if (tutorialStep_ == kTutorialStepGreenSweep) {
            tutorialStep_ = kTutorialStepPractice;
        }
    }

    if (tutorialAttackInProgress_ && isGreenCutStep() &&
        !tutorialCounterSuccess_ && tutorialExcellentTimer_ <= 0.0f &&
        tutorialGreenCutTimer_ > 0.0f) {
        tutorialGreenCutTimer_ =
            (std::max)(0.0f, tutorialGreenCutTimer_ - deltaTime);
        if (tutorialGreenCutTimer_ <= 0.0f) {
            tutorialMissTimer_ = 1.25f;
            tutorialExcellentTimer_ = 0.0f;
            tutorialAttackDelay_ = 1.40f;
            tutorialAttackInProgress_ = false;
            enemyRedPunishUncounterable_ = false;
            enemy_.ResetTutorialState();
            enemy_.SetTutorialPosition({0.0f, 0.0f, 3.10f});
            enemy_.FaceTargetImmediately(player_.GetTransform().position);
            previousCombatSlashStates_ = swordSlashStatesBeforeCombat;
            if (tutorialStep_ == kTutorialStepGreenSmash) {
                tutorialStep_ = kTutorialStepRedSmash;
            } else if (tutorialStep_ == kTutorialStepGreenSweep) {
                tutorialStep_ = kTutorialStepRedSweep;
            }
        }
    }

    if (tutorialAttackInProgress_ &&
        enemy_.GetActionKind() == ActionKind::None) {
        tutorialAttackInProgress_ = false;
        enemy_.ResetTutorialState();
        enemy_.SetTutorialPosition({0.0f, 0.0f, 3.10f});
        enemy_.FaceTargetImmediately(player_.GetTransform().position);
        if (tutorialCounterSuccess_) {
            tutorialAttackDelay_ =
                tutorialStep_ == kTutorialStepPractice ? 1.85f : 2.20f;
        } else {
            tutorialMissTimer_ = 1.25f;
            tutorialAttackDelay_ = 2.15f;
            if (tutorialStep_ == kTutorialStepGreenSmash) {
                tutorialStep_ = kTutorialStepRedSmash;
            } else if (tutorialStep_ == kTutorialStepGreenSweep) {
                tutorialStep_ = kTutorialStepRedSweep;
            }
        }
    }

    if (counterCinematicActive_) {
        counterCinematicTimer_ -= deltaTime;
        if (counterCinematicTimer_ <= 0.0f) {
            counterCinematicTimer_ = 0.0f;
            counterCinematicActive_ = false;
            enemy_.FinishCounterRecoil();
            SetEnemyAnimationFrozen(false);
        }
    }

    EmitEnemyCueParticles(deltaTime);
    sparkParticles_.Update(deltaTime);
    explosionParticles_.Update(deltaTime);
    smokeParticles_.Update(deltaTime);
    swordFlashParticles_.Update(deltaTime);
}

void GameScene::Draw() {
    if (backgroundOnlyMode_) {
        ctx_->rendering.model->PreDraw();
        DrawArena();
        ctx_->rendering.model->PostDraw();
        return;
    }
    if (readyPreviewMode_) {
        ctx_->rendering.model->PrepareSkinning({enemyModelId_});
        GPUParticleSystem::DispatchPendingUpdates(
            {&smokeParticles_, &sparkParticles_, &explosionParticles_});
        ctx_->rendering.model->PreDraw();
        DrawArena();
        enemy_.Draw(ctx_->rendering.model, camera_, 1.28f);
        ctx_->rendering.model->PostDraw();
        GPUParticleSystem::DrawBatch(
            {&smokeParticles_, &sparkParticles_, &explosionParticles_},
            camera_);
        return;
    }

    ctx_->rendering.model->PrepareSkinning({playerModelId_, enemyModelId_});
    GPUParticleSystem::DispatchPendingUpdates(
        {&smokeParticles_, &sparkParticles_, &explosionParticles_,
         &swordFlashParticles_});

    ctx_->rendering.model->PreDraw();
    const bool bladeClashWinFinish =
        bladeClashFinishActive_ && bladeClashFinishPlayerWon_;
    if (bladeClashWinFinish) {
        DrawBladeClashFinishBackdrop();
    } else {
        DrawArena();
    }
    const float playerVisualScale = bladeClashWinFinish ? 0.68f : 1.0f;
    const float enemyVisualScale = bladeClashWinFinish ? 1.32f : 1.0f;
    const float introWorldReveal = BattleIntroWorldRevealProgress();
    if (!battleIntroActive_ || introWorldReveal > 0.02f) {
        player_.Draw(ctx_->rendering.model, camera_,
                     !playerViewCamera_ || bladeClashFinishActive_,
                     bladeClashFinishActive_, playerVisualScale);
    }
    if (!(victorySequenceActive_ && victoryFinalExplosionEmitted_)) {
        enemy_.Draw(ctx_->rendering.model, camera_, enemyVisualScale);
    }
    ctx_->rendering.model->PostDraw();
    swordTrailRenderer_.Draw(camera_);
    swordSlashArcRenderer_.Draw(camera_);

    if (!bladeClashWinFinish) {
        GPUParticleSystem::DrawBatch({&smokeParticles_, &sparkParticles_,
                                      &explosionParticles_,
                                      &swordFlashParticles_},
                                     camera_);
    }
    DrawVictoryFlash();
    DrawDefeatFlash();
    DrawBattleIntroFlash();
    DrawBladeClashFinishFrame();
}

void GameScene::OpenPauseMenu() {
    if (titleDemoMode_ || backgroundOnlyMode_ || readyPreviewMode_) {
        return;
    }
    if (paused_) {
        return;
    }
    if (ctx_ != nullptr && ctx_->rendering.postProcessSystem != nullptr) {
        pauseSavedPostProcess_ = ctx_->rendering.postProcessSystem->GetProfile();
        pausePostProcessSaved_ = true;
        ctx_->rendering.postProcessSystem->SetProfile(PostProcessProfile{});
    }
    paused_ = true;
    pauseMenuIndex_ = 0;
}

void GameScene::ClosePauseMenu() {
    paused_ = false;
    if (pausePostProcessSaved_ && ctx_ != nullptr &&
        ctx_->rendering.postProcessSystem != nullptr) {
        ctx_->rendering.postProcessSystem->SetProfile(pauseSavedPostProcess_);
    }
    pausePostProcessSaved_ = false;
}

void GameScene::UpdatePauseMenu(Input *input) {
    if (input == nullptr) {
        return;
    }

    const bool gamepad = input->IsGamepadConnected();
    const bool moveUp =
        input->IsKeyTrigger(DIK_UP) || input->IsKeyTrigger(DIK_W) ||
        (gamepad && input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_UP));
    const bool moveDown =
        input->IsKeyTrigger(DIK_DOWN) || input->IsKeyTrigger(DIK_S) ||
        (gamepad && input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_DOWN));
    constexpr int kPauseMenuItemCount = 3;
    if (moveUp && !moveDown && pauseMenuIndex_ > 0) {
        --pauseMenuIndex_;
    } else if (moveDown && !moveUp) {
        if (pauseMenuIndex_ < kPauseMenuItemCount - 1) {
            ++pauseMenuIndex_;
        }
    }

    const bool cancel =
        input->IsKeyTrigger(DIK_ESCAPE) ||
        (gamepad && input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_B));
    if (cancel) {
        ClosePauseMenu();
        return;
    }

    const bool confirm =
        input->IsKeyTrigger(DIK_RETURN) || input->IsKeyTrigger(DIK_SPACE) ||
        (gamepad && input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_A));
    if (confirm) {
        ExecutePauseMenuSelection();
    }
}

void GameScene::ExecutePauseMenuSelection() {
    switch (pauseMenuIndex_) {
    case 0:
        ClosePauseMenu();
        break;
    case 1:
        sceneManager_->ChangeScene(
            std::make_unique<GameScene>(inputCalibration_, combatDifficulty_));
        break;
    case 2:
        sceneManager_->ChangeScene(std::make_unique<TitleScene>());
        break;
    default:
        break;
    }
}

void GameScene::LoadPauseMenuImages() {
    if (pauseMenuImagesLoaded_ || ctx_ == nullptr ||
        ctx_->rendering.texture == nullptr) {
        return;
    }

    const std::array<std::wstring, 4> paths{
        L"app/resources/ui/pause/pause_title.png",
        L"app/resources/ui/pause/continue.png",
        L"app/resources/ui/pause/retry.png",
        L"app/resources/ui/pause/title.png",
    };
    for (size_t i = 0; i < paths.size(); ++i) {
        pauseMenuTextureIds_[i] = ctx_->rendering.texture->Load(paths[i]);
        pauseMenuTextureWidths_[i] = static_cast<float>(
            ctx_->rendering.texture->GetWidth(pauseMenuTextureIds_[i]));
        pauseMenuTextureHeights_[i] = static_cast<float>(
            ctx_->rendering.texture->GetHeight(pauseMenuTextureIds_[i]));
    }
    pauseMenuImagesLoaded_ = true;
}

void GameScene::LoadTutorialImages() {
    if (tutorialImagesLoaded_ || ctx_ == nullptr ||
        ctx_->rendering.texture == nullptr) {
        return;
    }

    const bool handTutorial =
        inputCalibration_.controlType == InputControlType::Hand;
    const std::array<std::wstring, 10> paths{
        handTutorial
            ? L"app/resources/ui/tutorial_dynamic/left_right_hand.png"
            : L"app/resources/ui/tutorial_dynamic/left_right_kbm.png",
        handTutorial
            ? L"app/resources/ui/tutorial_dynamic/right_sword_hand.png"
            : L"app/resources/ui/tutorial_dynamic/right_sword_kbm.png",
        L"app/resources/ui/tutorial_dynamic/wait.png",
        L"app/resources/ui/tutorial_dynamic/vertical.png",
        L"app/resources/ui/tutorial_dynamic/horizontal.png",
        L"app/resources/ui/tutorial_dynamic/release.png",
        L"app/resources/ui/tutorial_dynamic/success.png",
        L"app/resources/ui/tutorial_dynamic/miss.png",
        L"app/resources/ui/tutorial_dynamic/complete.png",
        L"app/resources/ui/tutorial_dynamic/excellent.png",
    };
    for (size_t i = 0; i < paths.size(); ++i) {
        tutorialTextureIds_[i] = ctx_->rendering.texture->Load(paths[i]);
        tutorialTextureWidths_[i] = static_cast<float>(
            ctx_->rendering.texture->GetWidth(tutorialTextureIds_[i]));
        tutorialTextureHeights_[i] = static_cast<float>(
            ctx_->rendering.texture->GetHeight(tutorialTextureIds_[i]));
    }
    tutorialImagesLoaded_ = true;
}

void GameScene::DrawPauseMenu() {
    if (!paused_ || ctx_ == nullptr || ctx_->rendering.sprite == nullptr ||
        ctx_->systems.winApp == nullptr) {
        return;
    }

    LoadPauseMenuImages();

    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());
    const float panelW = (std::min)(w * 0.38f, 500.0f);
    const float panelH = 360.0f;
    const float panelX = (w - panelW) * 0.5f;
    const float panelY = (h - panelH) * 0.5f;
    const float itemW = panelW - 116.0f;
    const float itemH = 48.0f;
    const float itemX = panelX + (panelW - itemW) * 0.5f;
    const float itemStartY = panelY + 138.0f;
    const float itemGap = 62.0f;

    ctx_->rendering.sprite->PreDraw();
    DrawPauseRect(0.0f, 0.0f, w, h, {0.0f, 0.0f, 0.0f, 0.58f});
    DrawPauseRect(panelX - 4.0f, panelY - 4.0f, panelW + 8.0f, panelH + 8.0f,
                  {0.90f, 0.72f, 0.32f, 0.38f});
    DrawPauseRect(panelX, panelY, panelW, panelH,
                  {0.018f, 0.020f, 0.024f, 0.92f});
    DrawPauseRect(panelX, panelY, panelW, 4.0f,
                  {1.0f, 0.76f, 0.22f, 0.88f});
    DrawPauseImage(pauseMenuTextureIds_[0], pauseMenuTextureWidths_[0],
                   pauseMenuTextureHeights_[0],
                   w * 0.5f - pauseMenuTextureWidths_[0] * 0.5f,
                   panelY + 44.0f, 1.0f);

    for (int i = 0; i < 3; ++i) {
        const float y = itemStartY + static_cast<float>(i) * itemGap;
        const bool selected = i == pauseMenuIndex_;
        DrawPauseRect(itemX, y, itemW, itemH,
                      selected ? DirectX::XMFLOAT4{0.82f, 0.52f, 0.12f, 0.82f}
                               : DirectX::XMFLOAT4{0.08f, 0.095f, 0.11f,
                                                    0.82f});
        DrawPauseRect(itemX + 4.0f, y + 4.0f, itemW - 8.0f, itemH - 8.0f,
                      selected ? DirectX::XMFLOAT4{0.20f, 0.14f, 0.05f, 0.86f}
                               : DirectX::XMFLOAT4{0.015f, 0.018f, 0.022f,
                                                    0.86f});
        if (selected) {
            DrawPauseRect(itemX + 12.0f, y + 12.0f, 5.0f, itemH - 24.0f,
                          {1.0f, 0.88f, 0.36f, 0.95f});
        }

        const size_t imageIndex = static_cast<size_t>(i + 1);
        const float maxLabelW = itemW * 0.78f;
        const float maxLabelH = itemH * 0.78f;
        const float baseScale =
            (std::min)(maxLabelW /
                           (std::max)(pauseMenuTextureWidths_[imageIndex], 1.0f),
                       maxLabelH /
                           (std::max)(pauseMenuTextureHeights_[imageIndex], 1.0f));
        const float scale = (std::min)(selected ? 1.0f : 0.92f, baseScale);
        const float imageW = pauseMenuTextureWidths_[imageIndex] * scale;
        const float imageH = pauseMenuTextureHeights_[imageIndex] * scale;
        DrawPauseImage(pauseMenuTextureIds_[imageIndex],
                       pauseMenuTextureWidths_[imageIndex],
                       pauseMenuTextureHeights_[imageIndex],
                       itemX + (itemW - imageW) * 0.5f,
                       y + (itemH - imageH) * 0.5f, scale,
                       selected ? 1.0f : 0.82f);
    }
    ctx_->rendering.sprite->PostDraw();
}

void GameScene::DrawPauseRect(float x, float y, float w, float h,
                              const DirectX::XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.textureId = 0;
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void GameScene::DrawBladeClashFinishFrame() {
    if (!bladeClashFinishActive_ || ctx_ == nullptr ||
        ctx_->rendering.sprite == nullptr || ctx_->systems.winApp == nullptr) {
        return;
    }

    const float screenW = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float screenH = static_cast<float>(ctx_->systems.winApp->GetHeight());
    const float ratio =
        bladeClashFinishDuration_ > 0.0001f
            ? std::clamp(bladeClashFinishTimer_ / bladeClashFinishDuration_,
                         0.0f, 1.0f)
            : 1.0f;
    const float fadeIn =
        std::clamp(bladeClashFinishTimer_ / 0.16f, 0.0f, 1.0f);
    const float fadeOut =
        1.0f - std::clamp((ratio - 0.90f) / 0.10f, 0.0f, 1.0f);
    const float alpha = fadeIn * fadeOut;
    if (alpha <= 0.01f) {
        return;
    }

    auto pulse = [&](float center, float width, float peak) {
        return (std::max)(
            0.0f,
            (1.0f - std::fabs(bladeClashFinishTimer_ - center) / width) *
                peak);
    };
    const float flash =
        bladeClashFinishPlayerWon_
            ? std::clamp((std::max)(pulse(0.28f, 0.16f, 1.0f),
                                    pulse(0.56f, 0.20f, 0.72f)),
                         0.0f, 1.0f)
            : std::clamp((std::max)(pulse(0.10f, 0.24f, 0.72f),
                                    pulse(0.58f, 0.32f, 0.42f)),
                         0.0f, 1.0f);
    const float barH = std::clamp(screenH * 0.085f, 52.0f, 78.0f);
    const XMFLOAT4 barColor =
        bladeClashFinishPlayerWon_
            ? XMFLOAT4{1.0f, 0.94f, 0.62f, 0.18f * alpha}
            : XMFLOAT4{1.0f, 0.08f, 0.02f, 0.16f * alpha};

    ctx_->rendering.sprite->PreDraw();
    DrawPauseRect(0.0f, 0.0f, screenW, barH, barColor);
    DrawPauseRect(0.0f, screenH - barH, screenW, barH, barColor);
    if (flash > 0.01f) {
        DrawPauseRect(0.0f, 0.0f, screenW, screenH,
                      bladeClashFinishPlayerWon_
                          ? XMFLOAT4{1.0f, 0.98f, 0.80f, flash * 0.18f}
                          : XMFLOAT4{1.0f, 0.04f, 0.02f, flash * 0.16f});
    }
    ctx_->rendering.sprite->PostDraw();
}

void GameScene::DrawBladeClashOverlay() {
    if (!bladeClashActive_ || ctx_ == nullptr ||
        ctx_->rendering.sprite == nullptr) {
        return;
    }

    const float screenWidth =
        ctx_->systems.winApp != nullptr
            ? static_cast<float>(ctx_->systems.winApp->GetWidth())
            : 1280.0f;
    const float screenHeight =
        ctx_->systems.winApp != nullptr
            ? static_cast<float>(ctx_->systems.winApp->GetHeight())
            : 720.0f;
    const float barW = (std::min)(screenWidth * 0.46f, 520.0f);
    const float barH = 16.0f;
    const float x = (screenWidth - barW) * 0.5f;
    const float y = screenHeight * 0.70f;
    const float normalized = std::clamp((bladeClashGauge_ + 1.0f) * 0.5f,
                                        0.0f, 1.0f);
    const float centerX = x + barW * 0.5f;
    const float markerX = x + barW * normalized;
    const float timeRate = bladeClashDuration_ > 0.0001f
                               ? std::clamp(bladeClashTimer_ /
                                                bladeClashDuration_,
                                            0.0f, 1.0f)
                               : 0.0f;

    ctx_->rendering.sprite->PreDraw();
    const float impact = std::clamp(bladeClashImpactPulse_, 0.0f, 1.0f);
    DrawPauseRect(x - 10.0f - impact * 5.0f, y - 12.0f - impact * 3.0f,
                  barW + 20.0f + impact * 10.0f,
                  barH + 24.0f + impact * 6.0f,
                  {0.02f, 0.022f, 0.028f, 0.72f});
    DrawPauseRect(x, y, barW, barH, {0.08f, 0.075f, 0.065f, 0.92f});
    DrawPauseRect(x + 3.0f, y + 3.0f, barW - 6.0f, barH - 6.0f,
                  {0.22f, 0.10f, 0.08f, 0.86f});
    DrawPauseRect(centerX, y - 4.0f, 2.0f, barH + 8.0f,
                  {0.95f, 0.90f, 0.64f, 0.64f});
    DrawPauseRect(x + 3.0f, y + 3.0f,
                  (barW - 6.0f) * normalized, barH - 6.0f,
                  {0.95f, 0.62f, 0.16f, 0.94f});
    DrawPauseRect(markerX - 5.0f, y - 7.0f, 10.0f, barH + 14.0f,
                  {1.0f, 0.94f, 0.62f, 0.96f});
    DrawPauseRect(x, y + barH + 8.0f, barW * timeRate, 4.0f,
                  {0.72f, 0.88f, 1.0f, 0.64f});
    ctx_->rendering.sprite->PostDraw();
}

void GameScene::DrawPauseImage(uint32_t textureId, float textureWidth,
                               float textureHeight, float x, float y,
                               float scale, float alpha) {
    Sprite sprite{};
    sprite.textureId = textureId;
    sprite.position = {x, y};
    sprite.size = {textureWidth * scale, textureHeight * scale};
    sprite.color = {1.0f, 1.0f, 1.0f, alpha};
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void GameScene::DrawTutorialOverlay() {
    if (!tutorialMode_ || ctx_ == nullptr || ctx_->rendering.sprite == nullptr ||
        ctx_->systems.winApp == nullptr) {
        return;
    }

    LoadTutorialImages();

    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());
    const ActionKind actionKind = enemy_.GetActionKind();
    const ActionStep actionStep = enemy_.GetActionStep();
    const float releaseRatio =
        (actionStep == ActionStep::Active)
            ? 1.0f
            : std::clamp(enemy_.GetReleaseAnticipationRatio(), 0.0f, 1.0f);

    int messageIndex = kTutorialTextWait;
    if (tutorialSuccessTimer_ > 0.0f) {
        messageIndex = kTutorialTextSuccess;
    } else if (tutorialMissTimer_ > 0.0f) {
        messageIndex = kTutorialTextMiss;
    } else if (tutorialStep_ == kTutorialStepLeftRight) {
        messageIndex = kTutorialTextLeftRight;
    } else if (tutorialStep_ == kTutorialStepRightSword) {
        messageIndex = kTutorialTextRightSword;
    } else if (tutorialStep_ == kTutorialStepRedSmash) {
        messageIndex = kTutorialTextVertical;
    } else if (tutorialStep_ == kTutorialStepRedSweep) {
        messageIndex = kTutorialTextHorizontal;
    } else if (tutorialStep_ == kTutorialStepGreenSmash ||
               tutorialStep_ == kTutorialStepGreenSweep) {
        messageIndex = kTutorialTextRelease;
    } else if (tutorialStep_ >= kTutorialStepPractice) {
        messageIndex = kTutorialTextComplete;
    } else if (releaseRatio > 0.0f || actionStep == ActionStep::Active) {
        messageIndex = kTutorialTextRelease;
    } else if (actionKind == ActionKind::Smash) {
        messageIndex = kTutorialTextVertical;
    } else if (actionKind == ActionKind::Sweep) {
        messageIndex = kTutorialTextHorizontal;
    }

    ctx_->rendering.sprite->PreDraw();
    DrawPauseRect(0.0f, 0.0f, w, h * 0.16f, {0.0f, 0.0f, 0.0f, 0.34f});
    DrawPauseRect(0.0f, h * 0.76f, w, h * 0.24f,
                  {0.0f, 0.0f, 0.0f, 0.48f});

    const float panelW = std::clamp(w * 0.62f, 680.0f, 1060.0f);
    const float panelH = 112.0f;
    const float panelX = (w - panelW) * 0.5f;
    const float panelY = h - 164.0f;
    const bool releaseNow = messageIndex == kTutorialTextRelease;
    DrawPauseRect(panelX + 8.0f, panelY + 10.0f, panelW, panelH,
                  {0.0f, 0.0f, 0.0f, 0.28f});
    DrawPauseRect(panelX, panelY, panelW, panelH,
                  releaseNow ? DirectX::XMFLOAT4{0.02f, 0.18f, 0.10f, 0.86f}
                             : DirectX::XMFLOAT4{0.014f, 0.030f, 0.032f,
                                                  0.82f});
    DrawPauseRect(panelX, panelY, panelW * (releaseNow ? 1.0f : 0.42f), 5.0f,
                  releaseNow ? DirectX::XMFLOAT4{0.14f, 1.0f, 0.28f, 0.96f}
                             : DirectX::XMFLOAT4{0.02f, 0.95f, 0.84f, 0.78f});

    const float textW = tutorialTextureWidths_[messageIndex];
    const float textH = tutorialTextureHeights_[messageIndex];
    const float scale =
        (std::min)(1.0f,
                   (panelW - 92.0f) / (std::max)(textW, 1.0f));
    DrawPauseImage(tutorialTextureIds_[messageIndex], textW, textH,
                   panelX + (panelW - textW * scale) * 0.5f,
                   panelY + (panelH - textH * scale) * 0.5f, scale, 1.0f);

    if (tutorialExcellentTimer_ > 0.0f) {
        const float excellentW =
            tutorialTextureWidths_[kTutorialTextExcellent];
        const float excellentH =
            tutorialTextureHeights_[kTutorialTextExcellent];
        const float excellentScale =
            (std::min)(1.20f, (w * 0.48f) / (std::max)(excellentW, 1.0f));
        const float excellentDrawW = excellentW * excellentScale;
        const float excellentDrawH = excellentH * excellentScale;
        const float excellentX = (w - excellentDrawW) * 0.5f;
        const float excellentY = (h - excellentDrawH) * 0.5f;
        DrawPauseRect(excellentX - 36.0f, excellentY - 20.0f,
                      excellentDrawW + 72.0f, excellentDrawH + 40.0f,
                      {0.0f, 0.0f, 0.0f, 0.42f});
        DrawPauseImage(tutorialTextureIds_[kTutorialTextExcellent],
                       excellentW, excellentH, excellentX, excellentY,
                       excellentScale, 1.0f);
    }
    ctx_->rendering.sprite->PostDraw();
}

void GameScene::DrawTutorialEntryFade() {
    if (!tutorialMode_ || ctx_ == nullptr || ctx_->rendering.sprite == nullptr ||
        ctx_->systems.winApp == nullptr ||
        (tutorialEntryFadeTimer_ >= kTutorialEntryFadeDuration &&
         !tutorialExitRequested_)) {
        return;
    }

    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());
    float alpha = 0.0f;
    if (tutorialEntryFadeTimer_ < kTutorialEntryFadeDuration) {
        const float t =
            SmoothStep01(tutorialEntryFadeTimer_ / kTutorialEntryFadeDuration);
        alpha = (std::max)(alpha, 1.0f - t);
    }
    if (tutorialExitRequested_) {
        const float t =
            SmoothStep01(tutorialExitFadeTimer_ / kTutorialExitFadeDuration);
        alpha = (std::max)(alpha, t);
    }
    ctx_->rendering.sprite->PreDraw();
    DrawPauseRect(0.0f, 0.0f, w, h, {0.0f, 0.0f, 0.0f, alpha});
    ctx_->rendering.sprite->PostDraw();
}

void GameScene::DispatchCombatFeedback(const CombatFeedbackEvent &event) {
    combatFeedback_.PushEvent(event);
    EmitCombatParticles(event);
    if (event.type == CombatFeedbackEventType::PlayerSlashHit ||
        event.type == CombatFeedbackEventType::CounterSuccess ||
        event.type == CombatFeedbackEventType::BladeClashPierce) {
        DirectX::XMFLOAT2 slashDirection{0.0f, 0.0f};
        const auto swords = player_.GetSwords();
        if (event.swordIndex < swords.size() && swords[event.swordIndex]) {
            slashDirection = swords[event.swordIndex]->GetSlashDirection();
        }
        if (event.type == CombatFeedbackEventType::CounterSuccess) {
            swordSlashArcRenderer_.EmitParryLine(event.position, event.direction,
                                                 camera_, event.power,
                                                 slashDirection);
        } else {
            swordSlashArcRenderer_.EmitHitLine(event.position, event.direction,
                                               camera_, event.power,
                                               slashDirection);
        }
    }
    if (!soundsLoaded_ || ctx_ == nullptr || ctx_->systems.sound == nullptr) {
        return;
    }

    switch (event.type) {
    case CombatFeedbackEventType::CounterSuccess:
    case CombatFeedbackEventType::BladeClashGuardBreak:
        ctx_->systems.sound->Play(counterSoundId_, kCounterSoundVolume);
        break;
    case CombatFeedbackEventType::PlayerSlashHit:
    case CombatFeedbackEventType::BladeClashPierce:
        ctx_->systems.sound->Play(hitSoundId_, kHitSoundVolume);
        break;
    case CombatFeedbackEventType::PlayerDamaged:
        ctx_->systems.sound->Play(damageSoundId_, kDamageSoundVolume);
        break;
    default:
        break;
    }
}

void GameScene::UpdateSwordVfx(float deltaTime) {
    swordTrailRenderer_.Update(player_, deltaTime);
    swordSlashArcRenderer_.Update(deltaTime);
}

void GameScene::EmitReadyPreviewHeatParticles(float deltaTime) {
    if (!readyPreviewMode_ || readyPreviewHeat_ <= 0.015f) {
        return;
    }

    readyPreviewParticleTimer_ -= deltaTime;
    if (readyPreviewParticleTimer_ > 0.0f) {
        return;
    }

    const float heat = readyPreviewHeat_;
    const float danger = heat * heat * (3.0f - 2.0f * heat);
    readyPreviewParticleTimer_ = 0.055f - 0.046f * danger;
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const float wave = sceneLightTime_ * (2.6f + danger * 7.4f);
    const float ringRadius = 0.52f + 1.18f * danger;
    const float side = std::sinf(wave * 1.7f) * ringRadius;
    const float depth = std::cosf(wave * 1.1f) * (0.28f + 0.58f * danger);
    const XMFLOAT3 core{
        enemyPos.x + side * 0.32f,
        enemyPos.y + 0.82f + 0.58f * danger,
        enemyPos.z - 0.34f + depth * 0.24f,
    };
    const XMFLOAT3 upward{std::sinf(wave) * (0.12f + 0.24f * danger), 1.0f,
                          std::cosf(wave * 0.8f) *
                              (0.10f + 0.22f * danger)};
    const XMFLOAT4 flameColor{1.0f, 0.30f + 0.48f * heat, 0.04f,
                              0.88f + 0.12f * danger};
    const XMFLOAT4 smokeColor{0.26f + 0.36f * danger, 0.18f + 0.10f * heat,
                              0.12f, 0.60f + 0.28f * danger};

    EmitParticleBurst(
        smokeParticles_, {core.x, core.y - 0.36f, core.z},
        static_cast<uint32_t>(34.0f + 88.0f * heat + 250.0f * danger),
        0.82f + 0.58f * heat + 1.24f * danger, AppParticleBurstStyle::Smoke,
        smokeColor, upward, 0.85f + 1.18f * heat + 2.28f * danger);
    EmitParticleBurst(
        sparkParticles_, {core.x, core.y + 0.12f, core.z},
        static_cast<uint32_t>(96.0f + 220.0f * heat + 520.0f * danger),
        0.30f + 0.34f * heat + 0.70f * danger, AppParticleBurstStyle::Sparks,
        flameColor, upward, 2.60f + 3.90f * heat + 6.90f * danger);

    const XMFLOAT3 secondOrigin{enemyPos.x - side * 0.68f,
                                enemyPos.y + 0.62f + 0.42f * danger,
                                enemyPos.z - 0.42f - depth * 0.36f};
    EmitParticleBurst(
        sparkParticles_, secondOrigin,
        static_cast<uint32_t>(74.0f + 176.0f * heat + 410.0f * danger),
        0.24f + 0.30f * heat + 0.64f * danger, AppParticleBurstStyle::Sparks,
        {1.0f, 0.22f + 0.44f * heat, 0.03f, 0.86f + 0.12f * danger},
        upward, 2.10f + 3.50f * heat + 6.20f * danger);

    if (heat > 0.24f) {
        EmitParticleBurst(
            explosionParticles_, {core.x, core.y + 0.22f, core.z},
            static_cast<uint32_t>(54.0f + 152.0f * heat + 350.0f * danger),
            0.24f + 0.30f * heat + 0.76f * danger,
            AppParticleBurstStyle::Explosion, flameColor, upward,
            1.75f + 2.80f * heat + 5.20f * danger);
        EmitParticleBurst(
            smokeParticles_,
            {core.x - side * 0.24f, core.y - 0.04f, core.z - depth * 0.18f},
            static_cast<uint32_t>(18.0f + 58.0f * heat + 170.0f * danger),
            0.38f + 0.78f * danger, AppParticleBurstStyle::Flash,
            {1.0f, 0.44f + 0.18f * heat, 0.08f, 0.30f + 0.42f * danger},
            upward, 0.86f + 2.55f * danger);
    }

    if (heat > 0.52f) {
        const XMFLOAT3 ringOrigin{enemyPos.x - side * 0.92f,
                                  enemyPos.y + 0.98f + 0.36f * danger,
                                  enemyPos.z - 0.36f + depth * 0.74f};
        EmitParticleBurst(
            explosionParticles_, ringOrigin,
            static_cast<uint32_t>(110.0f + 360.0f * danger),
            0.38f + 0.78f * danger, AppParticleBurstStyle::SlashLine,
            {1.0f, 0.64f, 0.08f, 0.76f + 0.22f * danger},
            {std::cosf(wave), 0.18f + 0.32f * danger, std::sinf(wave)},
            1.80f + 4.80f * danger);
        EmitParticleBurst(
            sparkParticles_, {ringOrigin.x, ringOrigin.y + 0.18f, ringOrigin.z},
            static_cast<uint32_t>(150.0f + 450.0f * danger),
            0.30f + 0.70f * danger, AppParticleBurstStyle::Sparks,
            {1.0f, 0.78f, 0.18f, 0.98f}, upward, 2.70f + 6.30f * danger);
    }

    if (heat > 0.72f) {
        const float burstSide = std::cosf(wave * 2.3f) * (1.05f + 0.72f * danger);
        const XMFLOAT3 panicOrigin{enemyPos.x + burstSide,
                                   enemyPos.y + 1.12f + 0.38f * danger,
                                   enemyPos.z - 0.30f - depth * 0.46f};
        EmitParticleBurst(
            explosionParticles_, panicOrigin,
            static_cast<uint32_t>(260.0f + 520.0f * danger),
            0.42f + 0.96f * danger, AppParticleBurstStyle::Explosion,
            {1.0f, 0.10f + 0.30f * heat, 0.02f, 0.94f}, upward,
            3.10f + 7.40f * danger);
        EmitParticleBurst(
            smokeParticles_, {panicOrigin.x, panicOrigin.y - 0.16f, panicOrigin.z},
            static_cast<uint32_t>(86.0f + 250.0f * danger),
            0.78f + 1.54f * danger, AppParticleBurstStyle::Flash,
            {1.0f, 0.34f, 0.08f, 0.58f + 0.34f * danger}, upward,
            1.50f + 4.10f * danger);
    }
}

void GameScene::EmitCombatParticles(const CombatFeedbackEvent &event) {
    XMFLOAT3 position = event.position;
    position.y += 0.08f;

    const XMFLOAT3 direction = event.direction;
    const float power = (std::max)(0.6f, event.power);

    switch (event.type) {
    case CombatFeedbackEventType::PlayerSlashHit:
        EmitParticleBurst(sparkParticles_, position,
                          static_cast<uint32_t>(34.0f + power * 12.0f), 0.050f,
                          AppParticleBurstStyle::Sparks,
                          {0.94f, 0.97f, 1.00f, 0.76f}, direction,
                          2.35f + power * 0.38f);
        EmitParticleBurst(swordFlashParticles_, position,
                          static_cast<uint32_t>(14.0f + power * 4.0f),
                          0.088f + power * 0.012f, AppParticleBurstStyle::Flash,
                          {1.0f, 0.98f, 1.00f, 0.72f}, direction,
                          0.38f + power * 0.05f);
        EmitParticleBurst(
            explosionParticles_, position,
            static_cast<uint32_t>(54.0f + power * 12.0f),
            0.18f + power * 0.016f, AppParticleBurstStyle::SlashLine,
            {0.94f, 0.90f, 1.00f, 0.82f}, direction, 2.18f + power * 0.30f);
        EmitParticleBurst(smokeParticles_, position,
                          static_cast<uint32_t>(6.0f + power * 2.0f),
                          0.13f + power * 0.012f, AppParticleBurstStyle::Smoke,
                          {0.96f, 0.97f, 1.00f, 0.16f}, direction,
                          1.26f + power * 0.12f);
        break;
    case CombatFeedbackEventType::PlayerDamaged:
        EmitParticleBurst(sparkParticles_, position, 110, 0.24f,
                          AppParticleBurstStyle::Sparks,
                          {1.00f, 0.52f, 0.20f, 1.0f}, direction, 2.15f);
        EmitParticleBurst(explosionParticles_, position, 42, 0.30f,
                          AppParticleBurstStyle::Explosion,
                          {1.00f, 0.30f, 0.10f, 1.0f}, direction, 1.15f);
        EmitParticleBurst(smokeParticles_, position, 34, 0.40f,
                          AppParticleBurstStyle::Smoke,
                          {0.48f, 0.36f, 0.28f, 1.0f}, direction, 0.78f);
        break;
    case CombatFeedbackEventType::CounterSuccess:
        EmitParticleBurst(swordFlashParticles_, position,
                          static_cast<uint32_t>(18.0f + power * 5.0f),
                          0.24f + power * 0.030f, AppParticleBurstStyle::Flash,
                          {0.88f, 1.00f, 1.00f, 0.92f}, direction,
                          0.62f + power * 0.06f);
        EmitParticleBurst(sparkParticles_, position,
                          static_cast<uint32_t>(154.0f + power * 24.0f),
                          0.42f + power * 0.035f,
                          AppParticleBurstStyle::Sparks,
                          {0.04f, 0.72f, 1.00f, 0.96f}, direction,
                          2.55f + power * 0.30f);
        EmitParticleBurst(explosionParticles_, position,
                          static_cast<uint32_t>(62.0f + power * 14.0f),
                          0.48f + power * 0.030f,
                          AppParticleBurstStyle::SlashLine,
                          {0.74f, 0.14f, 1.00f, 0.88f}, direction,
                          1.80f + power * 0.18f);
        EmitParticleBurst(explosionParticles_, position,
                          static_cast<uint32_t>(70.0f + power * 12.0f),
                          0.58f + power * 0.035f,
                          AppParticleBurstStyle::SpiritSparkle,
                          {0.28f, 0.92f, 1.00f, 0.76f}, direction,
                          1.62f + power * 0.20f);
        EmitParticleBurst(smokeParticles_, position, 12, 0.34f,
                          AppParticleBurstStyle::Flash,
                          {0.52f, 0.22f, 1.00f, 0.24f}, direction, 0.48f);
        break;
    case CombatFeedbackEventType::BladeClashGuardBreak:
        EmitParticleBurst(sparkParticles_, position, 170, 0.34f,
                          AppParticleBurstStyle::Sparks,
                          {1.00f, 0.76f, 0.26f, 1.0f}, direction, 2.1f);
        EmitParticleBurst(explosionParticles_, position, 78, 0.46f,
                          AppParticleBurstStyle::Explosion,
                          {1.00f, 0.46f, 0.12f, 1.0f}, direction, 1.48f);
        EmitParticleBurst(smokeParticles_, position, 54, 0.56f,
                          AppParticleBurstStyle::Smoke,
                          {0.36f, 0.31f, 0.27f, 1.0f}, direction, 0.92f);
        break;
    case CombatFeedbackEventType::BladeClashPierce:
        EmitParticleBurst(sparkParticles_, position, 140, 0.20f,
                          AppParticleBurstStyle::Sparks,
                          {1.00f, 0.86f, 0.34f, 0.94f}, direction, 2.45f);
        EmitParticleBurst(explosionParticles_, position, 96, 0.34f,
                          AppParticleBurstStyle::SlashLine,
                          {1.00f, 0.82f, 0.30f, 0.86f}, direction, 2.15f);
        EmitParticleBurst(swordFlashParticles_, position, 12, 0.24f,
                          AppParticleBurstStyle::Flash,
                          {1.0f, 0.96f, 0.72f, 0.74f}, direction, 0.36f);
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
            EmitParticleBurst(explosionParticles_, origin, 150, 2.10f,
                              AppParticleBurstStyle::SpiritSparkle,
                              {1.0f, 1.0f, 0.96f, 0.72f}, forward, 1.68f);
            break;
        case ActionKind::Sweep:
            EmitParticleBurst(explosionParticles_, origin, 158, 2.18f,
                              AppParticleBurstStyle::SpiritSparkle,
                              {1.0f, 1.0f, 0.96f, 0.70f}, forward, 1.58f);
            break;
        case ActionKind::BladeClash:
            EmitParticleBurst(explosionParticles_, origin, 180, 2.24f,
                              AppParticleBurstStyle::SpiritSparkle,
                              {1.0f, 0.92f, 0.66f, 0.78f}, forward, 1.82f);
            EmitParticleBurst(sparkParticles_, origin, 84, 0.20f,
                              AppParticleBurstStyle::Sparks,
                              {1.0f, 0.72f, 0.24f, 0.92f}, forward, 2.10f);
            break;
        default:
            break;
        }
    }
}

void GameScene::EmitEnemyCueParticles(float deltaTime) {
    enemyCueParticleTimer_ =
        (std::max)(0.0f, enemyCueParticleTimer_ - deltaTime);

    if (ctx_ == nullptr || battleIntroActive_ || victorySequenceActive_ ||
        defeatSequenceActive_ || bladeClashFinishActive_) {
        return;
    }

    const ActionKind kind = enemy_.GetActionKind();
    const ActionStep actionStep = enemy_.GetActionStep();
    if (!(kind == ActionKind::Smash || kind == ActionKind::Sweep) ||
        !(actionStep == ActionStep::Charge || actionStep == ActionStep::Hold ||
          actionStep == ActionStep::Active)) {
        return;
    }

    constexpr float kReleaseCounterWindowDuration = 0.62f;
    const float releaseAnticipation = enemy_.GetReleaseAnticipationRatio();
    const bool preReleaseCounterCueVisible =
        actionStep != ActionStep::Active && releaseAnticipation > 0.0f;
    const bool activeReleaseCounterCueVisible =
        actionStep == ActionStep::Active &&
        enemy_.GetActionTimerForPresentation() <=
            kReleaseCounterWindowDuration;
    bool releaseCounterCueVisible =
        preReleaseCounterCueVisible || activeReleaseCounterCueVisible;
    if (tutorialMode_) {
        if (tutorialStep_ == kTutorialStepRedSmash ||
            tutorialStep_ == kTutorialStepRedSweep) {
            releaseCounterCueVisible = false;
        } else if ((tutorialStep_ == kTutorialStepGreenSmash ||
                    tutorialStep_ == kTutorialStepGreenSweep) &&
                   !releaseCounterCueVisible) {
            return;
        }
    }

    if (enemyCueParticleTimer_ > 0.0f) {
        return;
    }

    const float yaw = enemy_.GetTelegraphYaw();
    const XMFLOAT3 forward = {std::sinf(yaw), 0.12f, std::cosf(yaw)};
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    XMFLOAT3 cuePos = enemyPos;
    cuePos.x += forward.x * 1.18f;
    cuePos.y += 1.28f;
    cuePos.z += forward.z * 1.18f;

    const XMFLOAT4 cueColor =
        releaseCounterCueVisible ? XMFLOAT4{0.22f, 1.0f, 0.34f, 0.92f}
                                 : XMFLOAT4{1.0f, 0.02f, 0.12f, 0.98f};
    const XMFLOAT4 lineColor =
        releaseCounterCueVisible ? XMFLOAT4{0.20f, 1.0f, 0.32f, 1.0f}
                                 : XMFLOAT4{1.0f, 0.06f, 0.06f, 1.0f};
    const float cuePower =
        activeReleaseCounterCueVisible
            ? 1.0f
            : std::clamp(0.64f + releaseAnticipation * 0.28f, 0.64f,
                         0.92f);

    EmitParticleBurst(
        sparkParticles_, cuePos, releaseCounterCueVisible ? 72u : 76u,
        releaseCounterCueVisible ? 0.90f * cuePower : 1.18f,
        AppParticleBurstStyle::Sparks, cueColor, forward,
        releaseCounterCueVisible ? 4.15f * cuePower : 4.70f);
    const SwordCounterAxis cueAxis = RequiredVisualCounterAxisForAction(kind);
    if (cueAxis != SwordCounterAxis::None) {
        swordSlashArcRenderer_.EmitDirectionCueLine(
            cuePos,
            cueAxis == SwordCounterAxis::Vertical ? XMFLOAT2{0.0f, 1.0f}
                                                  : XMFLOAT2{1.0f, 0.0f},
            camera_, lineColor, releaseCounterCueVisible);
    }

    const XMFLOAT4 flashColor =
        releaseCounterCueVisible ? XMFLOAT4{0.18f, 1.0f, 0.28f, 0.78f}
                                 : XMFLOAT4{1.0f, 0.04f, 0.10f, 0.88f};
    EmitParticleBurst(
        smokeParticles_, cuePos,
        releaseCounterCueVisible ? (activeReleaseCounterCueVisible ? 7 : 4) : 5,
        releaseCounterCueVisible ? (activeReleaseCounterCueVisible ? 0.82f
                                                                   : 0.62f)
                                 : 0.70f,
        AppParticleBurstStyle::Flash, flashColor, forward, 0.58f);

    enemyCueParticleTimer_ = releaseCounterCueVisible ? 0.110f : 0.125f;
}

void GameScene::UpdateBattlePostProcessState(float deltaTime) {
    (void)deltaTime;
    if (titleDemoMode_ || backgroundOnlyMode_ || readyPreviewMode_ ||
        ctx_ == nullptr ||
        ctx_->rendering.postProcessSystem == nullptr) {
        return;
    }

    PostProcessProfile profile = GetPostProcessProfile(ctx_);
    const bool hasFeedbackPost = profile.radialBlur.strength > 0.001f ||
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
    if (tutorialMode_) {
        DrawTutorialOverlay();
        DrawHandCameraPreview();
        DrawTutorialEntryFade();
        return;
    }
    if (backgroundOnlyMode_ || readyPreviewMode_ || titleDemoMode_) {
        return;
    }
    if (battleIntroActive_) {
        return;
    }
    hud_.Draw(*ctx_);
    DrawBladeClashOverlay();
    DrawPauseMenu();
    DrawHandCameraPreview();
}

void GameScene::UpdateHandCameraPreview(float deltaTime) {
    if (inputCalibration_.controlType != InputControlType::Hand ||
        !handTrackingStartRequested_) {
        return;
    }
    cameraPreviewReceiver_.Update(deltaTime);
}

void GameScene::DrawHandCameraPreview() {
    if (inputCalibration_.controlType != InputControlType::Hand ||
        !handTrackingStartRequested_ || ctx_ == nullptr ||
        ctx_->rendering.sprite == nullptr || ctx_->rendering.texture == nullptr) {
        return;
    }
    cameraPreviewReceiver_.Draw(ctx_->rendering.sprite, ctx_->rendering.texture,
                                kHandCameraPreviewStaleSeconds);
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
    const float release =
        SmoothStep01((ratio - kReleaseStart) / kReleaseDuration);
    const float hold = charge * (1.0f - release);
    if (!titleDemoMode_) {
        ApplyBattlePostProcess(ctx_, 0.010f + 0.026f * hold + 0.036f * release,
                               0.30f + 0.42f * hold, 0.10f + 0.18f * hold);
    }

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
        ApplyReleasedClearColor(ctx_->rendering.dxCommon);
    }
}

void GameScene::EmitPhaseTransitionStartEffects() {
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 origin{enemyPos.x, enemyPos.y + 1.18f, enemyPos.z};
    EmitParticleBurst(smokeParticles_, origin, 54, 1.00f,
                      AppParticleBurstStyle::Flash, {1.0f, 0.52f, 0.12f, 0.86f},
                      {0.0f, 1.0f, 0.0f}, 0.62f);
    EmitParticleBurst(sparkParticles_, origin, 150, 1.45f,
                      AppParticleBurstStyle::Sparks,
                      {1.0f, 0.72f, 0.24f, 0.96f}, {0.0f, 1.0f, 0.0f}, 3.2f);
    if (soundsLoaded_ && ctx_ != nullptr && ctx_->systems.sound != nullptr) {
        ctx_->systems.sound->Play(enemyReleaseSoundId_,
                                  kEnemyReleaseSoundVolume);
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
    const float release =
        SmoothStep01((ratio - kReleaseStart) / kReleaseDuration);
    const float hold = charge * (1.0f - release);
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 origin{enemyPos.x, enemyPos.y + 1.28f, enemyPos.z};
    const uint32_t sparkCount = static_cast<uint32_t>(28.0f + 44.0f * hold);
    EmitParticleBurst(sparkParticles_, origin, sparkCount, 0.58f + 0.46f * hold,
                      AppParticleBurstStyle::Sparks,
                      {1.0f, 0.28f, 0.04f, 0.86f}, {0.0f, 1.0f, 0.0f},
                      2.4f + 2.0f * hold);
    if (hold > 0.35f) {
        EmitParticleBurst(
            smokeParticles_, origin, 3, 0.58f, AppParticleBurstStyle::Flash,
            {1.0f, 0.20f, 0.04f, 0.52f}, {0.0f, 1.0f, 0.0f}, 0.38f);
    }
}

void GameScene::EmitPhaseTransitionReleaseEffects() {
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 origin{enemyPos.x, enemyPos.y + 1.30f, enemyPos.z};
    EmitParticleBurst(explosionParticles_, origin, 110, 1.38f,
                      AppParticleBurstStyle::Explosion,
                      {1.0f, 0.30f, 0.05f, 0.92f}, {0.0f, 1.0f, 0.0f}, 3.0f);
    EmitParticleBurst(sparkParticles_, origin, 230, 1.85f,
                      AppParticleBurstStyle::Sparks, {1.0f, 0.86f, 0.30f, 1.0f},
                      {0.0f, 1.0f, 0.0f}, 7.0f);
    EmitParticleBurst(smokeParticles_, origin, 52, 1.42f,
                      AppParticleBurstStyle::Flash, {1.0f, 0.58f, 0.12f, 0.76f},
                      {0.0f, 1.0f, 0.0f}, 1.0f);
    if (soundsLoaded_ && ctx_ != nullptr && ctx_->systems.sound != nullptr) {
        ctx_->systems.sound->Play(explosionSoundId_, kExplosionSoundVolume);
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
    ApplyBattleIntroClearColor(ctx_->rendering.dxCommon, battleIntroTimer_);
    if (!titleDemoMode_ && !battleIntroRevealEmitted_) {
        ApplyBattlePostProcess(ctx_, 0.035f * (1.0f - ratio),
                               0.16f + 0.06f * ratio, 0.0f, 0.48f, 18);
    } else if (!titleDemoMode_) {
        if (!titleDemoMode_) {
            ApplyReleasedPostProcess(ctx_);
        }
    }
    if (!battleIntroRevealEmitted_ && battleIntroTimer_ >= 2.36f) {
        battleIntroRevealEmitted_ = true;
        combatFeedback_.AddCameraShake(0.38f, 0.055f, 0.034f);
        if (!titleDemoMode_) {
            ApplyReleasedPostProcess(ctx_);
        }
        const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
        EmitParticleBurst(
            swordFlashParticles_, {enemyPos.x, enemyPos.y + 1.45f, enemyPos.z},
            8, 0.38f, AppParticleBurstStyle::Flash, {1.0f, 0.98f, 0.88f, 0.82f},
            {0.0f, 1.0f, 0.0f}, 0.22f);
        EmitParticleBurst(
            explosionParticles_, {enemyPos.x, enemyPos.y + 1.30f, enemyPos.z},
            156, 2.12f, AppParticleBurstStyle::SpiritSparkle,
            {1.0f, 1.0f, 0.96f, 0.68f}, {0.0f, 1.0f, 0.0f}, 1.52f);
        if (soundsLoaded_ && ctx_ != nullptr &&
            ctx_->systems.sound != nullptr) {
            ctx_->systems.sound->Play(enemyReleaseSoundId_,
                                      kEnemyReleaseSoundVolume);
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
        FinishBattleIntro();
    }
}

void GameScene::FinishBattleIntro() {
    battleIntroActive_ = false;
    battleIntroTimer_ = battleIntroDuration_;
    battleIntroRevealEmitted_ = true;
    ApplyEnemyIntroDissolve(1.0f);
    ClearBattlePostProcess(ctx_);
    if (!titleDemoMode_) {
        ApplyReleasedPostProcess(ctx_);
    }
    ApplyReleasedClearColor(ctx_->rendering.dxCommon);
    enemy_.FaceTargetImmediately(player_.GetTransform().position);
    SyncEnemyAnimation();
    SetEnemyAnimationFrozen(false);
    UpdateSceneLighting();
    UpdateBattleCamera();
    camera_.UpdateMatrices();
}

void GameScene::ApplyEnemyIntroDissolve(float revealRatio) {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr ||
        enemyModelId_ == 0) {
        return;
    }
    Model *model = ctx_->rendering.model->GetModel(enemyModelId_);
    if (model == nullptr) {
        return;
    }

    const float reveal = std::clamp(revealRatio, 0.0f, 1.0f);
    const bool dissolveActive = battleIntroActive_ && reveal < 0.995f;
    const float liquidRipple =
        dissolveActive
            ? 0.045f * std::sinf(battleIntroTimer_ * 18.0f) * (1.0f - reveal)
            : 0.0f;
    const float threshold =
        std::clamp(0.96f - reveal * 1.12f + liquidRipple, 0.0f, 1.0f);
    const float alpha = SmoothStep01(reveal);
    const float glowAlpha =
        dissolveActive ? (0.18f + alpha * 0.82f) : 1.0f;
    for (ModelSubMesh &subMesh : model->subMeshes) {
        Material material =
            ctx_->rendering.model->GetMaterial(subMesh.materialId);
        material.enableDissolve = dissolveActive ? 1.0f : 0.0f;
        material.dissolveThreshold = threshold;
        material.dissolveEdgeWidth = 0.16f + 0.18f * (1.0f - reveal);
        material.dissolveEdgeColor = {1.0f, 0.84f, 0.32f, 1.0f};
        material.color.w = glowAlpha;
        material.blendMode = dissolveActive
                                 ? static_cast<int32_t>(BlendMode::Transparent)
                                 : static_cast<int32_t>(BlendMode::Opaque);
        material.depthWrite = dissolveActive ? 0 : 1;
        ctx_->rendering.model->SetMaterial(subMesh.materialId, material);
    }
}

void GameScene::UpdateBladeClashFinish(float deltaTime) {
    if (!bladeClashFinishActive_) {
        return;
    }

    const float bladeClashWinActionTimer =
        AdvanceBladeClashFinishTimer(deltaTime);
    ApplyBladeClashFinishPostProcess();

    if (bladeClashFinishPlayerWon_ &&
        !bladeClashFinishGuardBreakEmitted_ &&
        bladeClashFinishTimer_ >= Clash::kWinGuardBreakImpactTime) {
        bladeClashFinishGuardBreakEmitted_ = true;
        XMFLOAT3 breakCenter = enemy_.GetTransform().position;
        breakCenter.y += 1.22f;
        EmitParticleBurst(explosionParticles_, breakCenter, 92, 0.86f,
                          AppParticleBurstStyle::Explosion,
                          {1.0f, 0.86f, 0.42f, 0.72f},
                          {-bladeClashDirection_.x, 0.16f,
                           -bladeClashDirection_.z},
                          1.72f);
        EmitParticleBurst(sparkParticles_, breakCenter, 128, 0.72f,
                          AppParticleBurstStyle::Sparks,
                          {1.0f, 0.92f, 0.48f, 0.88f},
                          {bladeClashDirection_.z, 0.18f,
                           -bladeClashDirection_.x},
                          2.25f);
        EmitParticleBurst(swordFlashParticles_, breakCenter, 10, 0.58f,
                          AppParticleBurstStyle::Flash,
                          {1.0f, 1.0f, 0.82f, 0.66f},
                          {-bladeClashDirection_.x, 0.0f,
                           -bladeClashDirection_.z},
                          0.34f);

        CombatFeedbackEvent guardBreakFeedback{};
        guardBreakFeedback.type = CombatFeedbackEventType::BladeClashGuardBreak;
        guardBreakFeedback.position = breakCenter;
        guardBreakFeedback.direction = {-bladeClashDirection_.x, 0.0f,
                                        -bladeClashDirection_.z};
        guardBreakFeedback.power = 10.5f;
        DispatchCombatFeedback(guardBreakFeedback);
    }

    if (bladeClashFinishPlayerWon_) {
        if (!bladeClashFinishImpactEmitted_ &&
            bladeClashWinActionTimer >= 0.28f) {
            bladeClashFinishImpactEmitted_ = true;
            XMFLOAT3 cutCenter = enemy_.GetTransform().position;
            cutCenter.y += 1.18f;
            EmitParticleBurst(explosionParticles_, cutCenter, 104, 0.86f,
                              AppParticleBurstStyle::SlashLine,
                              {1.0f, 0.90f, 0.58f, 0.66f},
                              {bladeClashDirection_.z, 0.12f,
                               -bladeClashDirection_.x},
                              1.72f);
            EmitParticleBurst(explosionParticles_, cutCenter, 42, 0.68f,
                              AppParticleBurstStyle::Explosion,
                              {1.0f, 0.78f, 0.34f, 0.50f},
                              {bladeClashDirection_.x, 0.16f,
                               bladeClashDirection_.z},
                              1.18f);
            EmitParticleBurst(swordFlashParticles_, cutCenter, 8, 0.42f,
                              AppParticleBurstStyle::Flash,
                              {1.0f, 0.96f, 0.80f, 0.56f},
                              {bladeClashDirection_.z, 0.0f,
                               -bladeClashDirection_.x},
                              0.26f);
            EmitParticleBurst(sparkParticles_, cutCenter, 46, 0.26f,
                              AppParticleBurstStyle::Sparks,
                              {1.0f, 0.82f, 0.28f, 0.58f},
                              {bladeClashDirection_.z, 0.16f,
                               -bladeClashDirection_.x},
                              1.08f);

            CombatFeedbackEvent slashFeedback{};
            slashFeedback.type = CombatFeedbackEventType::BladeClashPierce;
            slashFeedback.position = cutCenter;
            slashFeedback.direction = {bladeClashDirection_.z, 0.0f,
                                       -bladeClashDirection_.x};
            slashFeedback.power = 9.5f;
            DispatchCombatFeedback(slashFeedback);
        }
        if (!bladeClashFinishSkidEmitted_ &&
            bladeClashWinActionTimer >= 0.76f) {
            bladeClashFinishSkidEmitted_ = true;
            XMFLOAT3 skidCenter = {
                enemy_.GetTransform().position.x +
                    bladeClashDirection_.x * 6.8f,
                player_.GetTransform().position.y + 0.54f,
                enemy_.GetTransform().position.z +
                    bladeClashDirection_.z * 6.8f};
            EmitParticleBurst(explosionParticles_, skidCenter, 78, 0.92f,
                              AppParticleBurstStyle::SlashLine,
                              {1.0f, 0.78f, 0.32f, 0.58f},
                              {bladeClashDirection_.z, 0.02f,
                               -bladeClashDirection_.x},
                              1.46f);
            EmitParticleBurst(sparkParticles_, skidCenter, 48, 0.46f,
                              AppParticleBurstStyle::Sparks,
                              {1.0f, 0.70f, 0.20f, 0.70f},
                              {-bladeClashDirection_.x, 0.04f,
                               -bladeClashDirection_.z},
                              1.08f);
            EmitParticleBurst(smokeParticles_, skidCenter, 52, 0.94f,
                              AppParticleBurstStyle::Smoke,
                              {0.36f, 0.30f, 0.25f, 0.52f},
                              {bladeClashDirection_.x, 0.08f,
                               bladeClashDirection_.z},
                              1.08f);
        }

        const float finishYaw =
            std::atan2f(bladeClashDirection_.x, bladeClashDirection_.z);
        player_.SetDefeatPoseRatio(0.0f);
        if (bladeClashWinActionTimer <= 0.0f) {
            const float guardT = std::clamp(
                bladeClashFinishTimer_ / Clash::kWinGuardBreakLead, 0.0f,
                1.0f);
            const float brace = guardT < 0.10f ? guardT / 0.10f : 1.0f;
            const float strain =
                std::sinf(std::clamp(guardT / 0.42f, 0.0f, 1.0f) * kPi);
            const float collapseT = std::clamp(
                (bladeClashFinishTimer_ - Clash::kWinGuardBreakImpactTime) /
                    (Clash::kWinGuardBreakLead -
                     Clash::kWinGuardBreakImpactTime),
                0.0f, 1.0f);
            const float collapseEase =
                1.0f - std::pow(1.0f - collapseT, 4.0f);
            const float snap =
                std::sinf(std::clamp((bladeClashFinishTimer_ -
                                      Clash::kWinGuardBreakImpactTime) /
                                         0.08f,
                                     0.0f, 1.0f) *
                          kPi);
            XMFLOAT3 bracePos = bladeClashFinishPlayerStart_;
            bracePos.x += bladeClashDirection_.x *
                          (0.07f * brace + 0.13f * strain);
            bracePos.z += bladeClashDirection_.z *
                          (0.07f * brace + 0.13f * strain);
            player_.SetCinematicBladeClashPose(
                bracePos, finishYaw,
                std::clamp(0.38f + 0.14f * strain + 0.10f * snap, 0.0f,
                           1.0f));

            XMFLOAT3 enemyPos = {
                bladeClashFinishEnemyStart_.x +
                    bladeClashDirection_.x *
                        (0.13f * snap +
                         Clash::kGuardBreakRecoilDistance * collapseEase),
                bladeClashFinishEnemyStart_.y + 0.09f * snap +
                    (Clash::kGuardBreakLift -
                     Clash::kGuardBreakDrop * 0.25f) *
                        collapseEase,
                bladeClashFinishEnemyStart_.z +
                    bladeClashDirection_.z *
                        (0.13f * snap +
                         Clash::kGuardBreakRecoilDistance * collapseEase)};
            const float enemyYaw =
                std::atan2f(-bladeClashDirection_.x, -bladeClashDirection_.z);
            const float side = bladeClashDirection_.x >= 0.0f ? 1.0f : -1.0f;
            enemy_.SetCinematicTransform(
                enemyPos, enemyYaw, -Clash::kGuardBreakPose * collapseEase,
                side * (0.085f * snap + 0.090f * collapseEase));
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
            XMFLOAT3 dashPos =
                Lerp(bladeClashFinishPlayerStart_, bladeClashFinishPlayerEnd_,
                     dashEase);
            const float anticipation =
                std::sinf(windupEase * kPi) * (1.0f - cutEase);
            const float lift =
                std::sinf(cutT * kPi) * 0.10f * (1.0f - settleEase);
            dashPos.x -= bladeClashDirection_.x * 0.42f * anticipation;
            dashPos.y += lift;
            dashPos.z -= bladeClashDirection_.z * 0.42f * anticipation;
            player_.SetCinematicBladeClashPose(
                dashPos, finishYaw,
                std::clamp(0.58f +
                               0.42f *
                                   std::sinf(std::clamp(
                                                 (bladeClashWinActionTimer -
                                                  0.06f) /
                                                     0.38f,
                                                 0.0f, 1.0f) *
                                             kPi) -
                               0.16f * settleEase,
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
                Clash::kGuardBreakRecoilDistance + 1.12f * enemyHitEase +
                3.45f * enemyBreakEase + 5.90f * enemySlamEase +
                1.95f * enemySlideEase;
            const float side = bladeClashDirection_.x >= 0.0f ? 1.0f : -1.0f;
            const XMFLOAT3 right = {bladeClashDirection_.z, 0.0f,
                                    -bladeClashDirection_.x};
            const float sideDrift =
                side * (0.42f * hitPop + 0.92f * enemySlamEase -
                        0.18f * enemySlideEase);
            XMFLOAT3 enemyPos = {
                bladeClashFinishEnemyStart_.x + bladeClashDirection_.x * recoil +
                    right.x * sideDrift,
                bladeClashFinishEnemyStart_.y +
                    Clash::kGuardBreakLift * (1.0f - 0.22f * enemySlamEase) -
                    Clash::kGuardBreakDrop + hitPop * 0.50f +
                    breakArc * 1.12f + slamArc * 0.58f -
                    0.82f * enemySlideEase,
                bladeClashFinishEnemyStart_.z + bladeClashDirection_.z * recoil +
                    right.z * sideDrift};
            const float enemyYaw =
                std::atan2f(-bladeClashDirection_.x, -bladeClashDirection_.z);
            enemy_.SetCinematicTransform(
                enemyPos,
                enemyYaw +
                    side * (0.46f * hitPop + 0.98f * enemySlamEase -
                            0.22f * enemySlideEase),
                -0.18f * Clash::kGuardBreakPose - 0.28f * hitPop -
                    0.90f * enemySlamEase + 0.18f * enemySlideEase,
                side * (0.36f * hitPop + 0.92f * enemySlamEase -
                        0.20f * enemySlideEase));
        }
    } else {
        if (!bladeClashFinishSkidEmitted_ &&
            bladeClashFinishTimer_ >= Clash::kLossHitTime) {
            bladeClashFinishSkidEmitted_ = true;
            bladeClashFinishImpactEmitted_ = true;
            XMFLOAT3 sweepCenter = player_.GetTransform().position;
            sweepCenter.y += 1.02f;
            EmitParticleBurst(explosionParticles_, sweepCenter, 92, 1.24f,
                              AppParticleBurstStyle::SlashLine,
                              {1.0f, 0.28f, 0.06f, 0.96f},
                              {bladeClashDirection_.z, -0.04f,
                               -bladeClashDirection_.x},
                              1.92f);
            EmitParticleBurst(swordFlashParticles_, sweepCenter, 14, 0.92f,
                              AppParticleBurstStyle::Flash,
                              {1.0f, 0.56f, 0.16f, 0.82f},
                              {-bladeClashDirection_.x, 0.0f,
                               -bladeClashDirection_.z},
                              0.44f);
            EmitParticleBurst(sparkParticles_, sweepCenter, 62, 0.44f,
                              AppParticleBurstStyle::Sparks,
                              {1.0f, 0.48f, 0.12f, 0.82f},
                              {-bladeClashDirection_.x, 0.08f,
                               -bladeClashDirection_.z},
                              1.45f);

            CombatFeedbackEvent lossFeedback{};
            lossFeedback.type = CombatFeedbackEventType::PlayerDamaged;
            lossFeedback.position = sweepCenter;
            lossFeedback.direction = {-bladeClashDirection_.x, 0.0f,
                                      -bladeClashDirection_.z};
            lossFeedback.power = 12.0f;
            DispatchCombatFeedback(lossFeedback);
            playerHitCooldown_ = 0.82f;
        }
        if (bladeClashFinishSkidEmitted_ &&
            bladeClashFinishTimer_ >= 1.24f &&
            bladeClashFinishTimer_ < 1.28f) {
            XMFLOAT3 skidCenter = player_.GetTransform().position;
            skidCenter.y += 0.28f;
            EmitParticleBurst(explosionParticles_, skidCenter, 36, 0.56f,
                              AppParticleBurstStyle::SlashLine,
                              {1.0f, 0.42f, 0.10f, 0.38f},
                              {bladeClashDirection_.z, 0.02f,
                               -bladeClashDirection_.x},
                              0.92f);
            EmitParticleBurst(smokeParticles_, skidCenter, 38, 0.64f,
                              AppParticleBurstStyle::Smoke,
                              {0.34f, 0.28f, 0.24f, 0.44f},
                              {-bladeClashDirection_.x, 0.03f,
                               -bladeClashDirection_.z},
                              0.62f);
        }
        if (bladeClashFinishSkidEmitted_ &&
            !bladeClashFinishWallImpactEmitted_ &&
            bladeClashFinishTimer_ >= Clash::kLossWallImpactTime) {
            bladeClashFinishWallImpactEmitted_ = true;
            XMFLOAT3 crashCenter = player_.GetTransform().position;
            crashCenter.y += 0.86f;
            EmitParticleBurst(explosionParticles_, crashCenter, 110, 0.96f,
                              AppParticleBurstStyle::Explosion,
                              {1.0f, 0.36f, 0.10f, 0.78f},
                              {-bladeClashDirection_.x, 0.12f,
                               -bladeClashDirection_.z},
                              2.35f);
            EmitParticleBurst(sparkParticles_, crashCenter, 72, 0.58f,
                              AppParticleBurstStyle::Sparks,
                              {1.0f, 0.56f, 0.16f, 0.86f},
                              {bladeClashDirection_.z, 0.10f,
                               -bladeClashDirection_.x},
                              1.42f);
            EmitParticleBurst(smokeParticles_, crashCenter, 64, 0.90f,
                              AppParticleBurstStyle::Smoke,
                              {0.36f, 0.30f, 0.26f, 0.56f},
                              {-bladeClashDirection_.x, 0.04f,
                               -bladeClashDirection_.z},
                              0.92f);

            CombatFeedbackEvent crashFeedback{};
            crashFeedback.type = CombatFeedbackEventType::PlayerDamaged;
            crashFeedback.position = crashCenter;
            crashFeedback.direction = {-bladeClashDirection_.x, 0.0f,
                                       -bladeClashDirection_.z};
            crashFeedback.power = 16.0f;
            DispatchCombatFeedback(crashFeedback);
        }

        const Clash::LossPose lossPose = Clash::EvaluateLossPose(
            bladeClashFinishTimer_, bladeClashFinishPlayerStart_,
            bladeClashDirection_);
        player_.LockPosition(lossPose.position);
        if (bladeClashFinishSkidEmitted_) {
            const float faceEnemyYaw =
                std::atan2f(bladeClashDirection_.x, bladeClashDirection_.z);
            const float awayYaw =
                std::atan2f(-bladeClashDirection_.x, -bladeClashDirection_.z);
            player_.SetYaw(lossPose.slideT > 0.001f ? awayYaw : faceEnemyYaw);
            player_.SetBladeClashPose(false);
            player_.SetDefeatPoseRatio(std::clamp(
                0.20f + 0.58f * lossPose.recoilEase +
                    0.32f * lossPose.slideEase,
                0.0f, 1.0f));
        } else {
            player_.SetDefeatPoseRatio(0.0f);
            player_.SetBladeClashPose(true, 0.0f);
        }
    }

    if (bladeClashFinishTimer_ >= bladeClashFinishDuration_) {
        CompleteBladeClashFinish();
    }
}

float GameScene::AdvanceBladeClashFinishTimer(float deltaTime) {
    float finishDeltaTime = deltaTime;
    if (bladeClashFinishPlayerWon_) {
        finishDeltaTime *= Clash::GuardBreakTimeScale(bladeClashFinishTimer_);
    }
    bladeClashFinishTimer_ += finishDeltaTime;
    return bladeClashFinishPlayerWon_
               ? Clash::WinActionTimer(bladeClashFinishTimer_)
               : bladeClashFinishTimer_;
}

void GameScene::ApplyBladeClashFinishPostProcess() {
    const float ratio =
        bladeClashFinishDuration_ > 0.0001f
            ? std::clamp(bladeClashFinishTimer_ / bladeClashFinishDuration_,
                         0.0f, 1.0f)
            : 1.0f;
    const float hold = 1.0f - std::clamp((ratio - 0.76f) / 0.24f, 0.0f, 1.0f);
    ApplyBattlePostProcess(
        ctx_, bladeClashFinishPlayerWon_ ? 0.018f * hold : 0.020f * hold,
        bladeClashFinishPlayerWon_ ? 0.22f : 0.46f,
        bladeClashFinishPlayerWon_ ? 0.03f * hold : 0.16f * hold, 0.48f, 18);
}

void GameScene::CompleteBladeClashFinish() {
    const bool shouldResolveEnemyTransition =
        bladeClashFinishPendingEnemyTransition_;
    bladeClashFinishActive_ = false;
    bladeClashFinishPendingEnemyTransition_ = false;
    player_.SetBladeClashPose(false);
    player_.SetDefeatPoseRatio(0.0f);
    SetEnemyAnimationFrozen(false);
    ClearBattlePostProcess(ctx_);
    if (shouldResolveEnemyTransition) {
        enemy_.ResolveDeferredDamageTransitions();
        enemyHitCooldown_ = 0.22f;
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
        hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(), enemy_.GetMaxHP());
    }

    if (!titleDemoMode_) {
        ApplyBattlePostProcess(ctx_, 0.055f, 0.58f, 0.10f, 0.48f, 24);
    }

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

    if (!titleDemoMode_) {
        ApplyBattlePostProcess(ctx_, 0.040f, 0.62f, 0.18f, 0.54f, 22);
    }

    const XMFLOAT3 playerPos = player_.GetTransform().position;
    EmitParticleBurst(explosionParticles_,
                      {playerPos.x, playerPos.y + 1.05f, playerPos.z}, 180,
                      1.55f, AppParticleBurstStyle::Explosion,
                      {1.0f, 0.12f, 0.05f, 0.82f}, {0.0f, 1.0f, 0.0f}, 2.2f);
    EmitParticleBurst(sparkParticles_,
                      {playerPos.x, playerPos.y + 1.18f, playerPos.z}, 260,
                      1.35f, AppParticleBurstStyle::Sparks,
                      {1.0f, 0.32f, 0.16f, 0.92f}, {0.0f, 1.0f, 0.0f}, 5.2f);
}

void GameScene::UpdateDefeatSequence(float deltaTime) {
    defeatSequenceTimer_ += deltaTime;
    const float fallStart = 0.34f;
    const float fallDuration = 1.22f;
    const float fallRatio = std::clamp(
        (defeatSequenceTimer_ - fallStart) / fallDuration, 0.0f, 1.0f);
    player_.SetDefeatPoseRatio(fallRatio);

    const float postProcessRatio =
        std::clamp(defeatSequenceTimer_ / defeatSequenceDuration_, 0.0f, 1.0f);
    if (!titleDemoMode_) {
        ApplyBattlePostProcess(ctx_, 0.040f * (1.0f - postProcessRatio), 0.62f,
                               0.18f + 0.22f * postProcessRatio, 0.54f, 22);
    }

    if (!defeatImpactEmitted_ && defeatSequenceTimer_ >= 1.58f) {
        defeatImpactEmitted_ = true;
        const XMFLOAT3 playerPos = player_.GetTransform().position;
        EmitParticleBurst(
            smokeParticles_, {playerPos.x, playerPos.y + 0.28f, playerPos.z},
            160, 2.15f, AppParticleBurstStyle::Smoke,
            {0.18f, 0.17f, 0.16f, 0.88f}, {0.0f, 1.0f, 0.0f}, 0.85f);
        EmitParticleBurst(
            sparkParticles_, {playerPos.x, playerPos.y + 0.42f, playerPos.z},
            180, 1.05f, AppParticleBurstStyle::Sparks,
            {1.0f, 0.18f, 0.08f, 0.86f}, {0.0f, 1.0f, 0.0f}, 4.2f);
    }

    if (defeatSequenceTimer_ >= defeatSequenceDuration_) {
        defeatSequenceActive_ = false;
        player_.SetDefeatPoseRatio(0.0f);
        ClearBattlePostProcess(ctx_);
        if (titleDemoMode_) {
            battleResultRequested_ = false;
            battleIntroActive_ = true;
            battleIntroTimer_ = 0.0f;
            battleIntroRevealEmitted_ = false;
            player_.Initialize(playerModelId_, swordModelId_);
            player_.SetInputCalibration(inputCalibration_);
            enemy_.SetDifficulty(combatDifficulty_);
            enemy_.Initialize(enemyModelId_);
            enemy_.FaceTargetImmediately(player_.GetTransform().position);
            ApplyEnemyIntroDissolve(0.0f);
            return;
        }
        sceneManager_->ChangeScene(std::make_unique<BattleResultScene>(
            BattleResultScene::ResultKind::GameOver, battleElapsedTime_,
            inputCalibration_, combatDifficulty_));
    }
}

void GameScene::UpdateVictorySequence(float deltaTime) {
    victorySequenceTimer_ += deltaTime;
    const float ratio = std::clamp(
        victorySequenceTimer_ / victorySequenceDuration_, 0.0f, 1.0f);

    const float stepped = std::floor(ratio * 14.0f) / 14.0f;
    const float blur = (1.0f - stepped) * 0.070f;
    if (!titleDemoMode_) {
        ApplyBattlePostProcess(ctx_, blur, 0.58f, 0.10f + stepped * 0.18f,
                               0.48f, 24);
    }

    if (!victoryFinalExplosionEmitted_ && victorySequenceTimer_ >= 3.90f) {
        victoryFinalExplosionEmitted_ = true;
        const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
        if (soundsLoaded_ && ctx_ != nullptr &&
            ctx_->systems.sound != nullptr) {
            ctx_->systems.sound->Play(explosionSoundId_,
                                      kExplosionSoundVolume);
        }
        EmitParticleBurst(
            explosionParticles_, {enemyPos.x, enemyPos.y + 1.05f, enemyPos.z},
            3200, 8.40f, AppParticleBurstStyle::Explosion,
            {1.0f, 0.64f, 0.08f, 1.0f}, {0.0f, 1.0f, 0.0f}, 5.80f);
        EmitParticleBurst(
            sparkParticles_, {enemyPos.x, enemyPos.y + 1.22f, enemyPos.z}, 3600,
            9.20f, AppParticleBurstStyle::Sparks, {1.0f, 0.98f, 0.58f, 1.0f},
            {0.0f, 1.0f, 0.0f}, 12.4f);
        EmitParticleBurst(
            smokeParticles_, {enemyPos.x, enemyPos.y + 1.12f, enemyPos.z}, 680,
            6.80f, AppParticleBurstStyle::Flash, {1.0f, 0.94f, 0.70f, 1.0f},
            {0.0f, 1.0f, 0.0f}, 1.55f);
    }

    if (victorySequenceTimer_ >= victorySequenceDuration_) {
        victorySequenceActive_ = false;
        ClearBattlePostProcess(ctx_);
        if (titleDemoMode_) {
            battleResultRequested_ = false;
            battleIntroActive_ = true;
            battleIntroTimer_ = 0.0f;
            battleIntroRevealEmitted_ = false;
            player_.Initialize(playerModelId_, swordModelId_);
            player_.SetInputCalibration(inputCalibration_);
            enemy_.SetDifficulty(combatDifficulty_);
            enemy_.Initialize(enemyModelId_);
            enemy_.FaceTargetImmediately(player_.GetTransform().position);
            ApplyEnemyIntroDissolve(0.0f);
            return;
        }
        sceneManager_->ChangeScene(std::make_unique<BattleResultScene>(
            BattleResultScene::ResultKind::Clear, victoryClearTime_,
            inputCalibration_, combatDifficulty_));
    }
}

void GameScene::DrawVictoryFlash() {
    if (!victorySequenceActive_ || ctx_ == nullptr ||
        ctx_->rendering.sprite == nullptr || ctx_->systems.winApp == nullptr) {
        return;
    }

    const float introPulse =
        victorySequenceTimer_ < 0.82f
            ? 0.56f + 0.44f * std::sin(victorySequenceTimer_ * 12.0f)
            : 0.0f;
    const float earlyFlash =
        (std::max)(0.0f, 1.0f - victorySequenceTimer_ / 0.88f);
    const auto flashPulse = [&](float center, float width, float peak) {
        return (std::max)(0.0f,
                          (1.0f -
                           std::fabs(victorySequenceTimer_ - center) / width) *
                              peak);
    };
    const float fallFlash = (std::max)(flashPulse(1.35f, 0.20f, 0.70f),
                                       flashPulse(2.25f, 0.22f, 0.80f));
    const float preExplosionFlash = flashPulse(3.58f, 0.42f, 1.0f);
    const float blink = (std::max)(introPulse * introPulse,
                                   (std::max)(fallFlash, preExplosionFlash));
    const float finalFlash =
        victoryFinalExplosionEmitted_
            ? (std::max)(0.0f, 1.0f - (victorySequenceTimer_ - 3.90f) / 0.52f)
            : 0.0f;
    const float alpha = std::clamp(
        (std::max)(earlyFlash, (std::max)(blink, finalFlash)), 0.0f, 1.0f);
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
    if (!defeatSequenceActive_ || ctx_ == nullptr ||
        ctx_->rendering.sprite == nullptr || ctx_->systems.winApp == nullptr) {
        return;
    }

    const auto pulse = [&](float center, float width, float peak) {
        return (std::max)(0.0f,
                          (1.0f -
                           std::fabs(defeatSequenceTimer_ - center) / width) *
                              peak);
    };
    const float red =
        (std::max)(pulse(0.08f, 0.22f, 0.72f), pulse(1.58f, 0.30f, 0.36f));
    const float black = std::clamp((defeatSequenceTimer_ - 1.55f) /
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
    if (!battleIntroActive_ || ctx_ == nullptr ||
        ctx_->rendering.sprite == nullptr || ctx_->systems.winApp == nullptr) {
        return;
    }

    const auto flashPulse = [&](float center, float width, float peak) {
        return (std::max)(0.0f, (1.0f - std::fabs(battleIntroTimer_ - center) /
                                            width) *
                                    peak);
    };
    const float appearFlash = flashPulse(2.36f, 0.24f, 0.58f);
    const float afterGlow = flashPulse(2.68f, 0.52f, 0.22f);
    const float alpha =
        std::clamp((std::max)(appearFlash, afterGlow), 0.0f, 0.62f);
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

float GameScene::BackgroundBuildProgress(float delay, float duration) const {
    if (!backgroundOnlyMode_) {
        if (battleIntroActive_ && !readyPreviewMode_) {
            return SmoothStep01((BattleIntroWorldRevealProgress() - delay) /
                                duration);
        }
        return 1.0f;
    }
    return SmoothStep01((backgroundBuildTimer_ - delay) / duration);
}

float GameScene::BattleIntroWorldRevealProgress() const {
    if (!battleIntroActive_ || readyPreviewMode_) {
        return 1.0f;
    }

    constexpr float kReleaseTime = 2.36f;
    constexpr float kRevealDuration = 0.20f;
    return SmoothStep01((battleIntroTimer_ - kReleaseTime) / kRevealDuration);
}

void GameScene::DrawBladeClashFinishBackdrop() {
    ModelManager *model = ctx_->rendering.model;
    if (model == nullptr) {
        return;
    }

    ModelDrawEffect backdropEffect{};
    backdropEffect.enabled = true;
    backdropEffect.additiveBlend = false;
    backdropEffect.disableCulling = true;
    backdropEffect.color = {0.74f, 0.78f, 0.86f, 0.22f};
    backdropEffect.intensity = 0.10f;
    backdropEffect.fresnelPower = 1.60f;
    backdropEffect.noiseAmount = 0.0f;
    backdropEffect.time = sceneLightTime_;
    model->SetDrawEffect(backdropEffect);

    Transform terrain{};
    terrain.position = {0.0f, -0.42f, 0.0f};
    terrain.rotation = MakeQuat(0.0f, 0.0f, 0.0f);
    terrain.scale = {1.0f, 1.0f, 1.0f};
    model->Draw(arenaLowPolyTerrainModelId_, terrain, camera_);

    DrawDistantHazardBackdrop(1.0f);

    Transform floor{};
    floor.position = {0.0f, -0.04f, 0.0f};
    floor.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    floor.scale = {28.0f, 28.0f, 1.0f};
    model->Draw(arenaFloorModelId_, floor, camera_);

    Transform centerDisk{};
    centerDisk.position = {0.0f, 0.006f, 0.0f};
    centerDisk.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    centerDisk.scale = {1.0f, 1.0f, 1.0f};
    model->Draw(arenaCenterDiskModelId_, centerDisk, camera_);

    model->ClearDrawEffect();
}

void GameScene::DrawDistantHazardBackdrop(float buildProgress) {
    ModelManager *model = ctx_->rendering.model;
    buildProgress = SmoothStep01(buildProgress);
    if (buildProgress <= 0.0f) {
        return;
    }
    const float pulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 1.8f);

    ModelDrawEffect cityEffect{};
    cityEffect.enabled = true;
    cityEffect.additiveBlend = false;
    cityEffect.disableCulling = true;
    cityEffect.color = {0.16f, 0.20f, 0.24f, 0.90f * buildProgress};
    cityEffect.intensity = 0.12f * buildProgress;
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
                    2.4f +
                    static_cast<float>((i * 5 + row * 7 + side) % 10) * 0.45f +
                    static_cast<float>(row) * 0.55f;
                const float order =
                    (static_cast<float>(row) * 0.12f +
                     static_cast<float>(i) / 17.0f * 0.16f);
                const float towerBuild =
                    SmoothStep01((buildProgress - order) / 0.34f);
                if (towerBuild <= 0.0f) {
                    continue;
                }
                Transform tower{};
                tower.position =
                    alongX ? XMFLOAT3{lane, -0.68f, sign * depthLane + 8.0f}
                           : XMFLOAT3{sign * depthLane, -0.68f, lane + 8.0f};
                tower.rotation =
                    MakeQuat(0.0f, alongX ? 0.0f : kPi * 0.5f, 0.0f);
                tower.scale = {
                    0.62f + static_cast<float>((i + row) % 3) * 0.16f,
                    height * towerBuild,
                    0.78f + static_cast<float>((i * 3 + row) % 2) * 0.24f};
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
    blockEffect.color = {0.07f, 0.10f, 0.13f, 0.92f * buildProgress};
    blockEffect.intensity = 0.18f * buildProgress;
    model->SetDrawEffect(blockEffect);
    std::vector<Transform> cityBlocks;
    cityBlocks.reserve(23u);
    for (int i = 0; i < 15; ++i) {
        const float offset = static_cast<float>(i - 7);
        const float blockBuild =
            SmoothStep01((buildProgress - static_cast<float>(i) * 0.012f) /
                         0.30f);
        if (blockBuild <= 0.0f) {
            continue;
        }
        Transform wall{};
        wall.position = {offset * 1.72f, -0.76f,
                         72.0f + std::fabs(offset) * 0.42f};
        wall.rotation = MakeQuat(0.0f, 0.0f, 0.0f);
        wall.scale = {1.28f + 0.12f * static_cast<float>(i % 3),
                      (5.3f + static_cast<float>((i * 5) % 5) * 0.72f) *
                          blockBuild,
                      1.08f};
        cityBlocks.push_back(wall);
    }
    for (int side = 0; side < 2; ++side) {
        const float sign = side == 0 ? -1.0f : 1.0f;
        for (int step = 0; step < 4; ++step) {
            const float braceBuild =
                SmoothStep01((buildProgress -
                              (0.18f + static_cast<float>(step) * 0.08f)) /
                             0.28f);
            if (braceBuild <= 0.0f) {
                continue;
            }
            Transform brace{};
            brace.position = {sign * (9.2f + static_cast<float>(step) * 2.25f),
                              2.25f + static_cast<float>(step) * 0.62f,
                              69.4f + static_cast<float>(step) * 1.55f};
            brace.rotation = MakeQuat(0.0f, sign * 0.12f, 0.0f);
            brace.scale = {(2.8f - static_cast<float>(step) * 0.22f) * braceBuild,
                           0.30f,
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
    lightEffect.color = {0.96f, 0.58f, 0.28f, 0.18f * buildProgress};
    lightEffect.intensity = (0.035f + 0.025f * pulse) * buildProgress;
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
            const float windowBuild =
                SmoothStep01((buildProgress -
                              (0.32f + static_cast<float>(i) * 0.018f)) /
                             0.34f);
            if (windowBuild <= 0.0f) {
                continue;
            }
            const float lane = (static_cast<float>(i) - 5.0f) * 4.0f;
            Transform panel{};
            panel.position =
                alongX
                    ? XMFLOAT3{lane, 1.15f + static_cast<float>(i % 4) * 0.58f,
                               sign * 47.35f + 8.0f}
                    : XMFLOAT3{sign * 47.35f,
                               1.15f + static_cast<float>(i % 4) * 0.58f,
                               lane + 8.0f};
            panel.rotation = MakeQuat(0.0f, alongX ? 0.0f : kPi * 0.5f, 0.0f);
            panel.scale = {0.16f,
                           (1.05f + static_cast<float>(i % 2) * 0.40f) *
                               windowBuild,
                           1.0f};
            cityWindows.push_back(panel);
        }
    }
    if (!cityWindows.empty()) {
        model->DrawInstanced(arenaCityWindowModelId_, cityWindows.data(),
                             static_cast<uint32_t>(cityWindows.size()),
                             camera_);
    }
    model->ClearDrawEffect();
}

void GameScene::DrawArena() {
    ModelManager *model = ctx_->rendering.model;
    const float pulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 2.2f);
    constexpr float kPatternSpacing = 3.55f;
    constexpr float kLaneSpacing = 4.25f;

    const float floorBuild = BackgroundBuildProgress(0.00f, 0.20f);
    const float tileBuild = BackgroundBuildProgress(0.10f, 0.30f);
    const float lineBuild = BackgroundBuildProgress(0.18f, 0.28f);
    const float distantBuild = BackgroundBuildProgress(0.24f, 0.38f);

    if (!backgroundOnlyMode_) {
        DrawDistantHazardBackdrop(distantBuild);
    }

    Transform floor{};
    if (floorBuild > 0.0f) {
        floor.position = {0.0f, -0.04f, 0.0f};
        floor.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        floor.scale = {180.0f * floorBuild, 180.0f * floorBuild, 1.0f};
        if (tutorialBackgroundMode_) {
            ModelDrawEffect floorEffect{};
            floorEffect.enabled = true;
            floorEffect.disableCulling = true;
            floorEffect.color = {0.02f, 0.90f, 0.78f, 0.36f};
            floorEffect.intensity = 0.16f * floorBuild;
            floorEffect.fresnelPower = 0.70f;
            floorEffect.baseDim = 0.32f;
            floorEffect.alphaBoost = 0.28f;
            floorEffect.time = sceneLightTime_;
            model->SetDrawEffect(floorEffect);
        }
        model->Draw(arenaFloorModelId_, floor, camera_);
        if (tutorialBackgroundMode_) {
            model->ClearDrawEffect();
        }
    }

    ModelDrawEffect fieldEffect{};
    fieldEffect.enabled = true;
    fieldEffect.additiveBlend = false;
    fieldEffect.disableCulling = true;
    fieldEffect.color = tutorialBackgroundMode_
                            ? XMFLOAT4{0.020f, 0.78f, 0.70f,
                                       0.180f * tileBuild}
                            : XMFLOAT4{0.085f, 0.075f, 0.060f,
                                       0.115f * tileBuild};
    fieldEffect.intensity =
        tutorialBackgroundMode_ ? 0.020f * tileBuild : 0.004f * tileBuild;
    fieldEffect.fresnelPower = 0.7f;
    fieldEffect.noiseAmount = 0.0f;
    fieldEffect.baseDim = tutorialBackgroundMode_ ? 0.18f : 0.0f;
    fieldEffect.time = sceneLightTime_;
    model->SetDrawEffect(fieldEffect);

    std::vector<Transform> fieldTiles;
    fieldTiles.reserve(480u);
    for (int z = -14; z <= 14; ++z) {
        for (int x = -14; x <= 14; ++x) {
            if ((std::abs(x) + std::abs(z)) % 2 != 0) {
                continue;
            }
            const float distance =
                (std::fabs(static_cast<float>(x)) + std::fabs(static_cast<float>(z))) /
                28.0f;
            const float tileLocalBuild =
                SmoothStep01((tileBuild - distance * 0.34f) / 0.46f);
            if (tileLocalBuild <= 0.0f) {
                continue;
            }
            Transform tile{};
            tile.position = {static_cast<float>(x) * kPatternSpacing, 0.004f,
                             static_cast<float>(z) * kPatternSpacing};
            tile.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
            tile.scale = {2.26f * tileLocalBuild, 2.26f * tileLocalBuild,
                          1.0f};
            fieldTiles.push_back(tile);
        }
    }

    if (tileBuild > 0.0f) {
        Transform center{};
        center.position = {0.0f, 0.012f, 0.0f};
        center.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        center.scale = {4.8f * tileBuild, 4.8f * tileBuild, 1.0f};
        fieldTiles.push_back(center);
    }

    for (int i = -13; i <= 13; ++i) {
        const float laneLocalBuild =
            SmoothStep01((lineBuild -
                          std::fabs(static_cast<float>(i)) / 13.0f * 0.24f) /
                         0.56f);
        if (laneLocalBuild <= 0.0f) {
            continue;
        }
        Transform laneX{};
        laneX.position = {0.0f, 0.016f, static_cast<float>(i) * kLaneSpacing};
        laneX.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        laneX.scale = {168.0f * laneLocalBuild, i == 0 ? 0.080f : 0.034f,
                       1.0f};
        fieldTiles.push_back(laneX);

        Transform laneZ{};
        laneZ.position = {static_cast<float>(i) * kLaneSpacing, 0.017f, 0.0f};
        laneZ.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        laneZ.scale = {i == 0 ? 0.080f : 0.034f, 168.0f * laneLocalBuild,
                       1.0f};
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
    lineEffect.color = tutorialBackgroundMode_
                           ? XMFLOAT4{0.020f, 0.95f, 0.84f,
                                      0.070f * lineBuild}
                           : XMFLOAT4{0.22f, 0.10f, 0.045f,
                                      0.026f * lineBuild};
    lineEffect.intensity =
        (tutorialBackgroundMode_ ? 0.018f + 0.010f * pulse
                                 : 0.0015f + 0.0015f * pulse) *
        lineBuild;
    lineEffect.fresnelPower = 0.75f;
    lineEffect.noiseAmount = 0.0f;
    lineEffect.time = sceneLightTime_;
    model->SetDrawEffect(lineEffect);
    std::vector<Transform> glowLines;
    glowLines.reserve(62u);
    for (int i = -15; i <= 15; ++i) {
        const float glowBuild =
            SmoothStep01((lineBuild -
                          std::fabs(static_cast<float>(i)) / 15.0f * 0.28f) /
                         0.52f);
        if (glowBuild <= 0.0f) {
            continue;
        }
        Transform lightX{};
        lightX.position = {0.0f, 0.034f,
                           static_cast<float>(i) * kPatternSpacing};
        lightX.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        lightX.scale = {160.0f * glowBuild, 0.016f, 1.0f};
        glowLines.push_back(lightX);

        Transform lightZ{};
        lightZ.position = {static_cast<float>(i) * kPatternSpacing, 0.035f,
                           0.0f};
        lightZ.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        lightZ.scale = {0.016f, 160.0f * glowBuild, 1.0f};
        glowLines.push_back(lightZ);
    }
    if (!glowLines.empty()) {
        model->DrawInstanced(arenaCityWindowModelId_, glowLines.data(),
                             static_cast<uint32_t>(glowLines.size()), camera_);
    }
    model->ClearDrawEffect();

    if (backgroundOnlyMode_) {
        DrawDistantHazardBackdrop(distantBuild);
    }
}
