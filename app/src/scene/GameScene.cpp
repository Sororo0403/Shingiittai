#include "GameScene.h"
#include "AppSceneServices.h"
#include "BattleArenaRenderer.h"
#include "BladeClashCinematic.h"
#include "DirectXCommon.h"
#include "GameOverScene.h"
#include "GameVictoryScene.h"
#include "Input.h"
#include "Material.h"
#include "Model.h"
#include "ModelManager.h"
#include "PostEffectManager.h"
#include "SceneManager.h"
#include "SoundManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "TitleScene.h"
#include "TutorialSelectScene.h"
#include "WeaponSelectScene.h"
#include "WinApp.h"
#include "compat/ParticleCompat.h"
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <vector>

using namespace DirectX;

namespace {
template <class T>
T PickValue(bool condition, const T &trueValue, const T &falseValue) {
    return condition ? trueValue : falseValue;
}

bool IsEnemyAttackAction(ActionKind kind) {
    return kind == ActionKind::Smash || kind == ActionKind::Sweep ||
           kind == ActionKind::BladeClash || kind == ActionKind::ArcaneLaser ||
           kind == ActionKind::CataclysmLaser;
}

bool IsEnemyRangedAction(ActionKind kind) {
    return kind == ActionKind::ArcaneLaser ||
           kind == ActionKind::CataclysmLaser;
}

bool IsEnemyBasicSlashAction(ActionKind kind) {
    return kind == ActionKind::Smash || kind == ActionKind::Sweep;
}
bool IsPauseDirectionTriggered(Input &input, int primaryKey, int secondaryKey,
                               bool gamepad, WORD gamepadButton) {
    return input.IsKeyTrigger(primaryKey) || input.IsKeyTrigger(secondaryKey) ||
           (gamepad && input.IsGamepadButtonTrigger(gamepadButton));
}

bool IsPauseCancelTriggered(Input &input, bool gamepad) {
    return input.IsKeyTrigger(DIK_ESCAPE) || input.IsKeyTrigger(DIK_TAB) ||
           (gamepad && input.IsGamepadButtonTrigger(XINPUT_GAMEPAD_B));
}

bool IsPauseConfirmTriggered(Input &input, bool gamepad) {
    return input.IsKeyTrigger(DIK_RETURN) || input.IsKeyTrigger(DIK_SPACE) ||
           (gamepad && input.IsGamepadButtonTrigger(XINPUT_GAMEPAD_A));
}
constexpr float kPi = 3.14159265f;
namespace Clash = BladeClashCinematic;
constexpr float kSlashSoundVolume = 0.76f;
constexpr float kEnemyReleaseSoundVolume = 0.68f;
constexpr float kEnemyWarpSoundVolume = 0.66f;
constexpr float kHitSoundVolume = 0.74f;
constexpr float kCounterSoundVolume = 0.72f;
constexpr float kDamageSoundVolume = 0.74f;
constexpr float kExplosionSoundVolume = 0.68f;
constexpr float kVictoryEnemyFallStart = 0.78f;
constexpr float kVictoryEnemyFallDuration = 2.35f;
constexpr float kVictoryEnemyVanishDelay = 0.18f;
constexpr float kVictoryEnemyVanishTime = kVictoryEnemyFallStart +
                                          kVictoryEnemyFallDuration +
                                          kVictoryEnemyVanishDelay;
constexpr float kVictoryEnemyExplosionBillboardRise = 0.24f;
constexpr float kVictoryEnemyExplosionBillboardFadeStart = 0.82f;
constexpr float kVictoryEnemyExplosionBillboardFadeDuration = 0.72f;
constexpr float kArcaneProjectileHostileSpeed = 15.0f;
constexpr float kArcaneProjectileArcSpeed = 13.2f;
constexpr float kArcaneProjectileArcTurnStrength = 5.4f;
constexpr float kArcaneProjectileArcLiftDuration = 0.34f;
constexpr float kArcaneProjectileArcGravity = 9.8f;
constexpr float kCataclysmProjectileHostileSpeed = 12.0f;
constexpr float kArcaneProjectileReflectedSpeed = 34.0f;
constexpr float kCataclysmProjectileReflectedSpeed = 58.0f;
constexpr float kArcaneProjectileReflectedLaunchHold = 0.15f;
constexpr float kCataclysmProjectileReflectedLaunchHold = 0.12f;
constexpr float kArcaneProjectileReflectedReturnBlend = 0.44f;
constexpr float kCataclysmProjectileReflectedReturnBlend = 0.34f;
constexpr float kPlayerChargedProjectileSpeed = 38.0f;
constexpr float kPlayerChargedProjectileDamage = 210.0f;
constexpr float kArcaneProjectileReflectedSteerStrength = 18.0f;
constexpr float kCataclysmProjectileHomingDelay = 0.34f;
constexpr float kCataclysmProjectileHomingStrength = 5.6f;
constexpr float kArcaneProjectileDeflectRange = 5.80f;
constexpr float kCataclysmProjectileDeflectHeight = 13.8f;
constexpr float kArcaneProjectileSlashDot = 0.55f;
constexpr int kArcaneProjectileVolleyShotCount = 3;
constexpr int kCataclysmProjectileVolleyShotCount = 5;
constexpr float kArcaneProjectileVolleyInterval = 0.62f;
constexpr float kCataclysmProjectileVolleyInterval = 0.58f;
constexpr XMFLOAT2 kArcaneProjectileCueDirections[] = {
    {0.0f, 1.0f},
    {1.0f, 0.0f},
    {0.7071f, 0.7071f},
    {-0.7071f, 0.7071f},
};
constexpr float kArcaneProjectileVisualScaleMultiplier = 5.0f;
constexpr float kCataclysmProjectileBarrageSpeed = 11.0f;
constexpr float kCataclysmProjectileLaunchSpeed = 24.0f;
constexpr float kCataclysmProjectileSpreadDuration = 0.54f;
constexpr float kCataclysmProjectilePreviewHold = 0.96f;
constexpr float kCataclysmProjectileConvergeStrength = 8.4f;
constexpr float kCataclysmProjectileRocketLiftDuration = 0.38f;
constexpr float kCataclysmProjectileRocketLiftAcceleration = 18.0f;
constexpr float kCataclysmProjectileRocketGravity = 16.0f;
constexpr float kEnemyProjectileSmokeTrailBackOffset = 0.72f;
constexpr float kCataclysmProjectileSmokeTrailBackOffset = 1.04f;
constexpr float kRangedAttackPlayerMoveMultiplier = 0.34f;
constexpr float kCataclysmPreviewPlayerSlowMultiplier = 0.18f;
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
constexpr int kTutorialTextExit = 10;
constexpr int kTutorialTextShowHand = 11;
constexpr int kTutorialStepHandPresence = 0;
constexpr int kTutorialStepLeftSword = 1;
constexpr int kTutorialStepRightSword = 2;
constexpr int kTutorialStepRedSmash = 3;
constexpr int kTutorialStepGreenSmash = 4;
constexpr int kTutorialStepRedSweep = 5;
constexpr int kTutorialStepGreenSweep = 6;
constexpr int kTutorialStepPractice = 7;
constexpr int kTutorialOperationRequiredSlashes = 3;
constexpr float kTutorialHandPresenceRequiredSeconds = 3.0f;

XMFLOAT3
ComputeCataclysmProjectileLaunchDirection(int shotIndex,
                                          const XMFLOAT3 &baseForward) {
    const XMFLOAT3 side{baseForward.z, 0.0f, -baseForward.x};
    const float sideSign =
        static_cast<float>(shotIndex) -
        static_cast<float>(kCataclysmProjectileVolleyShotCount - 1) * 0.5f;
    const float sideWeight =
        std::clamp(std::fabs(sideSign) * 0.52f, 0.0f, 1.0f);
    return NormalizeParticleCompatVec3(
        {baseForward.x * 0.30f + side.x * sideSign * sideWeight, 0.76f,
         baseForward.z * 0.30f + side.z * sideSign * sideWeight},
        baseForward);
}
constexpr float kTutorialEntryFadeDuration = 0.64f;
constexpr float kTutorialEntryBlackHold = 0.14f;
constexpr float kTutorialExitFadeDuration = 0.42f;
constexpr float kTutorialControlsPadding = 32.0f;
constexpr float kTutorialControlsImageBottomTransparentPixels = 18.0f;
constexpr float kPauseExitFadeDuration = 0.42f;
constexpr float kBattleBgmBaseVolume = 0.24f;
constexpr uint16_t kHandCameraPreviewPort = 5006;
constexpr float kHandCameraPreviewStaleSeconds = 0.75f;
constexpr float kReadyPreviewParticleCountScale = 0.34f;
constexpr float kReadyPreviewParticleAlphaScale = 0.68f;
constexpr float kReadyPreviewParticleVelocityScale = 0.78f;

float FindLoudestPlaybackSecond(SoundManager *sound, uint32_t soundId) {
    if (sound == nullptr || soundId == SoundManager::kInvalidSoundId) {
        return 0.0f;
    }

    const SoundManager::SoundInfo *info = sound->GetInfo(soundId);
    if (info == nullptr || info->durationSeconds <= 0.0f) {
        return 0.0f;
    }

    constexpr float kScanStep = 0.005f;
    constexpr float kWindowSeconds = 0.035f;
    constexpr float kMinimumRemainingSeconds = 0.12f;
    const float scanEndSecond =
        (std::max)(0.0f, info->durationSeconds - kMinimumRemainingSeconds);
    float bestSecond = 0.0f;
    float bestAmplitude = -1.0f;
    const int scanStepCount = static_cast<int>(scanEndSecond / kScanStep) + 1;
    for (int step = 0; step < scanStepCount; ++step) {
        const float second = static_cast<float>(step) * kScanStep;
        const float amplitude =
            sound->GetAmplitudeAt(soundId, second, kWindowSeconds);
        if (amplitude > bestAmplitude) {
            bestAmplitude = amplitude;
            bestSecond = second;
        }
    }
    return bestSecond;
}

void SetPostProcessProfile(const SceneContext *ctx,
                           const PostProcessProfile &profile) {
    if (ctx == nullptr || ctx->rendering.postEffectManager == nullptr) {
        return;
    }
    ctx->rendering.postEffectManager->SetBaseProfile(profile);
}

void SetCinematicPostProcessProfile(const SceneContext *ctx,
                                    PostEffectLayerId layerId,
                                    const PostProcessProfile &profile) {
    if (ctx == nullptr || ctx->rendering.postEffectManager == nullptr ||
        layerId == 0) {
        return;
    }
    ctx->rendering.postEffectManager->SetLayerProfile(layerId, profile);
}

void ClearCinematicPostProcessProfile(const SceneContext *ctx,
                                      PostEffectLayerId layerId) {
    if (ctx == nullptr || ctx->rendering.postEffectManager == nullptr ||
        layerId == 0) {
        return;
    }
    ctx->rendering.postEffectManager->ClearLayer(layerId);
}

void ApplyBattlePostProcess(const SceneContext *ctx, PostEffectLayerId layerId,
                            float radialBlurStrength, float vignetteStrength,
                            float sceneDimStrength, float centerY = 0.48f,
                            int32_t sampleCount = 20,
                            float vignetteScale = 11.0f,
                            float vignettePower = 1.15f) {
    PostProcessProfile profile{};
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
    SetCinematicPostProcessProfile(ctx, layerId, profile);
}

void ClearBattlePostProcess(const SceneContext *ctx,
                            PostEffectLayerId layerId) {
    ClearCinematicPostProcessProfile(ctx, layerId);
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
    uint32_t arenaTutorialSpokeModelId = 0;
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

Material MakeTransparentBillboardMaterial(uint32_t textureId,
                                          const XMFLOAT4 &color) {
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

XMFLOAT3 SlashDirectionToWorld(const Camera &camera, const XMFLOAT2 &slashDir,
                               const XMFLOAT3 &fallback) {
    const XMFLOAT3 cameraForward = NormalizeParticleCompatVec3(
        AppCameraForward(camera), {0.0f, 0.0f, 1.0f});
    const XMFLOAT3 worldUp{0.0f, 1.0f, 0.0f};
    const XMFLOAT3 cameraRight = NormalizeParticleCompatVec3(
        CrossParticleCompatVec3(worldUp, cameraForward), {1.0f, 0.0f, 0.0f});
    const XMFLOAT3 cameraUp = NormalizeParticleCompatVec3(
        CrossParticleCompatVec3(cameraForward, cameraRight), worldUp);

    const float lenSq = slashDir.x * slashDir.x + slashDir.y * slashDir.y;
    if (lenSq < 0.010f) {
        return NormalizeParticleCompatVec3(fallback, {0.0f, 1.0f, 0.0f});
    }

    const float invLen = 1.0f / std::sqrt(lenSq);
    const XMFLOAT2 normalizedSlash{slashDir.x * invLen, slashDir.y * invLen};
    return NormalizeParticleCompatVec3(
        {cameraRight.x * normalizedSlash.x + cameraUp.x * normalizedSlash.y,
         cameraRight.y * normalizedSlash.x + cameraUp.y * normalizedSlash.y,
         cameraRight.z * normalizedSlash.x + cameraUp.z * normalizedSlash.y},
        fallback);
}

uint32_t ReadyPreviewParticleCount(float count) {
    return (std::max)(1u, static_cast<uint32_t>(std::round(
                              count * kReadyPreviewParticleCountScale)));
}

XMFLOAT4 ReadyPreviewParticleColor(float r, float g, float b, float a) {
    return {r, g, b, a * kReadyPreviewParticleAlphaScale};
}

constexpr float kFarSlashCounterFlashDuration = 0.50f;

SwordCounterAxis RequiredVisualCounterAxisForAction(ActionKind kind) {
    switch (kind) {
    case ActionKind::Smash:
        return SwordCounterAxis::Vertical;
    case ActionKind::Sweep:
    case ActionKind::BladeClash:
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

std::vector<uint8_t> CreateProceduralTexturePixels(
    uint32_t width, uint32_t height, const XMFLOAT3 &baseColor,
    const XMFLOAT3 &accentColor, uint32_t seed, float grainStrength,
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
                const float remaining = (std::max)(1.0f - grainStrength, 0.0f);
                pattern = broad * remaining * 0.58f + mid * remaining * 0.42f;
            } else {
                pattern = static_cast<float>(
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
    return pixels;
}

uint32_t CreateProceduralTexture(TextureManager *texture, uint32_t width,
                                 uint32_t height, const XMFLOAT3 &baseColor,
                                 const XMFLOAT3 &accentColor, uint32_t seed,
                                 float grainStrength,
                                 bool isotropicPattern = false) {
    const std::vector<uint8_t> pixels =
        CreateProceduralTexturePixels(width, height, baseColor, accentColor,
                                      seed, grainStrength, isotropicPattern);
    return texture->CreateFromRgbaPixels(width, height, pixels.data());
}

std::vector<uint8_t>
CreateSmoothMetalTexturePixels(uint32_t width, uint32_t height,
                               const XMFLOAT3 &baseColor,
                               const XMFLOAT3 &highlightColor) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    constexpr uint32_t kBrushSeed = 0xC1E4u;
    for (uint32_t y = 0; y < height; ++y) {
        const float v = height > 1u ? static_cast<float>(y) /
                                          static_cast<float>(height - 1u)
                                    : 0.0f;
        const float sheen =
            0.12f + 0.11f * std::sinf(v * 3.14159265f * 2.0f - 0.45f);
        for (uint32_t x = 0; x < width; ++x) {
            const size_t index = (static_cast<size_t>(y) * width + x) * 4u;
            const float wideColumn =
                static_cast<float>(Hash2D(x / 19u, 0u, kBrushSeed) & 255u) /
                255.0f;
            const float narrowColumn =
                static_cast<float>(Hash2D(x / 5u, 3u, kBrushSeed + 11u) &
                                   255u) /
                255.0f;
            const float scratchNoise =
                static_cast<float>(Hash2D(x, y / 23u, kBrushSeed + 29u) &
                                   255u) /
                255.0f;
            const float darkGroove =
                scratchNoise > 0.88f ? (scratchNoise - 0.88f) * 1.65f : 0.0f;
            const float brightEdge =
                scratchNoise < 0.06f ? (0.06f - scratchNoise) * 1.95f : 0.0f;
            const float brush = (wideColumn - 0.5f) * 0.18f +
                                (narrowColumn - 0.5f) * 0.13f - darkGroove +
                                brightEdge;
            const float t = std::clamp(sheen + brush, 0.0f, 1.0f);
            const XMFLOAT3 color{
                baseColor.x + (highlightColor.x - baseColor.x) * t,
                baseColor.y + (highlightColor.y - baseColor.y) * t,
                baseColor.z + (highlightColor.z - baseColor.z) * t};
            pixels[index + 0] =
                static_cast<uint8_t>(std::clamp(color.x, 0.0f, 1.0f) * 255.0f);
            pixels[index + 1] =
                static_cast<uint8_t>(std::clamp(color.y, 0.0f, 1.0f) * 255.0f);
            pixels[index + 2] =
                static_cast<uint8_t>(std::clamp(color.z, 0.0f, 1.0f) * 255.0f);
            pixels[index + 3] = 255u;
        }
    }
    return pixels;
}

uint32_t CreateSmoothMetalTexture(TextureManager *texture, uint32_t width,
                                  uint32_t height, const XMFLOAT3 &baseColor,
                                  const XMFLOAT3 &highlightColor) {
    const std::vector<uint8_t> pixels = CreateSmoothMetalTexturePixels(
        width, height, baseColor, highlightColor);
    return texture->CreateFromRgbaPixels(width, height, pixels.data());
}

void BlendTexturePixels(const std::vector<uint8_t> &from,
                        const std::vector<uint8_t> &to,
                        std::vector<uint8_t> &out, float t) {
    const size_t count = (std::min)(from.size(), to.size());
    out.resize(count);
    t = std::clamp(t, 0.0f, 1.0f);
    for (size_t i = 0; i < count; ++i) {
        const float value =
            static_cast<float>(from[i]) +
            (static_cast<float>(to[i]) - static_cast<float>(from[i])) * t;
        out[i] = static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
    }
}

uint32_t AppCreateRustedMetalTexture(TextureManager *texture, uint32_t width,
                                     uint32_t height) {
    return CreateProceduralTexture(texture, width, height,
                                   {0.23f, 0.22f, 0.20f}, {0.70f, 0.30f, 0.12f},
                                   0x914Au, 0.62f, true);
}

uint32_t AppCreateCleanMetalTexture(TextureManager *texture, uint32_t width,
                                    uint32_t height) {
    return CreateSmoothMetalTexture(
        texture, width, height, {0.40f, 0.42f, 0.41f}, {0.62f, 0.63f, 0.57f});
}

uint32_t AppCreateGoldMetalTexture(TextureManager *texture, uint32_t width,
                                   uint32_t height) {
    return CreateProceduralTexture(texture, width, height,
                                   {0.43f, 0.34f, 0.17f}, {0.70f, 0.58f, 0.30f},
                                   0xB05Du, 0.38f, true);
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

XMFLOAT4 Lerp(const XMFLOAT4 &a, const XMFLOAT4 &b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t};
}

float SmoothStep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float EaseInCubic01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * t;
}

float EaseOutCubic01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

XMFLOAT4 EnemyProjectileTint(BossPhase phase, bool phaseTransitionActive,
                             float transitionRatio) {
    constexpr XMFLOAT4 kRustTint{0.35f, 0.33f, 0.28f, 0.26f};
    constexpr XMFLOAT4 kCleanMetalTint{0.46f, 0.48f, 0.45f, 0.24f};
    constexpr XMFLOAT4 kGoldTint{0.50f, 0.36f, 0.13f, 0.26f};

    if (phase == BossPhase::Phase3) {
        return phaseTransitionActive ? Lerp(kCleanMetalTint, kGoldTint,
                                            SmoothStep01(transitionRatio))
                                     : kGoldTint;
    }
    if (phase == BossPhase::Phase2) {
        return phaseTransitionActive ? Lerp(kRustTint, kCleanMetalTint,
                                            SmoothStep01(transitionRatio))
                                     : kCleanMetalTint;
    }
    return kRustTint;
}

float EffectiveCombatDifficulty(float difficulty) {
    const float clamped = std::clamp(difficulty, 0.0f, 9.0f);
    const float pressure = SmoothStep01(clamped / 9.0f);
    return std::clamp(clamped + 0.55f + 0.45f * pressure, 0.0f, 9.0f);
}

XMFLOAT4 DifficultyGaugeHeatColor(float difficulty, float alpha) {
    const float t = std::clamp(difficulty, 0.0f, 9.0f) / 9.0f;
    const XMFLOAT4 blue{0.015f, 0.075f, 0.50f, alpha};
    const XMFLOAT4 yellow{1.0f, 0.78f, 0.10f, alpha};
    const XMFLOAT4 red{0.62f, 0.018f, 0.010f, alpha};
    if (t < 0.62f) {
        return Lerp(blue, yellow, t / 0.62f);
    }
    return Lerp(yellow, red, (t - 0.62f) / 0.38f);
}

void ApplyBattleIntroClearColor(DirectXCommon *dxCommon, float introTimer) {
    if (dxCommon == nullptr) {
        return;
    }

    constexpr float kReleaseFlashTime = 2.36f;
    if (introTimer < kReleaseFlashTime) {
        dxCommon->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    dxCommon->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

void ApplyReleasedClearColor(DirectXCommon *dxCommon) {
    if (dxCommon == nullptr) {
        return;
    }
    dxCommon->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

void ApplyReleasedPostProcess(const SceneContext *ctx,
                              PostEffectLayerId layerId) {
    ClearCinematicPostProcessProfile(ctx, layerId);
}

float GetChargeStanceSettleTime(ActionKind kind) {
    if (kind == ActionKind::Smash) {
        return 0.30f;
    }
    return 0.28f;
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

GameScene::~GameScene() {
    StopBattleBgm();
    if (ctx_ != nullptr && ctx_->rendering.postEffectManager != nullptr &&
        postEffectCinematicLayer_ != 0) {
        ctx_->rendering.postEffectManager->DestroyLayer(
            postEffectCinematicLayer_);
        postEffectCinematicLayer_ = 0;
    }
}

float GameScene::GetDifficultyRatio() const {
    return EffectiveCombatDifficulty(combatDifficulty_) / 9.0f;
}

float GameScene::GetHighDifficultyPressure() const {
    return GetDifficultyRatio();
}

float GameScene::GetEnemyProjectileSpeedScale() const {
    const float t = GetDifficultyRatio();
    return std::clamp(1.0f + (t - 0.56f) * 0.86f, 0.86f, 1.42f);
}

float GameScene::GetEnemyRangedVolleyInterval() const {
    const float t = GetDifficultyRatio();
    const float intervalScale =
        std::clamp(1.0f - (t - 0.56f) * 1.05f, 0.60f, 1.14f);
    const float baseInterval = arcaneProjectileVolleyCataclysm_
                                   ? kCataclysmProjectileVolleyInterval
                                   : kArcaneProjectileVolleyInterval;
    return baseInterval * intervalScale;
}

float GameScene::GetCounterCinematicDuration() const {
    constexpr float kBaseDuration = 0.66f;
    constexpr float kHardDuration = 0.38f;
    const float t = GetHighDifficultyPressure();
    return kBaseDuration + (kHardDuration - kBaseDuration) * t;
}

float GameScene::GetCounterVulnerabilityDuration() const {
    constexpr float kHardScale = 0.58f;
    const float t = GetHighDifficultyPressure();
    return player_.GetCounterVulnerabilityDuration() *
           (1.0f + (kHardScale - 1.0f) * t);
}

float GameScene::GetCounterPlayerHitCooldown(float baseCooldown) const {
    constexpr float kHardScale = 0.64f;
    const float t = GetHighDifficultyPressure();
    return baseCooldown * (1.0f + (kHardScale - 1.0f) * t);
}

float GameScene::GetEnemyNormalHitCooldown() const {
    constexpr float kBaseCooldown = 0.20f;
    constexpr float kHardCooldown = 0.12f;
    const float t = GetHighDifficultyPressure();
    return kBaseCooldown + (kHardCooldown - kBaseCooldown) * t;
}

void GameScene::SetReadyPreviewHeat(float heat) {
    readyPreviewHeat_ = std::clamp(heat, 0.0f, 1.0f);
}

void GameScene::StartBattleBgm() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr || titleDemoMode_ ||
        backgroundOnlyMode_ || readyPreviewMode_ ||
        battleBgmVoiceHandle_ != SoundManager::kInvalidVoiceHandle) {
        return;
    }

    const wchar_t *bgmPath =
        tutorialMode_ ? L"app/resources/audio/bgm/bgm_TutorialTheme.wav"
                      : L"app/resources/audio/bgm/bgm_Battle.wav";
    battleBgmSoundId_ = ctx_->systems.sound->LoadOrCreateSilent(bgmPath);
    battleBgmVoiceHandle_ = ctx_->systems.sound->Play(
        battleBgmSoundId_,
        kBattleBgmBaseVolume * AppSceneServices::GetBgmVolume(), true);
}

void GameScene::StopBattleBgm() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr ||
        battleBgmVoiceHandle_ == SoundManager::kInvalidVoiceHandle) {
        return;
    }

    ctx_->systems.sound->Stop(battleBgmVoiceHandle_);
    battleBgmVoiceHandle_ = SoundManager::kInvalidVoiceHandle;
}

void GameScene::UpdateEnemyWarpSound(ActionKind previousKind,
                                     ActionStep previousStep,
                                     ActionKind currentKind,
                                     ActionStep currentStep) {
    if (currentKind != ActionKind::Warp ||
        (previousKind == currentKind && previousStep == currentStep)) {
        return;
    }

    if (currentStep == ActionStep::Start) {
        PlayEnemyWarpSound();
    }
}

void GameScene::PlayEnemyWarpSound() {
    if (!soundsLoaded_ || ctx_ == nullptr || ctx_->systems.sound == nullptr) {
        return;
    }

    if (warpSoundId_ == SoundManager::kInvalidSoundId) {
        return;
    }

    ctx_->systems.sound->Play(
        warpSoundId_, kEnemyWarpSoundVolume * AppSceneServices::GetSeVolume());
}

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    ctx_->rendering.dxCommon->ResetClearColor();
    InitializePostEffects(ctx);

    float aspect = static_cast<float>(ctx_->systems.winApp->GetWidth()) /
                   static_cast<float>(ctx_->systems.winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.UpdateMatrices();
    camera_.SetPerspectiveFovDeg(currentFovDeg_);

    DirectXCommon *dx = ctx_->rendering.dxCommon;
    ModelManager *model = ctx_->rendering.model;
    TextureManager *texture = ctx_->rendering.texture;

    uint32_t playerModel =
        model->Load(L"app/resources/models/player/player.gltf");
    uint32_t swordModel = model->Load(L"app/resources/models/player/sword.glb");
    uint32_t enemyModel = model->Load(L"app/resources/models/boss/boss.gltf");
    uint32_t bulletModel =
        model->Load(L"app/resources/models/boss/bullet.gltf");
    playerModelId_ = playerModel;
    swordModelId_ = swordModel;
    enemyModelId_ = enemyModel;
    bulletModelId_ = bulletModel;
    particleTextureId_ =
        texture->Load(L"app/resources/effects/particles/smoke.png");
    const XMFLOAT4 victoryHeat =
        DifficultyGaugeHeatColor(combatDifficulty_, 1.0f);
    victoryFireBillboardModelId_ = model->CreatePlane(
        particleTextureId_,
        MakeTransparentBillboardMaterial(
            particleTextureId_,
            {std::clamp(victoryHeat.x + 0.30f, 0.0f, 1.0f),
             std::clamp(victoryHeat.y + 0.24f, 0.0f, 1.0f),
             std::clamp(victoryHeat.z + 0.12f, 0.0f, 1.0f), 0.88f}));
    victorySmokeBillboardModelId_ = model->CreatePlane(
        particleTextureId_,
        MakeTransparentBillboardMaterial(particleTextureId_,
                                         {0.46f, 0.42f, 0.36f, 0.84f}));
    victoryDarkSmokeBillboardModelId_ = model->CreatePlane(
        particleTextureId_,
        MakeTransparentBillboardMaterial(particleTextureId_,
                                         {0.20f, 0.18f, 0.16f, 0.92f}));
    InitializeEnemyPhaseMaterialSet(texture, enemyPhaseMaterials_);
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
    ApplyEnemyPhaseMaterial(model, texture, enemyModel, enemyPhaseMaterials_,
                            BossPhase::Phase1, false, 1.0f);
    InitializeBattleArenaModels(model, arenaStoneTextureId);
    InitializeBattleActors(dx, texture);
    ResetBattleSessionState(model);
}

void GameScene::InitializeBattleArenaModels(ModelManager *model,
                                            uint32_t arenaStoneTextureId) {
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
        gSharedBattleModels.arenaTutorialSpokeModelId =
            model->CreatePlane(arenaStoneTextureId, [] {
                Material material = MakeArenaMaterial(
                    {0.014f, 0.25f, 0.22f, 1.0f}, false, 0.045f, 0.62f);
                material.cullMode =
                    static_cast<int32_t>(MaterialCullMode::None);
                return material;
            }());
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

    BindSharedBattleArenaModels();
}

void GameScene::BindSharedBattleArenaModels() {
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
    arenaTutorialSpokeModelId_ = gSharedBattleModels.arenaTutorialSpokeModelId;
    arenaInnerRingModelId_ = gSharedBattleModels.arenaInnerRingModelId;
    arenaOuterRingModelId_ = gSharedBattleModels.arenaOuterRingModelId;
    arenaColumnModelId_ = gSharedBattleModels.arenaColumnModelId;
    arenaColumnCapModelId_ = gSharedBattleModels.arenaColumnCapModelId;
    arenaDomeModelId_ = gSharedBattleModels.arenaDomeModelId;
    arenaBarrierRingModelId_ = gSharedBattleModels.arenaBarrierRingModelId;
    chargeWeakPointModelId_ = gSharedBattleModels.chargeWeakPointModelId;
}

void GameScene::InitializeBattleActors(DirectXCommon *dx,
                                       TextureManager *texture) {
    sparkParticles_.Initialize(dx, ctx_->rendering.srv, texture,
                               particleTextureId_, 9000);
    sparkParticles_.SetEmission(1, 1000.0f);
    sparkParticles_.SetEmitterRadius(0.08f);
    explosionParticles_.Initialize(dx, ctx_->rendering.srv, texture,
                                   particleTextureId_, 9000);
    explosionParticles_.SetEmission(1, 1000.0f);
    explosionParticles_.SetEmitterRadius(0.25f);
    smokeParticles_.Initialize(dx, ctx_->rendering.srv, texture,
                               particleTextureId_, 3500);
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

    player_.Initialize(playerModelId_, swordModelId_);
    player_.SetInputCalibration(inputCalibration_);
    enemy_.SetDifficulty(combatDifficulty_);
    enemy_.Initialize(enemyModelId_);
    ApplyBulletTextureToModel(GetCurrentEnemyTextureId());
    LoadBattleSounds(*ctx_);
    StartBattleBgm();
}

void GameScene::ResetBattleSessionState(ModelManager *model) {
    cameraYaw_ = 0.0f;
    cameraPitch_ = 0.0f;
    isLockOn_ = true;
    currentFovDeg_ = normalFovDeg_;
    targetFovDeg_ = normalFovDeg_;

    InitializeBattleCameraState(model);
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
    tutorialHandHoldTimer_ = 0.0f;
    tutorialRedWaitTimer_ = 0.0f;
    tutorialGreenCutTimer_ = 0.0f;
    tutorialStep_ = inputCalibration_.controlType == InputControlType::Hand
                        ? kTutorialStepHandPresence
                        : kTutorialStepLeftSword;
    tutorialPendingStep_ = -1;
    tutorialPendingAttackIndexIncrement_ = 0;
    tutorialOperationSlashCount_ = 0;
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
    enemyMeleeHitConsumed_ = false;
    previousCombatSlashStates_.fill(false);
    normalSlashHitConsumed_.fill(false);
    normalSlashRearmTimers_.fill(0.0f);
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
    arcaneLaserParticleTimer_ = 0.0f;
    handTrackingStartRequested_ = false;
    paused_ = false;
    pauseMenuIndex_ = 0;
    pauseExitFadeActive_ = false;
    pauseExitFadeTimer_ = 0.0f;
    pauseExitTarget_ = 0;
    player_.SetCameraSwordSlashSuppressed(false);
    InitializeHandTracking(*ctx_);
    hud_.Initialize(*ctx_);
    InitializeSceneMode();
}

void GameScene::InitializePostEffects(const SceneContext &ctx) {
    if (ctx.rendering.postEffectManager != nullptr) {
        ctx.rendering.postEffectManager->SetBaseProfile(PostProcessProfile{});
        PostEffectLayerDesc desc{};
        desc.priority = 10;
        desc.blendMode = PostEffectLayerBlendMode::Overlay;
        postEffectCinematicLayer_ =
            ctx.rendering.postEffectManager->CreateLayer(desc);
    }
    const bool suppressFeedback =
        titleDemoMode_ || backgroundOnlyMode_ || readyPreviewMode_;
    combatFeedback_.Initialize(
        PickValue(suppressFeedback, static_cast<PostEffectManager *>(nullptr),
                  ctx.rendering.postEffectManager));
}

void GameScene::LoadBattleSounds(const SceneContext &ctx) {
    if (titleDemoMode_ || ctx.systems.sound == nullptr) {
        return;
    }
    SoundManager *sound = ctx.systems.sound;
    slashSoundId_ = sound->Load(L"app/resources/audio/se/combat/se_Slash.wav");
    normalHitSlashSoundId_ =
        sound->Load(L"app/resources/audio/se/combat/se_MetalSound.wav");
    enemyReleaseSoundId_ =
        sound->Load(L"app/resources/audio/se/combat/se_Shot.wav");
    hitSoundId_ =
        sound->Load(L"app/resources/audio/se/combat/se_MetalSound.wav");
    counterSoundId_ =
        sound->Load(L"app/resources/audio/se/combat/se_MetalSound.wav");
    damageSoundId_ =
        sound->Load(L"app/resources/audio/se/combat/se_MetalSound.wav");
    counterSuccessSlashSoundId_ =
        sound->Load(L"app/resources/audio/se/combat/se_Slash.wav");
    mistimedCounterSoundId_ =
        sound->Load(L"app/resources/audio/se/combat/se_MetalSound.wav");
    mistimedCounterSoundStartSeconds_ =
        FindLoudestPlaybackSecond(sound, mistimedCounterSoundId_);
    explosionSoundId_ =
        sound->Load(L"app/resources/audio/se/combat/explosion_4.mp3");
    victoryExplosionSoundId_ =
        sound->Load(L"app/resources/audio/se/combat/explosion_4.mp3");
    warpSoundId_ =
        sound->LoadOrCreateSilent(L"app/resources/audio/se/movement/warp.mp3");
    soundsLoaded_ = true;
}

void GameScene::InitializeBattleCameraState(ModelManager *model) {
    const XMFLOAT3 &playerPos = player_.GetTransform().position;
    const XMFLOAT3 &enemyPos = enemy_.GetTransform().position;
    lockOnOrbitCameraPos_ = {playerPos.x, playerPos.y + lockOnOrbitHeight_,
                             playerPos.z - lockOnOrbitRadius_};
    lockOnLookAt_ =
        PickValue(playerViewCamera_,
                  XMFLOAT3{enemyPos.x, enemyPos.y + playerViewLockOnLookHeight_,
                           enemyPos.z},
                  XMFLOAT3{playerPos.x * lockOnLookPlayerWeight_ +
                               enemyPos.x * lockOnLookEnemyWeight_,
                           (playerPos.y + cameraLookHeight_) * 0.52f +
                               (enemyPos.y + 1.30f) * 0.48f,
                           playerPos.z * lockOnLookPlayerWeight_ +
                               enemyPos.z * lockOnLookEnemyWeight_});
    Model *playerModelData = model->GetModel(playerModelId_);
    if (playerModelData != nullptr && !playerModelData->animations.empty()) {
        model->PlayAnimation(playerModelId_, playerModelData->currentAnimation,
                             true);
    }
}

void GameScene::InitializeHandTracking(const SceneContext &ctx) {
    if (inputCalibration_.controlType != InputControlType::Hand) {
        return;
    }
    if (AppSceneServices::HasHandTrackingStart()) {
        handTrackingStartRequested_ =
            AppSceneServices::RequestHandTrackingStart();
    }
    if (handTrackingStartRequested_) {
        cameraPreviewReceiver_.Initialize(ctx.rendering.texture,
                                          kHandCameraPreviewPort);
    }
}

void GameScene::InitializeSceneMode() {
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
    if (UpdatePreviewOrTutorialMode(baseDeltaTime) ||
        UpdatePauseOrIntro(input, baseDeltaTime)) {
        return;
    }
#ifdef _DEBUG
    UpdateDebugKeys(input);
#endif
    if (UpdateBattleSequenceMode(input, baseDeltaTime)) {
        return;
    }
    if (battleIntroRevealEmitted_) {
        ApplyReleasedClearColor(ctx_->rendering.dxCommon);
    } else {
        ctx_->rendering.dxCommon->ResetClearColor();
    }
    phaseTransitionWasActive_ = false;
    phaseTransitionReleaseEmitted_ = false;
    combatFeedback_.Update(baseDeltaTime, sceneLightTime_);
    if (UpdateBladeClashFinishMode(input, baseDeltaTime)) {
        return;
    }

    const GameplayTiming timing = ComputeGameplayTiming(baseDeltaTime);
    UpdateCamera(input);
    UpdateGameplayPlayer(input, timing, baseDeltaTime);
    UpdateSwordVfx(timing.gameplayDeltaTime);
    UpdateHandCameraPreview(baseDeltaTime);
    hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(), enemy_.GetMaxHP());
    sceneLightTime_ += baseDeltaTime;
    battleElapsedTime_ += timing.gameplayDeltaTime;
    UpdateGameplayEnemy(timing.enemyDeltaTime);
    UpdateSceneLighting();
    UpdateGameplayEnemyAnimation(timing.enemyDeltaTime, baseDeltaTime);
    ApplyEnemyProceduralAnimation();
    UpdateBattleCamera();
    camera_.UpdateMatrices();
    UpdateGameplayCombat(timing.gameplayDeltaTime);
    if (UpdateGameplayEndState(baseDeltaTime)) {
        return;
    }
    UpdateGameplayParticles(timing.gameplayDeltaTime);
}

bool GameScene::UpdatePreviewOrTutorialMode(float baseDeltaTime) {
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
        return true;
    }
    if (backgroundOnlyMode_) {
        sceneLightTime_ += baseDeltaTime;
        backgroundBuildTimer_ =
            (std::min)(backgroundBuildTimer_ + baseDeltaTime, 1.2f);
        UpdateSceneLighting();
        UpdateBackgroundCamera(baseDeltaTime);
        return true;
    }
    if (tutorialMode_) {
        UpdateTutorial(baseDeltaTime);
        return true;
    }

    return false;
}

bool GameScene::UpdatePauseOrIntro(Input *input, float baseDeltaTime) {
    if (paused_) {
        UpdatePauseMenu(input);
        return true;
    }
    if (battleIntroActive_) {
        if (input != nullptr &&
            (input->IsKeyTrigger(DIK_SPACE) ||
             input->IsKeyTrigger(DIK_ESCAPE) || input->IsKeyTrigger(DIK_TAB))) {
            FinishBattleIntro();
            return true;
        }
        UpdateBattleIntro(baseDeltaTime);
        return true;
    }
    if (input != nullptr &&
        (input->IsKeyTrigger(DIK_ESCAPE) || input->IsKeyTrigger(DIK_TAB))) {
        OpenPauseMenu();
        return true;
    }

    return false;
}

bool GameScene::UpdateBattleSequenceMode(Input *input, float baseDeltaTime) {
    (void)input;
    if (victorySequenceActive_) {
        sceneLightTime_ += baseDeltaTime;
        combatFeedback_.Update(baseDeltaTime, sceneLightTime_);
        hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(), enemy_.GetMaxHP());
        UpdateVictorySequence(baseDeltaTime);
        const float victoryPoseRatio =
            std::clamp((victorySequenceTimer_ - kVictoryEnemyFallStart) /
                           kVictoryEnemyFallDuration,
                       0.0f, 1.0f);
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
        return true;
    }
    if (defeatSequenceActive_) {
        sceneLightTime_ += baseDeltaTime;
        combatFeedback_.Update(baseDeltaTime, sceneLightTime_);
        hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(), enemy_.GetMaxHP());
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
        return true;
    }
    if (enemy_.IsPhaseTransitionActive() && !bladeClashFinishActive_) {
        UpdatePhaseTransitionCinematic(baseDeltaTime);
        return true;
    }

    return false;
}

bool GameScene::UpdateBladeClashFinishMode(Input *input, float baseDeltaTime) {
    if (bladeClashFinishActive_) {
        sceneLightTime_ += baseDeltaTime;
        hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(), enemy_.GetMaxHP());
        UpdateBladeClashFinish(baseDeltaTime);
        UpdateCamera(input);
        const float playerAnimScale =
            bladeClashFinishPlayerWon_ ? 0.08f : 0.04f;
        ctx_->rendering.model->UpdateAnimation(playerModelId_,
                                               baseDeltaTime * playerAnimScale);
        UpdateBladeClashEnemyAnimation(baseDeltaTime);
        UpdateSwordVfx(baseDeltaTime);
        UpdateSceneLighting();
        ApplyEnemyProceduralAnimation();
        UpdateBattleCamera();
        camera_.UpdateMatrices();
        sparkParticles_.Update(baseDeltaTime);
        explosionParticles_.Update(baseDeltaTime);
        smokeParticles_.Update(baseDeltaTime);
        swordFlashParticles_.Update(baseDeltaTime);
        return true;
    }

    return false;
}

GameScene::GameplayTiming
GameScene::ComputeGameplayTiming(float baseDeltaTime) const {
    GameplayTiming timing{};
    const float gameplayTimeScale = ComputeGameplayTimeScale();
    timing.gameplayDeltaTime = baseDeltaTime * gameplayTimeScale;
    timing.cataclysmProjectilePreviewSlow =
        arcaneProjectileVolleyActive_ && arcaneProjectileVolleyCataclysm_ &&
        arcaneProjectileVolleyShotsFired_ == 0;
    timing.playerDeltaTime =
        timing.gameplayDeltaTime * (timing.cataclysmProjectilePreviewSlow
                                        ? kCataclysmPreviewPlayerSlowMultiplier
                                        : 1.0f);
    timing.enemyDeltaTime =
        bladeClashActive_ ? 0.0f
        : counterCinematicActive_
            ? (baseDeltaTime * (std::min)(counterTimeScale_, gameplayTimeScale))
            : timing.gameplayDeltaTime;
    if (!enemy_.IsPhaseTransitionActive() && !counterCinematicActive_ &&
        !bladeClashActive_) {
        timing.enemyDeltaTime *= 1.0f + 0.08f * GetHighDifficultyPressure();
    }
    if (enemy_.GetBossPhase() == BossPhase::Phase3 &&
        !enemy_.IsPhaseTransitionActive() && !counterCinematicActive_ &&
        !bladeClashActive_) {
        timing.enemyDeltaTime *= 1.08f;
    }
    return timing;
}

void GameScene::UpdateGameplayPlayer(Input *input, const GameplayTiming &timing,
                                     float baseDeltaTime) {
    ctx_->rendering.model->UpdateAnimation(playerModelId_,
                                           timing.playerDeltaTime);

    if (titleDemoMode_) {
        player_.SetMovementSpeedMultiplier(1.0f);
        player_.UpdateDemo(timing.playerDeltaTime,
                           enemy_.GetTransform().position);
    } else {
        const bool lockPlayerForFarWarpSlash =
            enemy_.ShouldLockPlayerForFarWarpSlash();
        const ActionKind playerMoveEnemyAction = enemy_.GetActionKind();
        const bool slowPlayerForRangedAttack =
            playerMoveEnemyAction == ActionKind::ArcaneLaser ||
            playerMoveEnemyAction == ActionKind::CataclysmLaser;
        player_.SetMovementSpeedMultiplier(
            timing.cataclysmProjectilePreviewSlow
                ? kCataclysmPreviewPlayerSlowMultiplier
                : (slowPlayerForRangedAttack ? kRangedAttackPlayerMoveMultiplier
                                             : 1.0f));
        player_.SetHandPostSlashCooldownEnabled(!bladeClashActive_);
        player_.Update(input, timing.playerDeltaTime,
                       enemy_.GetTransform().position, cameraYaw_,
                       baseDeltaTime, false, lockPlayerForFarWarpSlash);
        const Player::ChargedShot chargedShot = player_.ConsumeChargedShot();
        if (chargedShot.fired) {
            playerChargedProjectile_.active = true;
            playerChargedProjectile_.position = chargedShot.origin;
            playerChargedProjectile_.direction = chargedShot.direction;
            playerChargedProjectile_.velocity = {
                chargedShot.direction.x * kPlayerChargedProjectileSpeed,
                chargedShot.direction.y * kPlayerChargedProjectileSpeed,
                chargedShot.direction.z * kPlayerChargedProjectileSpeed};
            playerChargedProjectile_.age = 0.0f;
            playerChargedProjectile_.life = 1.6f;
            playerChargedProjectile_.damage = kPlayerChargedProjectileDamage;
            playerChargedProjectile_.hitConsumed = false;
            EmitParticleBurst(
                swordFlashParticles_, playerChargedProjectile_.position, 74,
                0.34f, AppParticleBurstStyle::Flash,
                {1.0f, 0.86f, 0.28f, 0.96f}, chargedShot.direction, 1.28f);
            if (soundsLoaded_ && ctx_->systems.sound != nullptr) {
                ctx_->systems.sound->Play(enemyReleaseSoundId_,
                                          kEnemyReleaseSoundVolume *
                                              AppSceneServices::GetSeVolume());
            }
        }
    }
}

void GameScene::UpdateGameplayEnemy(float enemyDeltaTime) {
    const ActionKind previousEnemyActionKind = enemy_.GetActionKind();
    const ActionStep previousEnemyActionStep = enemy_.GetActionStep();
    enemy_.Update(BuildPlayerCombatObservation(), enemyDeltaTime);
    const ActionKind currentEnemyActionKind = enemy_.GetActionKind();
    const ActionStep currentEnemyActionStep = enemy_.GetActionStep();
    if (currentEnemyActionKind != previousEnemyActionKind ||
        currentEnemyActionStep != previousEnemyActionStep) {
        UpdateEnemyWarpSound(previousEnemyActionKind, previousEnemyActionStep,
                             currentEnemyActionKind, currentEnemyActionStep);
        if (IsEnemyBasicSlashAction(currentEnemyActionKind) &&
            currentEnemyActionStep == ActionStep::Active) {
            swordSlashArcRenderer_.ClearDirectionCueLines();
        }
        EmitEnemyActionParticles(currentEnemyActionKind,
                                 currentEnemyActionStep);
        const bool isEnemyAttackRelease =
            IsEnemyAttackAction(currentEnemyActionKind) &&
            currentEnemyActionStep == ActionStep::Active;
        if (isEnemyAttackRelease) {
            if (soundsLoaded_ && ctx_->systems.sound != nullptr) {
                ctx_->systems.sound->Play(enemyReleaseSoundId_,
                                          kEnemyReleaseSoundVolume *
                                              AppSceneServices::GetSeVolume());
            }
            if (IsEnemyRangedAction(currentEnemyActionKind)) {
                BeginArcaneProjectileVolley();
            }
        }
    }
}

void GameScene::UpdateGameplayEnemyAnimation(float enemyDeltaTime,
                                             float baseDeltaTime) {
    SyncEnemyAnimation();
    SetEnemyAnimationFrozen(counterCinematicActive_);
    if (!enemyAnimationFrozen_) {
        float enemyAnimationDeltaTime = enemyDeltaTime;
        const ActionKind enemyActionKind = enemy_.GetActionKind();
        const ActionStep enemyActionStep = enemy_.GetActionStep();
        const bool bladeClashGuardWaiting =
            (enemyActionKind == ActionKind::BladeClash &&
             (enemyActionStep == ActionStep::Charge ||
              enemyActionStep == ActionStep::Active));
        if (bladeClashActive_ || bladeClashGuardWaiting) {
            UpdateBladeClashEnemyAnimation(baseDeltaTime);
            enemyAnimationDeltaTime = 0.0f;
        } else if (IsEnemyAttackAction(enemyActionKind)) {
            const float enemyActionTimer =
                enemy_.GetActionTimerForPresentation();
            const bool farWarpSlashStance =
                enemy_.IsFarWarpSlashActive() &&
                enemyActionStep == ActionStep::Charge &&
                enemyActionTimer >= 0.08f;
            if (farWarpSlashStance) {
                enemyAnimationDeltaTime *= 0.025f;
            } else if (IsChargeStanceSettled(enemyActionKind, enemyActionStep,
                                             enemyActionTimer)) {
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
}

void GameScene::UpdateGameplayCombat(float gameplayDeltaTime) {
    player_.SetCameraSwordSlashSuppressed(false);
    UpdateArcaneProjectile(gameplayDeltaTime);
    UpdatePlayerChargedProjectile(gameplayDeltaTime);
    UpdateArcaneProjectileVolley(gameplayDeltaTime);
    mistimedCounterSlashThisFrame_ = false;
    counterSuccessSlashThisFrame_ = false;
    UpdateCombat(gameplayDeltaTime);
    if (soundsLoaded_ && ctx_->systems.sound != nullptr) {
        const auto slashStates = player_.GetSwordSlashStates();
        for (size_t i = 0; i < slashStates.size(); ++i) {
            if (slashStates[i] && !previousSwordSoundStates_[i] &&
                !mistimedCounterSlashThisFrame_ &&
                !counterSuccessSlashThisFrame_) {
                ctx_->systems.sound->Play(slashSoundId_,
                                          kSlashSoundVolume *
                                              AppSceneServices::GetSeVolume());
            }
        }
        previousSwordSoundStates_ = slashStates;
    }
}

bool GameScene::UpdateGameplayEndState(float baseDeltaTime) {
    hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(), enemy_.GetMaxHP());
    if (enemy_.IsPhaseTransitionActive() && !phaseTransitionWasActive_) {
        ClearSwordVfx();
        phaseTransitionWasActive_ = true;
        phaseTransitionReleaseEmitted_ = false;
        phaseTransitionLoopTimer_ = 0.0f;
        EmitPhaseTransitionStartEffects();
    }
    if (!battleResultRequested_ && !bladeClashFinishActive_) {
        if (enemy_.GetHP() <= 0.0f) {
            BeginVictorySequence();
            return true;
        }
        if (player_.GetHP() <= 0.0f) {
            BeginDefeatSequence();
            return true;
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

    return false;
}

void GameScene::UpdateGameplayParticles(float gameplayDeltaTime) {
    EmitArcaneLaserParticles(gameplayDeltaTime);
    EmitEnemyCueParticles(gameplayDeltaTime);
    sparkParticles_.Update(gameplayDeltaTime);
    explosionParticles_.Update(gameplayDeltaTime);
    smokeParticles_.Update(gameplayDeltaTime);
    swordFlashParticles_.Update(gameplayDeltaTime);
}

#ifdef _DEBUG
void GameScene::UpdateDebugKeys(Input *input) {
    if (input == nullptr || titleDemoMode_ || backgroundOnlyMode_ ||
        readyPreviewMode_ || tutorialMode_) {
        return;
    }

    if (input->IsKeyTrigger(DIK_F1)) {
        debugPlayerInvincible_ = !debugPlayerInvincible_;
    }

    if (input->IsKeyTrigger(DIK_F2)) {
        BossPhase nextPhase = BossPhase::Phase1;
        switch (enemy_.GetBossPhase()) {
        case BossPhase::Phase1:
            nextPhase = BossPhase::Phase2;
            break;
        case BossPhase::Phase2:
            nextPhase = BossPhase::Phase3;
            break;
        case BossPhase::Phase3:
            nextPhase = BossPhase::Phase1;
            break;
        }

        enemy_.DebugForceBossPhase(nextPhase, true);
        phaseTransitionWasActive_ = false;
        phaseTransitionReleaseEmitted_ = false;
        phaseTransitionLoopTimer_ = 0.0f;
        bladeClashActive_ = false;
        bladeClashFinishActive_ = false;
    }
}
#endif

bool GameScene::IsTutorialOperationStepComplete() const {
    return tutorialOperationSlashCount_ >= kTutorialOperationRequiredSlashes;
}

void GameScene::AdvanceTutorialOperationStep() {
    tutorialPendingStep_ = tutorialStep_ + 1;
    tutorialPendingAttackIndexIncrement_ = 0;
    tutorialSuccessTimer_ = 2.0f;
    tutorialMissTimer_ = 0.0f;
    tutorialExcellentTimer_ = 0.0f;
    tutorialHandHoldTimer_ = 0.0f;
    tutorialRedWaitTimer_ = 0.0f;
    tutorialGreenCutTimer_ = 0.0f;
    tutorialAttackDelay_ =
        tutorialPendingStep_ >= kTutorialStepRedSmash ? 2.0f : 0.0f;
    tutorialAttackInProgress_ = false;
    tutorialCounterSuccess_ = false;
    enemy_.ResetTutorialState();
    enemy_.SetTutorialPosition({0.0f, 0.0f, 3.10f});
    enemy_.FaceTargetImmediately(player_.GetTransform().position);
}

void GameScene::UpdateTutorial(float deltaTime) {
    Input *input = ctx_->systems.input;
    if (UpdateTutorialExit(deltaTime) || HandleTutorialNavigationInput(input)) {
        return;
    }
    UpdateTutorialFrame(input, deltaTime);
    UpdateTutorialTimers(deltaTime);
    if (UpdateTutorialBasicStep(deltaTime)) {
        return;
    }

    BeginTutorialAttackIfReady(deltaTime);
    const auto slashStates = player_.GetSwordSlashStates();
    const bool slashStarted = DidTutorialSlashStart(slashStates);
    HandleTutorialRedEarlySlash(slashStates, slashStarted);
    UpdateTutorialEnemyAttack(deltaTime, slashStates);
    UpdateTutorialEnemyAnimation(deltaTime);
    UpdateTutorialCombat(deltaTime, slashStates);
    FinishTutorialAttackState(deltaTime, slashStates);
    EmitEnemyCueParticles(deltaTime);
    sparkParticles_.Update(deltaTime);
    explosionParticles_.Update(deltaTime);
    smokeParticles_.Update(deltaTime);
    swordFlashParticles_.Update(deltaTime);
}

bool GameScene::UpdateTutorialExit(float deltaTime) {
    if (tutorialExitRequested_) {
        tutorialExitFadeTimer_ = (std::min)(tutorialExitFadeTimer_ + deltaTime,
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
        return true;
    }

    return false;
}

bool GameScene::HandleTutorialNavigationInput(Input *input) {
    if (input != nullptr) {
        const bool excellentVisible =
            tutorialStep_ < kTutorialStepPractice &&
            (tutorialSuccessTimer_ > 0.0f || tutorialExcellentTimer_ > 0.0f);
        bool skippedExcellent = false;
        if (excellentVisible && input->IsKeyTrigger(DIK_SPACE)) {
            tutorialSuccessTimer_ = 0.0f;
            tutorialExcellentTimer_ = 0.0f;
            skippedExcellent = true;
        }
        if (!skippedExcellent && tutorialStep_ >= kTutorialStepPractice &&
            input->IsKeyTrigger(DIK_SPACE)) {
            tutorialExitRequested_ = true;
            tutorialExitToSelect_ = false;
            tutorialExitFadeTimer_ = 0.0f;
            return true;
        }
        if (input->IsKeyTrigger(DIK_ESCAPE)) {
            tutorialExitRequested_ = true;
            tutorialExitToSelect_ = true;
            tutorialExitFadeTimer_ = 0.0f;
            return true;
        }
    }

    return false;
}

void GameScene::UpdateTutorialFrame(Input *input, float deltaTime) {
    tutorialTimer_ += deltaTime;
    tutorialEntryFadeTimer_ = (std::min)(tutorialEntryFadeTimer_ + deltaTime,
                                         kTutorialEntryFadeDuration);
    sceneLightTime_ += deltaTime;
    backgroundBuildTimer_ = 1.2f;
    combatFeedback_.Update(deltaTime, sceneLightTime_);
    UpdateBattlePostProcessState(deltaTime);

    UpdateCamera(input);
    player_.SetCameraSwordSlashSuppressed(tutorialStep_ ==
                                          kTutorialStepHandPresence);
    player_.Update(input, deltaTime, enemy_.GetTransform().position, cameraYaw_,
                   deltaTime, false);
    player_.LockPosition({0.0f, 0.0f, 0.0f});
    player_.SetCameraSwordSlashSuppressed(false);
    UpdateSwordVfx(deltaTime);
    UpdateHandCameraPreview(deltaTime);
}

void GameScene::UpdateTutorialTimers(float deltaTime) {
    tutorialSuccessTimer_ = (std::max)(0.0f, tutorialSuccessTimer_ - deltaTime);
    tutorialMissTimer_ = (std::max)(0.0f, tutorialMissTimer_ - deltaTime);
    tutorialExcellentTimer_ =
        (std::max)(0.0f, tutorialExcellentTimer_ - deltaTime);
    if (tutorialPendingStep_ >= 0 && tutorialSuccessTimer_ <= 0.0f &&
        tutorialExcellentTimer_ <= 0.0f) {
        tutorialStep_ = tutorialPendingStep_;
        tutorialAttackIndex_ += tutorialPendingAttackIndexIncrement_;
        tutorialPendingStep_ = -1;
        tutorialPendingAttackIndexIncrement_ = 0;
        tutorialOperationSlashCount_ = 0;
        tutorialHandHoldTimer_ = 0.0f;
        previousCombatSlashStates_.fill(false);
    }
}

bool GameScene::UpdateTutorialBasicStep(float deltaTime) {
    if (tutorialStep_ == kTutorialStepHandPresence) {
        UpdateTutorialHandPresence(deltaTime);
        return true;
    }
    if (tutorialStep_ < kTutorialStepRedSmash) {
        UpdateTutorialOperationStep(deltaTime);
        return true;
    }
    return false;
}

void GameScene::UpdateTutorialHandPresence(float deltaTime) {
    if (tutorialPendingStep_ < 0 && tutorialSuccessTimer_ <= 0.0f &&
        tutorialExcellentTimer_ <= 0.0f) {
        const bool handVisible =
            player_.HasFreshHandInput() &&
            (player_.IsHandActive(0) || player_.IsHandActive(1));
        tutorialHandHoldTimer_ =
            handVisible ? (std::min)(tutorialHandHoldTimer_ + deltaTime,
                                     kTutorialHandPresenceRequiredSeconds)
                        : 0.0f;
        if (tutorialHandHoldTimer_ >= kTutorialHandPresenceRequiredSeconds) {
            tutorialPendingStep_ = kTutorialStepLeftSword;
            tutorialPendingAttackIndexIncrement_ = 0;
            tutorialExcellentTimer_ = 2.0f;
            tutorialSuccessTimer_ = 0.0f;
            tutorialMissTimer_ = 0.0f;
            tutorialRedWaitTimer_ = 0.0f;
            tutorialGreenCutTimer_ = 0.0f;
            tutorialAttackDelay_ = 0.0f;
            previousCombatSlashStates_.fill(false);
        }
    }

    UpdateTutorialIdlePresentation(deltaTime);
}

void GameScene::UpdateTutorialOperationStep(float deltaTime) {
    if (tutorialPendingStep_ < 0 && tutorialSuccessTimer_ <= 0.0f &&
        tutorialExcellentTimer_ <= 0.0f) {
        const auto swords = player_.GetSwords();
        const auto slashStates = player_.GetSwordSlashStates();
        const size_t targetSword =
            tutorialStep_ == kTutorialStepLeftSword ? 0u : 1u;
        if (targetSword < slashStates.size() &&
            targetSword < previousCombatSlashStates_.size() &&
            targetSword < swords.size() && swords[targetSword] != nullptr &&
            slashStates[targetSword] &&
            !previousCombatSlashStates_[targetSword]) {
            ++tutorialOperationSlashCount_;
        }
        previousCombatSlashStates_ = slashStates;
    }

    if (tutorialPendingStep_ < 0 && tutorialSuccessTimer_ <= 0.0f &&
        tutorialExcellentTimer_ <= 0.0f && IsTutorialOperationStepComplete()) {
        AdvanceTutorialOperationStep();
    }

    UpdateTutorialIdlePresentation(deltaTime);
}

void GameScene::UpdateTutorialIdlePresentation(float deltaTime) {
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
}

ActionKind GameScene::GetTutorialAttackKind() const {
    if (tutorialStep_ == kTutorialStepRedSmash ||
        tutorialStep_ == kTutorialStepGreenSmash) {
        return ActionKind::Smash;
    }
    if (tutorialStep_ == kTutorialStepRedSweep ||
        tutorialStep_ == kTutorialStepGreenSweep) {
        return ActionKind::Sweep;
    }
    return tutorialAttackIndex_ % 2 == 0 ? ActionKind::Smash
                                         : ActionKind::Sweep;
}

bool GameScene::IsTutorialRedWaitStep() const {
    return tutorialStep_ == kTutorialStepRedSmash ||
           tutorialStep_ == kTutorialStepRedSweep;
}

bool GameScene::IsTutorialGreenCutStep() const {
    return tutorialStep_ == kTutorialStepGreenSmash ||
           tutorialStep_ == kTutorialStepGreenSweep;
}

void GameScene::BeginTutorialAttackIfReady(float deltaTime) {
    if (!tutorialAttackInProgress_ && tutorialExcellentTimer_ <= 0.0f) {
        tutorialAttackDelay_ -= deltaTime;
        if (tutorialAttackDelay_ <= 0.0f) {
            enemy_.ResetTutorialState();
            enemy_.SetTutorialPosition({0.0f, 0.0f, 3.10f});
            enemy_.FaceTargetImmediately(player_.GetTransform().position);
            const ActionKind attackKind = GetTutorialAttackKind();
            enemy_.BeginTutorialAttack(attackKind);
            EmitEnemyActionParticles(attackKind, ActionStep::Charge);
            tutorialAttackInProgress_ = true;
            tutorialCounterSuccess_ = false;
            tutorialRedWaitTimer_ = IsTutorialRedWaitStep() ? 3.0f : 0.0f;
            tutorialGreenCutTimer_ = 0.0f;
            counterCinematicActive_ = false;
            counterCinematicTimer_ = 0.0f;
            SetEnemyAnimationFrozen(false);
        }
    }
}

bool GameScene::DidTutorialSlashStart(
    const std::array<bool, 2> &slashStates) const {
    for (size_t i = 0; i < slashStates.size(); ++i) {
        if (slashStates[i] && !previousCombatSlashStates_[i]) {
            return true;
        }
    }
    return false;
}

void GameScene::HandleTutorialRedEarlySlash(
    const std::array<bool, 2> &swordSlashStatesBeforeCombat,
    bool tutorialSlashStarted) {
    if (tutorialAttackInProgress_ && IsTutorialRedWaitStep() &&
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
}

void GameScene::UpdateTutorialEnemyAttack(
    float deltaTime, const std::array<bool, 2> &swordSlashStatesBeforeCombat) {
    const ActionKind previousEnemyActionKind = enemy_.GetActionKind();
    const ActionStep previousEnemyActionStep = enemy_.GetActionStep();
    const float enemyTutorialDeltaTime =
        ComputeTutorialEnemyDeltaTime(deltaTime);
    enemy_.UpdateTutorial(BuildPlayerCombatObservation(),
                          enemyTutorialDeltaTime);
    const ActionKind currentEnemyActionKind = enemy_.GetActionKind();
    const ActionStep currentEnemyActionStep = enemy_.GetActionStep();
    const bool greenCueVisible = IsTutorialGreenCutStep() &&
                                 !enemyRedPunishUncounterable_ &&
                                 enemy_.GetReleaseAnticipationRatio() > 0.0f;
    if (tutorialAttackInProgress_ && greenCueVisible &&
        tutorialGreenCutTimer_ <= 0.0f) {
        tutorialGreenCutTimer_ = 3.0f;
    }
    UpdateTutorialRedWait(deltaTime, swordSlashStatesBeforeCombat);
    HandleTutorialEnemyActionTransition(
        previousEnemyActionKind, previousEnemyActionStep,
        currentEnemyActionKind, currentEnemyActionStep);
}

float GameScene::ComputeTutorialEnemyDeltaTime(float deltaTime) const {
    const bool releaseCueVisible = enemy_.GetReleaseAnticipationRatio() > 0.0f;
    const bool holdRedCue = IsTutorialRedWaitStep() &&
                            tutorialRedWaitTimer_ > 0.0f && releaseCueVisible;
    const bool holdGreenCue = IsTutorialGreenCutStep() &&
                              tutorialGreenCutTimer_ > 0.0f &&
                              releaseCueVisible;
    if (tutorialExcellentTimer_ > 0.0f || holdGreenCue || holdRedCue) {
        return 0.0f;
    }
    return IsTutorialRedWaitStep() ? deltaTime * 0.65f : deltaTime;
}

void GameScene::UpdateTutorialRedWait(
    float deltaTime, const std::array<bool, 2> &swordSlashStatesBeforeCombat) {
    if (!tutorialAttackInProgress_ || !IsTutorialRedWaitStep()) {
        return;
    }
    tutorialRedWaitTimer_ = (std::max)(0.0f, tutorialRedWaitTimer_ - deltaTime);
    if (tutorialRedWaitTimer_ > 0.0f) {
        return;
    }
    tutorialExcellentTimer_ = 2.0f;
    if (tutorialStep_ == kTutorialStepRedSmash) {
        tutorialPendingStep_ = kTutorialStepGreenSmash;
    } else if (tutorialStep_ == kTutorialStepRedSweep) {
        tutorialPendingStep_ = kTutorialStepGreenSweep;
    }
    tutorialPendingAttackIndexIncrement_ = 0;
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

void GameScene::HandleTutorialEnemyActionTransition(ActionKind previousKind,
                                                    ActionStep previousStep,
                                                    ActionKind currentKind,
                                                    ActionStep currentStep) {
    if (currentKind == previousKind && currentStep == previousStep) {
        return;
    }
    UpdateEnemyWarpSound(previousKind, previousStep, currentKind, currentStep);
    const bool basicSlashReleased = IsEnemyBasicSlashAction(currentKind) &&
                                    currentStep == ActionStep::Active;
    if (basicSlashReleased) {
        swordSlashArcRenderer_.ClearDirectionCueLines();
    }
    EmitEnemyActionParticles(currentKind, currentStep);
    if (basicSlashReleased && soundsLoaded_ && ctx_->systems.sound != nullptr) {
        ctx_->systems.sound->Play(enemyReleaseSoundId_,
                                  kEnemyReleaseSoundVolume *
                                      AppSceneServices::GetSeVolume());
    }
}

void GameScene::UpdateTutorialEnemyAnimation(float deltaTime) {
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
            const float enemyActionTimer =
                enemy_.GetActionTimerForPresentation();
            const bool farWarpSlashStance =
                enemy_.IsFarWarpSlashActive() &&
                enemyActionStep == ActionStep::Charge &&
                enemyActionTimer >= 0.08f;
            if (farWarpSlashStance) {
                enemyAnimationDeltaTime *= 0.025f;
            } else if (IsChargeStanceSettled(enemyActionKind, enemyActionStep,
                                             enemyActionTimer)) {
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
}

void GameScene::UpdateTutorialCombat(
    float deltaTime, const std::array<bool, 2> &swordSlashStatesBeforeCombat) {
    const bool wasCounterActive = counterCinematicActive_;
    if (IsTutorialRedWaitStep() || tutorialExcellentTimer_ > 0.0f ||
        (IsTutorialGreenCutStep() && tutorialGreenCutTimer_ <= 0.0f)) {
        previousCombatSlashStates_ = swordSlashStatesBeforeCombat;
    } else {
        UpdateCombat(deltaTime);
    }
    if (!tutorialCounterSuccess_ && !wasCounterActive &&
        counterCinematicActive_) {
        tutorialCounterSuccess_ = true;
        tutorialExcellentTimer_ = IsTutorialGreenCutStep() ? 2.0f : 0.0f;
        tutorialSuccessTimer_ = IsTutorialGreenCutStep() ? 0.0f : 2.0f;
        tutorialMissTimer_ = 0.0f;
        tutorialGreenCutTimer_ = 0.0f;
        if (tutorialStep_ == kTutorialStepGreenSmash) {
            tutorialPendingStep_ = kTutorialStepRedSweep;
        } else if (tutorialStep_ == kTutorialStepGreenSweep) {
            tutorialPendingStep_ = kTutorialStepPractice;
        }
        tutorialPendingAttackIndexIncrement_ = 1;
    }
}

void GameScene::FinishTutorialAttackState(
    float deltaTime, const std::array<bool, 2> &swordSlashStatesBeforeCombat) {
    UpdateTutorialGreenCutTimeout(deltaTime, swordSlashStatesBeforeCombat);
    ResolveFinishedTutorialAttack();
    UpdateTutorialCounterCinematic(deltaTime);
}

void GameScene::UpdateTutorialGreenCutTimeout(
    float deltaTime, const std::array<bool, 2> &swordSlashStatesBeforeCombat) {
    if (tutorialAttackInProgress_ && IsTutorialGreenCutStep() &&
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
}

void GameScene::ResolveFinishedTutorialAttack() {
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
}

void GameScene::UpdateTutorialCounterCinematic(float deltaTime) {
    if (counterCinematicActive_) {
        counterCinematicTimer_ -= deltaTime;
        if (counterCinematicTimer_ <= 0.0f) {
            counterCinematicTimer_ = 0.0f;
            counterCinematicActive_ = false;
            enemy_.FinishCounterRecoil();
            SetEnemyAnimationFrozen(false);
        }
    }
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
        ApplyEnemyPhaseMaterials();
        smokeParticles_.DispatchPendingUpdate();
        sparkParticles_.DispatchPendingUpdate();
        explosionParticles_.DispatchPendingUpdate();
        ctx_->rendering.model->PreDraw();
        DrawArena();
        enemy_.Draw(ctx_->rendering.model, camera_, 1.28f);
        ctx_->rendering.model->PostDraw();
        smokeParticles_.Draw(camera_);
        sparkParticles_.Draw(camera_);
        explosionParticles_.Draw(camera_);
        return;
    }

    ctx_->rendering.model->PrepareSkinning({playerModelId_, enemyModelId_});
    ApplyEnemyPhaseMaterials();
    smokeParticles_.DispatchPendingUpdate();
    sparkParticles_.DispatchPendingUpdate();
    explosionParticles_.DispatchPendingUpdate();
    swordFlashParticles_.DispatchPendingUpdate();

    ctx_->rendering.model->PreDraw();
    const bool bladeClashWinFinish =
        bladeClashFinishActive_ && bladeClashFinishPlayerWon_;
    DrawArena();
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
    DrawVictoryEnemyVanishExplosionBillboards();
    DrawArcaneProjectile();
    DrawPlayerChargedProjectile();
    ctx_->rendering.model->PostDraw();
    swordTrailRenderer_.Draw(camera_);
    swordSlashArcRenderer_.Draw(camera_);

    if (!bladeClashWinFinish) {
        smokeParticles_.Draw(camera_);
        sparkParticles_.Draw(camera_);
        explosionParticles_.Draw(camera_);
        swordFlashParticles_.Draw(camera_);
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
    if (ctx_ != nullptr && ctx_->rendering.postEffectManager != nullptr) {
        pauseSavedPostProcess_ =
            ctx_->rendering.postEffectManager->GetComposedProfile();
        pausePostProcessSaved_ = true;
        ctx_->rendering.postEffectManager->ClearLayers();
        ctx_->rendering.postEffectManager->SetBaseProfile(PostProcessProfile{});
    }
    paused_ = true;
    pauseMenuIndex_ = 0;
    pauseExitFadeActive_ = false;
    pauseExitFadeTimer_ = 0.0f;
    pauseExitTarget_ = 0;
    AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
}

void GameScene::ClosePauseMenu() {
    paused_ = false;
    pauseExitFadeActive_ = false;
    pauseExitFadeTimer_ = 0.0f;
    pauseExitTarget_ = 0;
    if (pausePostProcessSaved_ && ctx_ != nullptr &&
        ctx_->rendering.postEffectManager != nullptr) {
        ctx_->rendering.postEffectManager->SetBaseProfile(
            pauseSavedPostProcess_);
    }
    pausePostProcessSaved_ = false;
}

void GameScene::UpdatePauseMenu(Input *input) {
    if (input == nullptr) {
        return;
    }

    if (pauseExitFadeActive_) {
        pauseExitFadeTimer_ =
            (std::min)(pauseExitFadeTimer_ + ctx_->frame.deltaTime,
                       kPauseExitFadeDuration);
        if (pauseExitFadeTimer_ >= kPauseExitFadeDuration) {
            const int target = pauseExitTarget_;
            ClosePauseMenu();
            if (target == 1) {
                sceneManager_->ChangeScene(std::make_unique<GameScene>(
                    inputCalibration_, combatDifficulty_));
            } else if (target == 2) {
                sceneManager_->ChangeScene(std::make_unique<TitleScene>());
            }
        }
        return;
    }

    const bool gamepad = input->IsGamepadConnected();
    const bool moveUp = IsPauseDirectionTriggered(
        *input, DIK_UP, DIK_W, gamepad, XINPUT_GAMEPAD_DPAD_UP);
    const bool moveDown = IsPauseDirectionTriggered(
        *input, DIK_DOWN, DIK_S, gamepad, XINPUT_GAMEPAD_DPAD_DOWN);
    constexpr int kPauseMenuItemCount = 3;
    if (moveUp && !moveDown) {
        pauseMenuIndex_ =
            (pauseMenuIndex_ + kPauseMenuItemCount - 1) % kPauseMenuItemCount;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Select);
    } else if (moveDown && !moveUp) {
        pauseMenuIndex_ = (pauseMenuIndex_ + 1) % kPauseMenuItemCount;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Select);
    }

    const bool cancel = IsPauseCancelTriggered(*input, gamepad);
    if (cancel) {
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Cancel);
        ClosePauseMenu();
        return;
    }

    const bool confirm = IsPauseConfirmTriggered(*input, gamepad);
    if (confirm) {
        AppSceneServices::PlayMenuSe(
            *ctx_, pauseMenuIndex_ == 0 ? AppSceneServices::MenuSe::Cancel
                                        : AppSceneServices::MenuSe::Selected);
        ExecutePauseMenuSelection();
    }
}

void GameScene::ExecutePauseMenuSelection() {
    switch (pauseMenuIndex_) {
    case 0:
        ClosePauseMenu();
        break;
    case 1:
        pauseExitFadeActive_ = true;
        pauseExitFadeTimer_ = 0.0f;
        pauseExitTarget_ = 1;
        break;
    case 2:
        pauseExitFadeActive_ = true;
        pauseExitFadeTimer_ = 0.0f;
        pauseExitTarget_ = 2;
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
    const std::array<std::wstring, 12> paths{
        handTutorial ? L"app/resources/ui/tutorial_dynamic/left_right_hand.png"
                     : L"app/resources/ui/tutorial_dynamic/left_right_kbm.png",
        handTutorial ? L"app/resources/ui/tutorial_dynamic/right_sword_hand.png"
                     : L"app/resources/ui/tutorial_dynamic/right_sword_kbm.png",
        L"app/resources/ui/tutorial_dynamic/wait.png",
        L"app/resources/ui/tutorial_dynamic/vertical.png",
        L"app/resources/ui/tutorial_dynamic/horizontal.png",
        L"app/resources/ui/tutorial_dynamic/release.png",
        L"app/resources/ui/tutorial_dynamic/success.png",
        L"app/resources/ui/tutorial_dynamic/miss.png",
        L"app/resources/ui/tutorial_dynamic/complete.png",
        L"app/resources/ui/tutorial_dynamic/excellent.png",
        L"app/resources/ui/tutorial_dynamic/exit.png",
        L"app/resources/ui/tutorial_dynamic/show_hand.png",
    };
    for (size_t i = 0; i < paths.size(); ++i) {
        tutorialTextureIds_[i] = ctx_->rendering.texture->Load(paths[i]);
        tutorialTextureWidths_[i] = static_cast<float>(
            ctx_->rendering.texture->GetWidth(tutorialTextureIds_[i]));
        tutorialTextureHeights_[i] = static_cast<float>(
            ctx_->rendering.texture->GetHeight(tutorialTextureIds_[i]));
    }
    for (int i = 0; i < 10; ++i) {
        const size_t index = static_cast<size_t>(i);
        tutorialDigitTextureIds_[index] = ctx_->rendering.texture->Load(
            L"app/resources/ui/result/mplus/glyphs/char_" + std::to_wstring(i) +
            L".png");
        tutorialDigitTextureWidths_[index] = static_cast<float>(
            ctx_->rendering.texture->GetWidth(tutorialDigitTextureIds_[index]));
        tutorialDigitTextureHeights_[index] =
            static_cast<float>(ctx_->rendering.texture->GetHeight(
                tutorialDigitTextureIds_[index]));
    }
    tutorialSlashTextureId_ = ctx_->rendering.texture->Load(
        L"app/resources/ui/result/mplus/glyphs/char_slash.png");
    tutorialSlashTextureWidth_ = static_cast<float>(
        ctx_->rendering.texture->GetWidth(tutorialSlashTextureId_));
    tutorialSlashTextureHeight_ = static_cast<float>(
        ctx_->rendering.texture->GetHeight(tutorialSlashTextureId_));
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
    DrawPauseRect(panelX, panelY, panelW, 4.0f, {1.0f, 0.76f, 0.22f, 0.88f});
    DrawPauseImage(pauseMenuTextureIds_[0], pauseMenuTextureWidths_[0],
                   pauseMenuTextureHeights_[0],
                   w * 0.5f - pauseMenuTextureWidths_[0] * 0.5f, panelY + 44.0f,
                   1.0f);

    for (int i = 0; i < 3; ++i) {
        const float y = itemStartY + static_cast<float>(i) * itemGap;
        const bool selected = i == pauseMenuIndex_;
        DrawPauseRect(itemX, y, itemW, itemH,
                      selected
                          ? DirectX::XMFLOAT4{0.82f, 0.52f, 0.12f, 0.82f}
                          : DirectX::XMFLOAT4{0.08f, 0.095f, 0.11f, 0.82f});
        DrawPauseRect(itemX + 4.0f, y + 4.0f, itemW - 8.0f, itemH - 8.0f,
                      selected
                          ? DirectX::XMFLOAT4{0.20f, 0.14f, 0.05f, 0.86f}
                          : DirectX::XMFLOAT4{0.015f, 0.018f, 0.022f, 0.86f});
        if (selected) {
            DrawPauseRect(itemX + 12.0f, y + 12.0f, 5.0f, itemH - 24.0f,
                          {1.0f, 0.88f, 0.36f, 0.95f});
        }

        const size_t imageIndex = static_cast<size_t>(i) + 1u;
        const float maxLabelW = itemW * 0.78f;
        const float maxLabelH = itemH * 0.78f;
        const float baseScale =
            (std::min)(maxLabelW /
                           (std::max)(pauseMenuTextureWidths_[imageIndex],
                                      1.0f),
                       maxLabelH /
                           (std::max)(pauseMenuTextureHeights_[imageIndex],
                                      1.0f));
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
    if (pauseExitFadeActive_) {
        const float fadeT = std::clamp(
            pauseExitFadeTimer_ / kPauseExitFadeDuration, 0.0f, 1.0f);
        DrawPauseRect(0.0f, 0.0f, w, h,
                      {0.0f, 0.0f, 0.0f, SmoothStep01(fadeT)});
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
    const float fadeIn = std::clamp(bladeClashFinishTimer_ / 0.16f, 0.0f, 1.0f);
    const float fadeOut =
        1.0f - std::clamp((ratio - 0.90f) / 0.10f, 0.0f, 1.0f);
    const float alpha = fadeIn * fadeOut;
    if (alpha <= 0.01f) {
        return;
    }

    auto pulse = [&](float center, float width, float peak) {
        return (std::max)(0.0f,
                          (1.0f -
                           std::fabs(bladeClashFinishTimer_ - center) / width) *
                              peak);
    };
    const float flash = bladeClashFinishPlayerWon_
                            ? std::clamp((std::max)(pulse(0.28f, 0.16f, 1.0f),
                                                    pulse(0.56f, 0.20f, 0.72f)),
                                         0.0f, 1.0f)
                            : std::clamp((std::max)(pulse(0.10f, 0.24f, 0.72f),
                                                    pulse(0.58f, 0.32f, 0.42f)),
                                         0.0f, 1.0f);
    const float barH = std::clamp(screenH * 0.085f, 52.0f, 78.0f);
    const XMFLOAT4 barColor = bladeClashFinishPlayerWon_
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
    const float normalized =
        std::clamp((bladeClashGauge_ + 1.0f) * 0.5f, 0.0f, 1.0f);
    const float centerX = x + barW * 0.5f;
    const float markerX = x + barW * normalized;
    const float timeRate =
        bladeClashDuration_ > 0.0001f
            ? std::clamp(bladeClashTimer_ / bladeClashDuration_, 0.0f, 1.0f)
            : 0.0f;

    ctx_->rendering.sprite->PreDraw();
    const float impact = std::clamp(bladeClashImpactPulse_, 0.0f, 1.0f);
    DrawPauseRect(x - 10.0f - impact * 5.0f, y - 12.0f - impact * 3.0f,
                  barW + 20.0f + impact * 10.0f, barH + 24.0f + impact * 6.0f,
                  {0.02f, 0.022f, 0.028f, 0.72f});
    DrawPauseRect(x, y, barW, barH, {0.08f, 0.075f, 0.065f, 0.92f});
    DrawPauseRect(x + 3.0f, y + 3.0f, barW - 6.0f, barH - 6.0f,
                  {0.22f, 0.10f, 0.08f, 0.86f});
    DrawPauseRect(centerX, y - 4.0f, 2.0f, barH + 8.0f,
                  {0.95f, 0.90f, 0.64f, 0.64f});
    DrawPauseRect(x + 3.0f, y + 3.0f, (barW - 6.0f) * normalized, barH - 6.0f,
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

void GameScene::DrawTutorialCounterDigit(int digit, float x, float y,
                                         float scale, const XMFLOAT4 &color) {
    const int clampedDigit = std::clamp(digit, 0, 9);
    const size_t index = static_cast<size_t>(clampedDigit);
    if (tutorialDigitTextureWidths_[index] <= 0.0f ||
        tutorialDigitTextureHeights_[index] <= 0.0f) {
        return;
    }

    Sprite sprite{};
    sprite.textureId = tutorialDigitTextureIds_[index];
    sprite.position = {x, y};
    sprite.size = {tutorialDigitTextureWidths_[index] * scale,
                   tutorialDigitTextureHeights_[index] * scale};
    sprite.color = color;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void GameScene::DrawTutorialSlashCounter(float x, float y, float scale,
                                         float alpha) {
    auto drawDigit = [&](int digit, float dx, const XMFLOAT4 &color) {
        DrawTutorialCounterDigit(digit, x + dx * scale, y, scale, color);
    };
    auto drawSlash = [&](float dx, const XMFLOAT4 &color) {
        if (tutorialSlashTextureWidth_ <= 0.0f ||
            tutorialSlashTextureHeight_ <= 0.0f) {
            return;
        }

        Sprite sprite{};
        sprite.textureId = tutorialSlashTextureId_;
        sprite.position = {x + dx * scale, y};
        sprite.size = {tutorialSlashTextureWidth_ * scale,
                       tutorialSlashTextureHeight_ * scale};
        sprite.color = color;
        ctx_->rendering.sprite->DrawSprite(sprite);
    };

    const int count = std::clamp(tutorialOperationSlashCount_, 0,
                                 kTutorialOperationRequiredSlashes);
    const XMFLOAT4 shadow{0.0f, 0.0f, 0.0f, 0.46f * alpha};
    const XMFLOAT4 numberColor{1.0f, 0.96f, 0.78f, 0.98f * alpha};
    const XMFLOAT4 slashColor{0.55f, 0.98f, 0.93f, 0.86f * alpha};

    DrawPauseRect(x - 16.0f * scale, y - 12.0f * scale, 148.0f * scale,
                  90.0f * scale, {0.0f, 0.0f, 0.0f, 0.38f * alpha});
    DrawPauseRect(x - 16.0f * scale, y - 12.0f * scale, 148.0f * scale,
                  4.0f * scale, {0.02f, 0.95f, 0.84f, 0.74f * alpha});

    drawDigit(count, -9.0f, shadow);
    drawSlash(34.0f, shadow);
    drawDigit(kTutorialOperationRequiredSlashes, 77.0f, shadow);

    drawDigit(count, -11.0f, numberColor);
    drawSlash(32.0f, slashColor);
    drawDigit(kTutorialOperationRequiredSlashes, 75.0f, numberColor);
}

void GameScene::DrawTutorialOverlay() {
    if (!tutorialMode_ || ctx_ == nullptr ||
        ctx_->rendering.sprite == nullptr || ctx_->systems.winApp == nullptr) {
        return;
    }
    LoadTutorialImages();
    const float width = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float height = static_cast<float>(ctx_->systems.winApp->GetHeight());
    const ActionStep step = enemy_.GetActionStep();
    const float releaseRatio =
        step == ActionStep::Active
            ? 1.0f
            : std::clamp(enemy_.GetReleaseAnticipationRatio(), 0.0f, 1.0f);
    const int messageIndex =
        ResolveTutorialMessageIndex(enemy_.GetActionKind(), step, releaseRatio);
    const bool showExcellent =
        tutorialStep_ < kTutorialStepPractice &&
        (tutorialExcellentTimer_ > 0.0f || tutorialSuccessTimer_ > 0.0f);

    ctx_->rendering.sprite->PreDraw();
    DrawPauseRect(0.0f, 0.0f, width, height * 0.16f, {0.0f, 0.0f, 0.0f, 0.34f});
    if (showExcellent) {
        DrawTutorialExcellent(width, height);
    } else {
        DrawTutorialMessagePanel(width, height, messageIndex);
    }
    ctx_->rendering.sprite->PostDraw();
}

int GameScene::ResolveTutorialMessageIndex(ActionKind actionKind,
                                           ActionStep actionStep,
                                           float releaseRatio) const {
    const bool redGuided = tutorialStep_ == kTutorialStepRedSmash ||
                           tutorialStep_ == kTutorialStepRedSweep;
    int message =
        !redGuided && (releaseRatio > 0.0f || actionStep == ActionStep::Active)
            ? kTutorialTextRelease
            : kTutorialTextWait;
    if (tutorialMissTimer_ > 0.0f) {
        return kTutorialTextMiss;
    }
    if (tutorialStep_ == kTutorialStepHandPresence) {
        return kTutorialTextShowHand;
    }
    if (tutorialStep_ == kTutorialStepLeftSword) {
        return kTutorialTextLeftRight;
    }
    if (tutorialStep_ == kTutorialStepRightSword) {
        return kTutorialTextRightSword;
    }
    if (tutorialStep_ == kTutorialStepGreenSmash ||
        tutorialStep_ == kTutorialStepGreenSweep) {
        return kTutorialTextRelease;
    }
    if (tutorialStep_ >= kTutorialStepPractice) {
        return kTutorialTextComplete;
    }
    if (actionKind == ActionKind::Smash) {
        return kTutorialTextVertical;
    }
    if (actionKind == ActionKind::Sweep) {
        return kTutorialTextHorizontal;
    }
    return message;
}

void GameScene::DrawTutorialMessagePanel(float w, float h, int messageIndex) {
    DrawPauseRect(0.0f, h * 0.76f, w, h * 0.24f, {0.0f, 0.0f, 0.0f, 0.48f});

    const float panelW = std::clamp(w * 0.62f, 680.0f, 1060.0f);
    const float panelH = 112.0f;
    const float panelX = (w - panelW) * 0.5f;
    const float panelY = h - 164.0f;
    const bool releaseNow = messageIndex == kTutorialTextRelease;
    DrawPauseRect(panelX + 8.0f, panelY + 10.0f, panelW, panelH,
                  {0.0f, 0.0f, 0.0f, 0.28f});
    DrawPauseRect(panelX, panelY, panelW, panelH,
                  releaseNow
                      ? DirectX::XMFLOAT4{0.02f, 0.18f, 0.10f, 0.86f}
                      : DirectX::XMFLOAT4{0.014f, 0.030f, 0.032f, 0.82f});
    DrawPauseRect(panelX, panelY, panelW * (releaseNow ? 1.0f : 0.42f), 5.0f,
                  releaseNow ? DirectX::XMFLOAT4{0.14f, 1.0f, 0.28f, 0.96f}
                             : DirectX::XMFLOAT4{0.02f, 0.95f, 0.84f, 0.78f});

    const float textW = tutorialTextureWidths_[messageIndex];
    const float textH = tutorialTextureHeights_[messageIndex];
    const float targetTextH = 66.0f;
    const float scale = (std::min)(targetTextH / (std::max)(textH, 1.0f),
                                   (panelW - 92.0f) / (std::max)(textW, 1.0f));
    DrawPauseImage(tutorialTextureIds_[messageIndex], textW, textH,
                   panelX + (panelW - textW * scale) * 0.5f,
                   panelY + (panelH - textH * scale) * 0.5f, scale, 1.0f);

    if (tutorialStep_ == kTutorialStepHandPresence) {
        const float meterW = panelW - 128.0f;
        const float meterH = 8.0f;
        const float meterX = panelX + (panelW - meterW) * 0.5f;
        const float meterY = panelY - 26.0f;
        const float holdRatio = std::clamp(
            tutorialHandHoldTimer_ / kTutorialHandPresenceRequiredSeconds, 0.0f,
            1.0f);
        DrawPauseRect(meterX, meterY, meterW, meterH,
                      {0.0f, 0.0f, 0.0f, 0.42f});
        DrawPauseRect(meterX, meterY, meterW * holdRatio, meterH,
                      {0.02f, 0.95f, 0.84f, 0.88f});
    }

    if (tutorialStep_ >= kTutorialStepLeftSword &&
        tutorialStep_ < kTutorialStepRedSmash) {
        const float counterScale = std::clamp(w / 1600.0f, 0.76f, 1.0f);
        DrawTutorialSlashCounter(panelX + panelW - 150.0f * counterScale,
                                 panelY - 82.0f * counterScale, counterScale,
                                 1.0f);
    }

    if (tutorialStep_ >= kTutorialStepPractice) {
        const float exitW = tutorialTextureWidths_[kTutorialTextExit];
        const float exitH = tutorialTextureHeights_[kTutorialTextExit];
        const float exitScale =
            (std::min)(0.80f, (w * 0.31f) / (std::max)(exitW, 1.0f));
        const float exitY =
            h -
            (exitH - kTutorialControlsImageBottomTransparentPixels) *
                exitScale -
            kTutorialControlsPadding;
        DrawPauseImage(tutorialTextureIds_[kTutorialTextExit], exitW, exitH,
                       kTutorialControlsPadding, exitY, exitScale, 0.58f);
    }
}

void GameScene::DrawTutorialExcellent(float w, float h) {
    const float excellentW = tutorialTextureWidths_[kTutorialTextExcellent];
    const float excellentH = tutorialTextureHeights_[kTutorialTextExcellent];
    const float excellentScale =
        (std::min)(1.20f, (w * 0.48f) / (std::max)(excellentW, 1.0f));
    const float excellentDrawW = excellentW * excellentScale;
    const float excellentDrawH = excellentH * excellentScale;
    const float excellentX = (w - excellentDrawW) * 0.5f;
    const float excellentY = (h - excellentDrawH) * 0.5f;
    DrawPauseRect(excellentX - 36.0f, excellentY - 20.0f,
                  excellentDrawW + 72.0f, excellentDrawH + 40.0f,
                  {0.0f, 0.0f, 0.0f, 0.42f});
    DrawPauseImage(tutorialTextureIds_[kTutorialTextExcellent], excellentW,
                   excellentH, excellentX, excellentY, excellentScale, 1.0f);
}
void GameScene::DrawTutorialEntryFade() {
    if (!tutorialMode_ || ctx_ == nullptr ||
        ctx_->rendering.sprite == nullptr || ctx_->systems.winApp == nullptr ||
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
    const bool suppressSwordVfx = ShouldSuppressSwordVfx();
    PrepareCombatFeedback(event, suppressSwordVfx);
    combatFeedback_.PushEvent(event);
    EmitCombatFeedbackVfx(event, suppressSwordVfx);
    PlayCombatFeedbackSound(event);
}

void GameScene::PrepareCombatFeedback(const CombatFeedbackEvent &event,
                                      bool suppressSwordVfx) {
    if (suppressSwordVfx) {
        ClearSwordVfx();
    }
    if (!suppressSwordVfx &&
        (event.type == CombatFeedbackEventType::MistimedCounterSlash ||
         event.type == CombatFeedbackEventType::CounterSuccess ||
         event.type == CombatFeedbackEventType::EnemyProjectileReflect)) {
        swordTrailRenderer_.SuppressSlashUntilInactive(event.swordIndex);
    }
    if (event.type == CombatFeedbackEventType::MistimedCounterSlash) {
        mistimedCounterSlashThisFrame_ = true;
    }
    if (event.type == CombatFeedbackEventType::CounterSuccess) {
        counterSuccessSlashThisFrame_ = true;
    }
}

void GameScene::EmitCombatFeedbackVfx(const CombatFeedbackEvent &event,
                                      bool suppressSwordVfx) {
    const bool isSwordVfxFeedback =
        event.type == CombatFeedbackEventType::PlayerSlashHit ||
        event.type == CombatFeedbackEventType::MistimedCounterSlash ||
        event.type == CombatFeedbackEventType::CounterSuccess ||
        event.type == CombatFeedbackEventType::EnemyProjectileReflect ||
        event.type == CombatFeedbackEventType::BladeClashPierce;
    if (!suppressSwordVfx || !isSwordVfxFeedback) {
        EmitCombatParticles(event);
    }
    if (!suppressSwordVfx && isSwordVfxFeedback) {
        DirectX::XMFLOAT2 slashDirection{0.0f, 0.0f};
        const auto swords = player_.GetSwords();
        if (event.swordIndex < swords.size() && swords[event.swordIndex]) {
            slashDirection = swords[event.swordIndex]->GetSlashDirection();
        }
        if (event.type == CombatFeedbackEventType::CounterSuccess ||
            event.type == CombatFeedbackEventType::EnemyProjectileReflect) {
            swordSlashArcRenderer_.EmitParryLine(event.position,
                                                 event.direction, camera_,
                                                 event.power, slashDirection);
        } else {
            const SwordSlashHitLineStyle hitLineStyle =
                event.type == CombatFeedbackEventType::MistimedCounterSlash
                    ? SwordSlashHitLineStyle::RedPunish
                    : SwordSlashHitLineStyle::Normal;
            swordSlashArcRenderer_.EmitHitLine(event.position, event.direction,
                                               camera_, event.power,
                                               slashDirection, hitLineStyle);
        }
    }
}

void GameScene::PlayCombatFeedbackSound(const CombatFeedbackEvent &event) {
    if (!soundsLoaded_ || ctx_ == nullptr || ctx_->systems.sound == nullptr) {
        return;
    }

    switch (event.type) {
    case CombatFeedbackEventType::EnemyProjectileReflect:
    case CombatFeedbackEventType::CounterSuccess:
        ctx_->systems.sound->Play(counterSuccessSlashSoundId_,
                                  kCounterSoundVolume *
                                      AppSceneServices::GetSeVolume());
        break;
    case CombatFeedbackEventType::BladeClashGuardBreak:
        ctx_->systems.sound->Play(counterSoundId_,
                                  kCounterSoundVolume *
                                      AppSceneServices::GetSeVolume());
        break;
    case CombatFeedbackEventType::PlayerSlashHit:
    case CombatFeedbackEventType::BladeClashPierce:
        ctx_->systems.sound->Play(
            hitSoundId_, kHitSoundVolume * AppSceneServices::GetSeVolume());
        break;
    case CombatFeedbackEventType::MistimedCounterSlash:
        ctx_->systems.sound->PlayFrom(
            mistimedCounterSoundId_, mistimedCounterSoundStartSeconds_,
            kDamageSoundVolume * AppSceneServices::GetSeVolume());
        break;
    case CombatFeedbackEventType::PlayerDamaged:
        ctx_->systems.sound->Play(damageSoundId_,
                                  kDamageSoundVolume *
                                      AppSceneServices::GetSeVolume());
        break;
    default:
        break;
    }
}

bool GameScene::ShouldSuppressSwordVfx() const {
    return enemy_.IsPhaseTransitionActive() || victorySequenceActive_ ||
           defeatSequenceActive_ || player_.GetHP() <= 0.0f ||
           enemy_.GetHP() <= 0.0f;
}

void GameScene::ClearSwordVfx() {
    swordTrailRenderer_.Reset();
    swordSlashArcRenderer_.Reset();
}

void GameScene::ClearGameplayParticles() {
    sparkParticles_.Clear();
    explosionParticles_.Clear();
    smokeParticles_.Clear();
    swordFlashParticles_.Clear();
    ClearSwordVfx();
}

void GameScene::UpdateSwordVfx(float deltaTime) {
    if (ShouldSuppressSwordVfx()) {
        ClearSwordVfx();
        return;
    }

    swordTrailRenderer_.Update(player_, deltaTime);
    swordSlashArcRenderer_.Update(deltaTime);
    if (player_.IsChargingRangedAttack()) {
        const float charge = player_.GetRangedAttackChargeRatio();
        const XMFLOAT3 playerPos = player_.GetTransform().position;
        const float yaw = player_.GetYaw();
        const XMFLOAT3 forward{std::sinf(yaw), 0.0f, std::cosf(yaw)};
        const XMFLOAT3 chargePos{playerPos.x + forward.x * 0.72f,
                                 playerPos.y + 1.24f,
                                 playerPos.z + forward.z * 0.72f};
        EmitParticleBurst(swordFlashParticles_, chargePos,
                          charge >= 1.0f ? 18u : 8u, 0.12f,
                          AppParticleBurstStyle::Flash,
                          {1.0f, 0.82f, 0.24f, 0.56f + 0.34f * charge}, forward,
                          0.24f + 0.72f * charge);
        EmitParticleBurst(
            sparkParticles_, chargePos, charge >= 1.0f ? 34u : 14u, 0.18f,
            AppParticleBurstStyle::SpiritSparkle,
            {1.0f, 0.92f, 0.38f, 0.42f + 0.34f * charge},
            {-forward.x, 0.0f, -forward.z}, 0.58f + 1.05f * charge);
    }
}

void GameScene::BeginArcaneProjectileVolley() {
    ResetArcaneProjectile();
    for (ArcaneProjectileState &projectile : cataclysmProjectiles_) {
        projectile = {};
    }
    arcaneProjectileVolleyActive_ = true;
    arcaneProjectileVolleyCataclysm_ =
        enemy_.GetActionKind() == ActionKind::CataclysmLaser;
    arcaneProjectileVolleyShotsFired_ = 0;
    arcaneProjectileVolleyReflectedHits_ = 0;
    arcaneProjectileVolleyTimer_ = 0.0f;
    if (arcaneProjectileVolleyCataclysm_) {
        for (int i = 0; i < kCataclysmProjectileVolleyShotCount; ++i) {
            arcaneProjectileVolleyShotsFired_ = i;
            SpawnArcaneProjectile();
        }
        arcaneProjectileVolleyShotsFired_ = 0;
        arcaneProjectileVolleyTimer_ = kCataclysmProjectilePreviewHold;
    }
}

void GameScene::UpdateArcaneProjectileVolley(float deltaTime) {
    if (!arcaneProjectileVolleyActive_) {
        return;
    }

    const ActionKind firingKind = arcaneProjectileVolleyCataclysm_
                                      ? ActionKind::CataclysmLaser
                                      : ActionKind::ArcaneLaser;
    const bool enemyStillFiring = enemy_.GetActionKind() == firingKind &&
                                  enemy_.GetActionStep() == ActionStep::Active;
    if (!enemyStillFiring) {
        arcaneProjectileVolleyActive_ = false;
        arcaneProjectileVolleyTimer_ = 0.0f;
        return;
    }

    const int shotCount = arcaneProjectileVolleyCataclysm_
                              ? kCataclysmProjectileVolleyShotCount
                              : kArcaneProjectileVolleyShotCount;
    if (arcaneProjectileVolleyShotsFired_ >= shotCount) {
        if (!HasActiveArcaneProjectiles()) {
            arcaneProjectileVolleyActive_ = false;
        }
        return;
    }

    arcaneProjectileVolleyTimer_ =
        (std::max)(0.0f, arcaneProjectileVolleyTimer_ - deltaTime);
    if (arcaneProjectileVolleyTimer_ > 0.0f) {
        return;
    }

    if (arcaneProjectileVolleyCataclysm_) {
        if (!FireCataclysmVolleyProjectile()) {
            ++arcaneProjectileVolleyShotsFired_;
            arcaneProjectileVolleyTimer_ = GetEnemyRangedVolleyInterval();
            return;
        }
    } else {
        SpawnArcaneProjectile();
    }
    ++arcaneProjectileVolleyShotsFired_;
    arcaneProjectileVolleyTimer_ = GetEnemyRangedVolleyInterval();
    if (soundsLoaded_ && ctx_ != nullptr && ctx_->systems.sound != nullptr) {
        ctx_->systems.sound->Play(enemyReleaseSoundId_,
                                  kEnemyReleaseSoundVolume *
                                      AppSceneServices::GetSeVolume());
    }
}

bool GameScene::HasActiveArcaneProjectiles() const {
    if (arcaneProjectile_.active) {
        return true;
    }
    return std::any_of(cataclysmProjectiles_.begin(),
                       cataclysmProjectiles_.end(),
                       [](const ArcaneProjectileState &projectile) {
                           return projectile.active;
                       });
}

bool GameScene::FireCataclysmVolleyProjectile() {
    const int shotIndex = std::clamp(arcaneProjectileVolleyShotsFired_, 0,
                                     kCataclysmProjectileVolleyShotCount - 1);
    ArcaneProjectileState &projectile = cataclysmProjectiles_[shotIndex];
    if (!projectile.active || !projectile.waitingToFire) {
        return false;
    }

    const XMFLOAT3 baseForward = NormalizeParticleCompatVec3(
        enemy_.GetCataclysmLaserDirection(), {0.0f, 0.0f, 1.0f});
    const XMFLOAT3 launchDir =
        ComputeCataclysmProjectileLaunchDirection(shotIndex, baseForward);
    const float speedScale = GetEnemyProjectileSpeedScale();
    const float launchSpeed = kCataclysmProjectileLaunchSpeed * speedScale;
    projectile.waitingToFire = false;
    projectile.velocity = {launchDir.x * launchSpeed, launchDir.y * launchSpeed,
                           launchDir.z * launchSpeed};
    projectile.age = 0.0f;
    projectile.life = 3.8f;
    EmitParticleBurst(swordFlashParticles_, projectile.position, 58, 0.24f,
                      AppParticleBurstStyle::Flash, {0.18f, 0.90f, 1.0f, 0.98f},
                      launchDir, 1.25f);
    EmitParticleBurst(sparkParticles_, projectile.position, 96, 0.28f,
                      AppParticleBurstStyle::Sparks,
                      {0.20f, 0.96f, 1.0f, 0.88f}, launchDir, 2.10f);
    return true;
}

void GameScene::SpawnArcaneProjectile() {
    const bool cataclysmShot = arcaneProjectileVolleyCataclysm_;
    XMFLOAT3 muzzle = cataclysmShot ? enemy_.GetCataclysmLaserMuzzlePosition()
                                    : enemy_.GetArcaneLaserMuzzlePosition();
    XMFLOAT3 direction = NormalizeParticleCompatVec3(
        cataclysmShot ? enemy_.GetCataclysmLaserDirection()
                      : enemy_.GetArcaneLaserDirection(),
        {0.0f, 0.0f, -1.0f});
    direction.y = 0.0f;
    direction = NormalizeParticleCompatVec3(direction, {0.0f, 0.0f, -1.0f});
    const bool arcaneArcShot = !cataclysmShot &&
                               enemy_.GetBossPhase() == BossPhase::Phase2 &&
                               arcaneProjectileVolleyShotsFired_ > 0;
    ArcaneProjectileState *projectile = FindFreeArcaneProjectile(cataclysmShot);
    if (projectile == nullptr) {
        return;
    }
    if (cataclysmShot) {
        ConfigureCataclysmProjectileAim(muzzle, direction);
    }
    if (arcaneArcShot) {
        ConfigureArcaneArcProjectileAim(muzzle, direction);
    }
    InitializeArcaneProjectileState(*projectile, cataclysmShot, arcaneArcShot,
                                    muzzle, direction);
}

GameScene::ArcaneProjectileState *
GameScene::FindFreeArcaneProjectile(bool cataclysmShot) {
    if (!cataclysmShot && !arcaneProjectile_.active) {
        return &arcaneProjectile_;
    }
    const auto available =
        std::find_if(cataclysmProjectiles_.begin(), cataclysmProjectiles_.end(),
                     [](const ArcaneProjectileState &candidate) {
                         return !candidate.active;
                     });
    return available == cataclysmProjectiles_.end() ? nullptr : &*available;
}

void GameScene::ConfigureCataclysmProjectileAim(XMFLOAT3 &muzzle,
                                                XMFLOAT3 &direction) const {
    constexpr XMFLOAT2 kBarrageBand[kCataclysmProjectileVolleyShotCount] = {
        {-3.45f, 0.12f}, {-1.70f, 0.22f}, {0.0f, 0.18f},
        {1.70f, 0.22f},  {3.45f, 0.12f},
    };
    const int barrageIndex =
        std::clamp(arcaneProjectileVolleyShotsFired_, 0,
                   kCataclysmProjectileVolleyShotCount - 1);
    const auto normalize2 = [](float x, float z) {
        float length = std::sqrt(x * x + z * z);
        if (length < 0.0001f) {
            length = 1.0f;
        }
        return XMFLOAT2{x / length, z / length};
    };
    const XMFLOAT2 sideDir = normalize2(direction.z, -direction.x);
    const XMFLOAT2 forwardDir = normalize2(direction.x, direction.z);
    const XMFLOAT2 band = kBarrageBand[barrageIndex];
    muzzle = enemy_.GetCataclysmLaserMuzzlePosition();
    muzzle.x += sideDir.x * band.x + forwardDir.x * band.y;
    muzzle.y += 0.34f + static_cast<float>(barrageIndex % 2) * 0.30f;
    muzzle.z += sideDir.y * band.x + forwardDir.y * band.y;
    direction = ComputeCataclysmProjectileLaunchDirection(
        barrageIndex,
        NormalizeParticleCompatVec3(direction, {0.0f, 0.0f, 1.0f}));
}

void GameScene::ConfigureArcaneArcProjectileAim(XMFLOAT3 &muzzle,
                                                XMFLOAT3 &direction) const {
    const float sideSign =
        (arcaneProjectileVolleyShotsFired_ % 2 == 0) ? -1.0f : 1.0f;
    const XMFLOAT3 side{direction.z, 0.0f, -direction.x};
    muzzle.x += side.x * sideSign * 2.25f + direction.x * 0.55f;
    muzzle.y += 2.35f;
    muzzle.z += side.z * sideSign * 2.25f + direction.z * 0.55f;
    direction = NormalizeParticleCompatVec3(
        {direction.x * 0.34f + side.x * sideSign * 0.58f, 0.72f,
         direction.z * 0.34f + side.z * sideSign * 0.58f},
        direction);
}

void GameScene::InitializeArcaneProjectileState(
    ArcaneProjectileState &projectile, bool cataclysmShot, bool arcaneArcShot,
    const XMFLOAT3 &muzzle, const XMFLOAT3 &direction) {
    const float speedScale = GetEnemyProjectileSpeedScale();
    const float hostileSpeed = (cataclysmShot ? kCataclysmProjectileHostileSpeed
                                              : kArcaneProjectileHostileSpeed) *
                               speedScale;
    const float arcSpeed = kArcaneProjectileArcSpeed * speedScale;
    projectile.active = true;
    projectile.reflected = false;
    projectile.cataclysm = cataclysmShot;
    projectile.fromAbove = cataclysmShot || arcaneArcShot;
    projectile.waitingToFire = cataclysmShot;
    projectile.position = muzzle;
    projectile.previousPosition = muzzle;
    projectile.velocity =
        cataclysmShot ? direction
        : arcaneArcShot
            ? XMFLOAT3{direction.x * arcSpeed, direction.y * arcSpeed,
                       direction.z * arcSpeed}
            : XMFLOAT3{direction.x * hostileSpeed, 0.0f,
                       direction.z * hostileSpeed};
    projectile.age = 0.0f;
    projectile.life = cataclysmShot ? 5.8f : 4.2f;
    projectile.damage = enemy_.GetCurrentAttackDamage();
    projectile.knockback = enemy_.GetCurrentAttackKnockback();
    projectile.textureId = GetCurrentEnemyTextureId();
    if (cataclysmShot) {
        constexpr XMFLOAT2 kCueDirections[kCataclysmProjectileVolleyShotCount] =
            {
                {0.0f, 1.0f}, {0.7071f, 0.7071f}, {-0.7071f, 0.7071f},
                {1.0f, 0.0f}, {-1.0f, 0.0f},
            };
        const int cueIndex =
            std::clamp(arcaneProjectileVolleyShotsFired_, 0,
                       kCataclysmProjectileVolleyShotCount - 1);
        projectile.cueDirection = kCueDirections[cueIndex];
    } else {
        constexpr int kCueDirectionCount =
            static_cast<int>(sizeof(kArcaneProjectileCueDirections) /
                             sizeof(kArcaneProjectileCueDirections[0]));
        const int cueIndex = std::rand() % kCueDirectionCount;
        projectile.cueDirection = kArcaneProjectileCueDirections[cueIndex];
    }
    projectile.reflectedBySwordIndex = 0;
    ApplyBulletTextureToModel(projectile.textureId);

    EmitParticleBurst(
        swordFlashParticles_, muzzle, 58, 0.28f, AppParticleBurstStyle::Flash,
        PickValue(cataclysmShot, XMFLOAT4{0.18f, 0.90f, 1.0f, 0.98f},
                  XMFLOAT4{0.24f, 1.0f, 0.78f, 0.96f}),
        direction, PickValue(cataclysmShot, 1.35f, 0.90f));
    EmitParticleBurst(
        sparkParticles_, muzzle, 96, 0.32f, AppParticleBurstStyle::Sparks,
        PickValue(cataclysmShot, XMFLOAT4{0.20f, 0.96f, 1.0f, 0.88f},
                  XMFLOAT4{0.30f, 1.0f, 0.86f, 0.82f}),
        direction, PickValue(cataclysmShot, 2.35f, 1.80f));
}

void GameScene::ResetArcaneProjectile() { arcaneProjectile_ = {}; }

void GameScene::UpdateArcaneProjectile(float deltaTime) {
    UpdateSingleArcaneProjectile(arcaneProjectile_, deltaTime);
    for (ArcaneProjectileState &projectile : cataclysmProjectiles_) {
        UpdateSingleArcaneProjectile(projectile, deltaTime);
    }
}

void GameScene::UpdateSingleArcaneProjectile(ArcaneProjectileState &projectile,
                                             float deltaTime) {
    if (!projectile.active || projectile.waitingToFire) {
        return;
    }
    projectile.age += deltaTime;
    projectile.previousPosition = projectile.position;

    if (projectile.reflected) {
        UpdateReflectedArcaneProjectile(projectile, deltaTime);
    } else if (projectile.fromAbove) {
        UpdateArcingArcaneProjectile(projectile, deltaTime);
    } else if (projectile.cataclysm &&
               projectile.age >= kCataclysmProjectileHomingDelay) {
        UpdateHomingArcaneProjectile(projectile, deltaTime);
    }
    FinishArcaneProjectileFrame(projectile, deltaTime);
}

void GameScene::UpdateReflectedArcaneProjectile(
    ArcaneProjectileState &projectile, float deltaTime) {
    XMFLOAT3 target = enemy_.GetTransform().position;
    target.y += 1.08f;
    XMFLOAT3 toEnemy{target.x - projectile.position.x,
                     target.y - projectile.position.y,
                     target.z - projectile.position.z};
    const XMFLOAT3 enemyDir =
        NormalizeParticleCompatVec3(toEnemy, {0.0f, 0.0f, 1.0f});
    const XMFLOAT3 launchDir = NormalizeParticleCompatVec3(
        projectile.reflectedLaunchDirection, enemyDir);
    const float launchHold = projectile.cataclysm
                                 ? kCataclysmProjectileReflectedLaunchHold
                                 : kArcaneProjectileReflectedLaunchHold;
    const float returnBlend = projectile.cataclysm
                                  ? kCataclysmProjectileReflectedReturnBlend
                                  : kArcaneProjectileReflectedReturnBlend;
    const float returnRate =
        SmoothStep01((projectile.age - launchHold) / returnBlend);
    const XMFLOAT3 desired = NormalizeParticleCompatVec3(
        {launchDir.x + (enemyDir.x - launchDir.x) * returnRate,
         launchDir.y + (enemyDir.y - launchDir.y) * returnRate,
         launchDir.z + (enemyDir.z - launchDir.z) * returnRate},
        enemyDir);
    const XMFLOAT3 current =
        NormalizeParticleCompatVec3(projectile.velocity, desired);
    const float steer =
        std::clamp(deltaTime * kArcaneProjectileReflectedSteerStrength *
                       (0.32f + returnRate * 0.86f),
                   0.0f, 1.0f);
    const XMFLOAT3 blended{current.x + (desired.x - current.x) * steer,
                           current.y + (desired.y - current.y) * steer,
                           current.z + (desired.z - current.z) * steer};
    const XMFLOAT3 reflectedDir = NormalizeParticleCompatVec3(blended, desired);
    const float reflectedSpeed = projectile.cataclysm
                                     ? kCataclysmProjectileReflectedSpeed
                                     : kArcaneProjectileReflectedSpeed;
    projectile.velocity = {reflectedDir.x * reflectedSpeed,
                           reflectedDir.y * reflectedSpeed,
                           reflectedDir.z * reflectedSpeed};
}

void GameScene::UpdateArcingArcaneProjectile(ArcaneProjectileState &projectile,
                                             float deltaTime) {
    XMFLOAT3 target = player_.GetTransform().position;
    target.y += 0.82f;
    const XMFLOAT3 toPlayer{target.x - projectile.position.x,
                            target.y - projectile.position.y,
                            target.z - projectile.position.z};
    const XMFLOAT3 desired =
        NormalizeParticleCompatVec3(toPlayer, projectile.velocity);
    const XMFLOAT3 current =
        NormalizeParticleCompatVec3(projectile.velocity, desired);
    const float turnEase =
        projectile.cataclysm
            ? EaseInCubic01(
                  (projectile.age - kCataclysmProjectileSpreadDuration) / 1.08f)
            : SmoothStep01(projectile.age / 0.48f);
    const float fallEase =
        projectile.cataclysm
            ? SmoothStep01(
                  (projectile.age - kCataclysmProjectileRocketLiftDuration) /
                  1.26f)
            : SmoothStep01((projectile.age - kArcaneProjectileArcLiftDuration) /
                           0.82f);
    const float speedEase =
        projectile.cataclysm
            ? EaseOutCubic01(projectile.age /
                             (kCataclysmProjectileSpreadDuration + 0.72f))
            : EaseOutCubic01(projectile.age / 0.58f);
    const float convergeStrength = projectile.cataclysm
                                       ? kCataclysmProjectileConvergeStrength
                                       : kArcaneProjectileArcTurnStrength;
    const float steer = std::clamp(
        deltaTime * (1.6f + convergeStrength * turnEase), 0.0f, 1.0f);
    const XMFLOAT3 blended{current.x + (desired.x - current.x) * steer,
                           current.y + (desired.y - current.y) * steer,
                           current.z + (desired.z - current.z) * steer};
    const XMFLOAT3 convergedDir = NormalizeParticleCompatVec3(blended, desired);
    const float speedScale = GetEnemyProjectileSpeedScale();
    const float arcedSpeed =
        (projectile.cataclysm
             ? kCataclysmProjectileLaunchSpeed * 0.42f +
                   (kCataclysmProjectileBarrageSpeed * 1.42f -
                    kCataclysmProjectileLaunchSpeed * 0.42f) *
                       speedEase
             : kArcaneProjectileArcSpeed +
                   (kArcaneProjectileHostileSpeed - kArcaneProjectileArcSpeed) *
                       speedEase) *
        speedScale;
    projectile.velocity = {convergedDir.x * arcedSpeed,
                           convergedDir.y * arcedSpeed,
                           convergedDir.z * arcedSpeed};
    if (projectile.cataclysm) {
        const float liftEase =
            1.0f - EaseOutCubic01(projectile.age /
                                  kCataclysmProjectileRocketLiftDuration);
        projectile.velocity.y +=
            kCataclysmProjectileRocketLiftAcceleration * liftEase * deltaTime;
        projectile.velocity.y -=
            kCataclysmProjectileRocketGravity * fallEase * deltaTime;
    } else {
        projectile.velocity.y -=
            kArcaneProjectileArcGravity * fallEase * deltaTime;
    }
}

void GameScene::UpdateHomingArcaneProjectile(ArcaneProjectileState &projectile,
                                             float deltaTime) {
    XMFLOAT3 target = player_.GetTransform().position;
    target.y = projectile.position.y;
    const XMFLOAT3 toPlayer{target.x - projectile.position.x, 0.0f,
                            target.z - projectile.position.z};
    const XMFLOAT3 desired =
        NormalizeParticleCompatVec3(toPlayer, projectile.velocity);
    const XMFLOAT3 current =
        NormalizeParticleCompatVec3(projectile.velocity, desired);
    const float steer =
        std::clamp(deltaTime * kCataclysmProjectileHomingStrength, 0.0f, 1.0f);
    const XMFLOAT3 blended{current.x + (desired.x - current.x) * steer, 0.0f,
                           current.z + (desired.z - current.z) * steer};
    const XMFLOAT3 homingDir = NormalizeParticleCompatVec3(blended, desired);
    const float hostileSpeed =
        kCataclysmProjectileHostileSpeed * GetEnemyProjectileSpeedScale();
    projectile.velocity = {homingDir.x * hostileSpeed, 0.0f,
                           homingDir.z * hostileSpeed};
}

void GameScene::FinishArcaneProjectileFrame(ArcaneProjectileState &projectile,
                                            float deltaTime) {
    projectile.position.x += projectile.velocity.x * deltaTime;
    projectile.position.y += projectile.velocity.y * deltaTime;
    projectile.position.z += projectile.velocity.z * deltaTime;
    projectile.life -= deltaTime;

    const XMFLOAT3 playerPos = player_.GetTransform().position;
    if (projectile.reflected && projectile.life <= 0.0f) {
        XMFLOAT3 target = enemy_.GetTransform().position;
        target.y += 1.08f;
        projectile.position = target;
        projectile.previousPosition = target;
        projectile.life = 0.016f;
        return;
    }
    if (projectile.life <= 0.0f ||
        (!projectile.reflected &&
         DistanceSq(projectile.position, playerPos) > 70.0f * 70.0f)) {
        projectile = {};
    }
}

void GameScene::UpdatePlayerChargedProjectile(float deltaTime) {
    if (!playerChargedProjectile_.active) {
        return;
    }

    playerChargedProjectile_.age += deltaTime;
    playerChargedProjectile_.position.x +=
        playerChargedProjectile_.velocity.x * deltaTime;
    playerChargedProjectile_.position.y +=
        playerChargedProjectile_.velocity.y * deltaTime;
    playerChargedProjectile_.position.z +=
        playerChargedProjectile_.velocity.z * deltaTime;
    playerChargedProjectile_.life -= deltaTime;

    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 impactTarget{enemyPos.x, enemyPos.y + 1.0f, enemyPos.z};
    if (!playerChargedProjectile_.hitConsumed &&
        DistanceSq(playerChargedProjectile_.position, impactTarget) <=
            1.55f * 1.55f) {
        const XMFLOAT3 impact = playerChargedProjectile_.position;
        const XMFLOAT3 direction = NormalizeParticleCompatVec3(
            playerChargedProjectile_.direction, {0.0f, 0.0f, 1.0f});
        const float appliedDamage =
            ApplyEnemyDamage(playerChargedProjectile_.damage);
        CombatFeedbackEvent feedback{};
        feedback.type = CombatFeedbackEventType::CounterSuccess;
        feedback.position = impactTarget;
        feedback.direction = direction;
        feedback.power = (std::max)(appliedDamage / 10.0f, 14.0f);
        feedback.swordIndex = 1;
        DispatchCombatFeedback(feedback);
        EmitParticleBurst(explosionParticles_, impact, 240, 0.68f,
                          AppParticleBurstStyle::Explosion,
                          {1.0f, 0.82f, 0.22f, 0.92f}, direction, 3.35f);
        EmitParticleBurst(sparkParticles_, impact, 160, 0.36f,
                          AppParticleBurstStyle::Sparks,
                          {1.0f, 0.94f, 0.42f, 0.88f}, direction, 2.85f);
        EmitParticleBurst(smokeParticles_, impact, 54, 0.58f,
                          AppParticleBurstStyle::Smoke,
                          {0.34f, 0.30f, 0.20f, 0.70f}, direction, 1.05f);
        if (soundsLoaded_ && ctx_ != nullptr &&
            ctx_->systems.sound != nullptr) {
            ctx_->systems.sound->Play(explosionSoundId_,
                                      kExplosionSoundVolume * 1.05f *
                                          AppSceneServices::GetSeVolume());
        }
        enemyHitCooldown_ = (std::max)(enemyHitCooldown_, 0.28f);
        playerChargedProjectile_ = {};
        return;
    }

    const XMFLOAT3 playerPos = player_.GetTransform().position;
    if (playerChargedProjectile_.life <= 0.0f ||
        DistanceSq(playerChargedProjectile_.position, playerPos) >
            80.0f * 80.0f) {
        playerChargedProjectile_ = {};
    }
}

void GameScene::ReflectArcaneProjectile(size_t swordIndex) {
    ReflectArcaneProjectile(arcaneProjectile_, swordIndex);
}

void GameScene::ReflectArcaneProjectile(ArcaneProjectileState &projectile,
                                        size_t swordIndex) {
    if (!projectile.active || projectile.reflected) {
        return;
    }

    XMFLOAT3 target = enemy_.GetTransform().position;
    target.y += 1.08f;
    const XMFLOAT3 toEnemy{target.x - projectile.position.x,
                           target.y - projectile.position.y,
                           target.z - projectile.position.z};
    const XMFLOAT3 direction =
        NormalizeParticleCompatVec3(toEnemy, {0.0f, 0.0f, 1.0f});
    XMFLOAT3 launchDirection = direction;
    const auto swords = player_.GetSwords();
    if (swordIndex < swords.size() && swords[swordIndex] != nullptr) {
        launchDirection = SlashDirectionToWorld(
            camera_, swords[swordIndex]->GetSlashDirection(), direction);
        launchDirection = NormalizeParticleCompatVec3(
            {launchDirection.x + direction.x * 0.18f,
             launchDirection.y + direction.y * 0.18f,
             launchDirection.z + direction.z * 0.18f},
            direction);
    }
    projectile.reflected = true;
    projectile.waitingToFire = false;
    projectile.reflectedBySwordIndex = swordIndex;
    projectile.reflectedLaunchDirection = launchDirection;
    projectile.previousPosition = projectile.position;
    const float reflectedSpeed = projectile.cataclysm
                                     ? kCataclysmProjectileReflectedSpeed
                                     : kArcaneProjectileReflectedSpeed;
    projectile.velocity = {launchDirection.x * reflectedSpeed,
                           launchDirection.y * reflectedSpeed,
                           launchDirection.z * reflectedSpeed};
    projectile.age = 0.0f;
    projectile.life = projectile.cataclysm ? 4.2f : 3.2f;

    CombatFeedbackEvent feedback{};
    feedback.type = CombatFeedbackEventType::EnemyProjectileReflect;
    feedback.position = projectile.position;
    feedback.direction = launchDirection;
    feedback.power = 5.8f;
    feedback.swordIndex = swordIndex;
    DispatchCombatFeedback(feedback);

    EmitParticleBurst(swordFlashParticles_, projectile.position, 84, 0.38f,
                      AppParticleBurstStyle::Flash, {0.28f, 1.0f, 0.78f, 0.98f},
                      launchDirection, 1.65f);
    EmitParticleBurst(explosionParticles_, projectile.position, 164, 0.42f,
                      AppParticleBurstStyle::SlashLine,
                      {0.20f, 1.0f, 0.88f, 0.92f}, launchDirection, 3.05f);
    EmitParticleBurst(sparkParticles_, projectile.position, 96, 0.28f,
                      AppParticleBurstStyle::Sparks,
                      {0.48f, 1.0f, 0.62f, 0.88f}, launchDirection, 2.45f);
}

void GameScene::EmitArcaneProjectileExplosion(
    const ArcaneProjectileState &projectile, const XMFLOAT3 &position,
    const XMFLOAT3 &direction, bool hitEnemy) {
    const bool cataclysm = projectile.cataclysm;
    const XMFLOAT4 coreColor =
        PickValue(hitEnemy, XMFLOAT4{0.18f, 0.48f, 1.0f, 1.0f},
                  XMFLOAT4{0.12f, 0.40f, 1.0f, 0.96f});
    const XMFLOAT4 hotBlueColor =
        PickValue(hitEnemy, XMFLOAT4{0.62f, 0.84f, 1.0f, 0.98f},
                  XMFLOAT4{0.42f, 0.70f, 1.0f, 0.90f});
    const XMFLOAT4 blackSmokeColor =
        PickValue(hitEnemy, XMFLOAT4{0.025f, 0.030f, 0.040f, 0.88f},
                  XMFLOAT4{0.020f, 0.024f, 0.034f, 0.82f});
    const float scale = PickValue(cataclysm, 1.28f, 1.0f);
    const uint32_t blastCount =
        static_cast<uint32_t>(PickValue(hitEnemy, 310.0f, 250.0f) * scale);
    const uint32_t flashCount =
        static_cast<uint32_t>(PickValue(hitEnemy, 86.0f, 62.0f) * scale);
    const uint32_t sparkCount =
        static_cast<uint32_t>(PickValue(hitEnemy, 210.0f, 165.0f) * scale);
    const uint32_t smokeCount =
        static_cast<uint32_t>(PickValue(hitEnemy, 132.0f, 106.0f) * scale);

    EmitParticleBurst(explosionParticles_, position, blastCount,
                      PickValue(hitEnemy, 0.74f, 0.60f) * scale,
                      AppParticleBurstStyle::Explosion, coreColor, direction,
                      PickValue(hitEnemy, 3.65f, 3.05f) * scale);
    EmitParticleBurst(swordFlashParticles_, position, flashCount, 0.34f * scale,
                      AppParticleBurstStyle::Flash, hotBlueColor, direction,
                      PickValue(hitEnemy, 1.70f, 1.35f) * scale);
    EmitParticleBurst(sparkParticles_, position, sparkCount, 0.38f * scale,
                      AppParticleBurstStyle::Sparks, hotBlueColor, direction,
                      PickValue(hitEnemy, 3.05f, 2.45f) * scale);

    ParticleEmitterSettings blackSmoke{};
    blackSmoke.position = position;
    blackSmoke.emissionType = ParticleEmissionType::Burst;
    blackSmoke.spawnShape = ParticleSpawnShape::Sphere;
    blackSmoke.burstCount = smokeCount;
    blackSmoke.maxParticles = smokeCount;
    blackSmoke.spawnOffsetScale = {0.34f * scale, 0.26f * scale, 0.34f * scale};
    blackSmoke.tintColor = blackSmokeColor;
    blackSmoke.direction = direction;
    blackSmoke.directionalVelocity = PickValue(hitEnemy, 1.28f, 1.04f) * scale;
    blackSmoke.radialVelocity = PickValue(hitEnemy, 0.62f, 0.50f) * scale;
    blackSmoke.velocityBias = {direction.x * 0.38f * scale,
                               direction.y * 0.38f * scale + 0.42f * scale,
                               direction.z * 0.38f * scale};
    blackSmoke.baseLifeTime = PickValue(hitEnemy, 1.18f, 0.96f) * scale;
    blackSmoke.lifeTimeRandom = blackSmoke.baseLifeTime * 0.32f;
    blackSmoke.startScale = 0.24f * scale;
    blackSmoke.endScale = PickValue(hitEnemy, 0.96f, 0.78f) * scale;
    blackSmoke.scaleRandom = 0.18f * scale;
    blackSmoke.turbulence = 0.28f;
    blackSmoke.damping = 0.94f;
    blackSmoke.fadeInTime = 0.06f;
    blackSmoke.fadeOutTime = blackSmoke.baseLifeTime * 0.70f;
    smokeParticles_.EmitOnce(blackSmoke);

    EmitParticleBurst(
        smokeParticles_, position,
        static_cast<uint32_t>(PickValue(hitEnemy, 54.0f, 42.0f) * scale),
        0.62f * scale, AppParticleBurstStyle::Smoke,
        {0.05f, 0.08f, 0.13f, 0.58f}, direction, 1.22f * scale);

    if (soundsLoaded_ && ctx_ != nullptr && ctx_->systems.sound != nullptr) {
        ctx_->systems.sound->Play(explosionSoundId_,
                                  kExplosionSoundVolume *
                                      PickValue(hitEnemy, 1.12f, 0.92f) *
                                      AppSceneServices::GetSeVolume());
    }
}

XMFLOAT2 GameScene::GetArcaneProjectileCueDirection() const {
    if (!arcaneProjectile_.active) {
        return {1.0f, 0.0f};
    }

    return arcaneProjectile_.cueDirection;
}

uint32_t GameScene::GetCurrentEnemyTextureId() const {
    return GetEnemyPhaseTextureId(enemyPhaseMaterials_);
}

void GameScene::ApplyBulletTextureToModel(uint32_t textureId) {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr ||
        bulletModelId_ == 0 || textureId == 0) {
        return;
    }

    Model *bulletModel = ctx_->rendering.model->GetModel(bulletModelId_);
    if (bulletModel == nullptr) {
        return;
    }

    const Model *enemyModel =
        enemyModelId_ != 0 ? ctx_->rendering.model->GetModel(enemyModelId_)
                           : nullptr;

    bulletModel->textureId = textureId;
    for (size_t i = 0; i < bulletModel->subMeshes.size(); ++i) {
        ModelSubMesh &subMesh = bulletModel->subMeshes[i];
        subMesh.textureId = textureId;
        Material material =
            ctx_->rendering.model->GetMaterial(subMesh.materialId);
        if (enemyModel != nullptr && !enemyModel->subMeshes.empty()) {
            const ModelSubMesh &enemySubMesh =
                enemyModel->subMeshes[i % enemyModel->subMeshes.size()];
            material =
                ctx_->rendering.model->GetMaterial(enemySubMesh.materialId);
        }
        material.enableTexture = 1;
        material.baseColorTextureId = textureId;
        ctx_->rendering.model->SetMaterial(subMesh.materialId, material);
    }
}

XMFLOAT2 GameScene::ProjectWorldDirectionToCueDirection(
    const XMFLOAT3 &worldDirection) const {
    XMFLOAT3 worldDir = worldDirection;
    worldDir = NormalizeParticleCompatVec3(worldDir, {1.0f, 0.0f, 0.0f});

    const XMFLOAT3 cameraForward = NormalizeParticleCompatVec3(
        AppCameraForward(camera_), {0.0f, 0.0f, 1.0f});
    const XMFLOAT3 worldUp{0.0f, 1.0f, 0.0f};
    XMFLOAT3 cameraRight = NormalizeParticleCompatVec3(
        CrossParticleCompatVec3(worldUp, cameraForward), {1.0f, 0.0f, 0.0f});
    XMFLOAT3 cameraUp = NormalizeParticleCompatVec3(
        CrossParticleCompatVec3(cameraForward, cameraRight),
        {0.0f, 1.0f, 0.0f});
    const float x = worldDir.x * cameraRight.x + worldDir.y * cameraRight.y +
                    worldDir.z * cameraRight.z;
    const float y = worldDir.x * cameraUp.x + worldDir.y * cameraUp.y +
                    worldDir.z * cameraUp.z;
    const float lenSq = x * x + y * y;
    if (lenSq < 0.010f) {
        return {1.0f, 0.0f};
    }

    const float invLen = 1.0f / std::sqrt(lenSq);
    return {x * invLen, y * invLen};
}

void GameScene::DrawArcaneProjectile() {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr ||
        bulletModelId_ == 0) {
        return;
    }

    auto drawProjectile = [&](const ArcaneProjectileState &state) {
        if (!state.active) {
            return;
        }

        ApplyBulletTextureToModel(GetCurrentEnemyTextureId());

        ModelDrawEffect effect{};
        effect.enabled = true;
        effect.additiveBlend = false;
        effect.disableCulling = true;
        effect.blendOverride = ModelDrawEffectBlendOverride::Opaque;
        effect.color =
            state.reflected
                ? XMFLOAT4{0.38f, 1.0f, 0.58f, 0.42f}
                : EnemyProjectileTint(enemy_.GetBossPhase(),
                                      enemy_.IsPhaseTransitionActive(),
                                      enemy_.IsPhaseTransitionActive()
                                          ? enemy_.GetPhaseTransitionRatio()
                                          : 1.0f);
        effect.intensity = state.reflected ? 0.18f : 0.06f;
        effect.fresnelPower = 0.72f;
        effect.surfaceTint = state.reflected ? 0.18f : 0.06f;
        effect.time = sceneLightTime_;
        ctx_->rendering.model->SetDrawEffect(effect);

        const float pulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 15.0f);
        const float baseScale = state.cataclysm ? 0.74f : 0.62f;
        const float reflectedScale = state.cataclysm ? 0.86f : 0.72f;
        const float scale =
            ((state.reflected ? reflectedScale : baseScale) + pulse * 0.07f) *
            kArcaneProjectileVisualScaleMultiplier;
        const XMFLOAT3 direction =
            NormalizeParticleCompatVec3(state.velocity, {0.0f, 0.0f, 1.0f});
        Transform projectile{};
        projectile.position = state.position;
        const XMVECTOR modelAxis = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        const XMVECTOR moveAxis = XMVector3Normalize(XMLoadFloat3(&direction));
        XMVECTOR rotationAxis = XMVector3Cross(modelAxis, moveAxis);
        float axisLength = XMVectorGetX(XMVector3Length(rotationAxis));
        const float dot = std::clamp(
            XMVectorGetX(XMVector3Dot(modelAxis, moveAxis)), -1.0f, 1.0f);
        XMVECTOR rotation{};
        if (axisLength < 0.0001f) {
            rotation = dot < 0.0f
                           ? XMQuaternionRotationAxis(
                                 XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), kPi)
                           : XMQuaternionIdentity();
        } else {
            rotationAxis = XMVectorScale(rotationAxis, 1.0f / axisLength);
            rotation = XMQuaternionRotationAxis(rotationAxis, std::acos(dot));
        }
        XMStoreFloat4(&projectile.rotation, rotation);
        projectile.scale = {scale, scale, scale};
        ctx_->rendering.model->Draw(bulletModelId_, projectile, camera_);
    };

    drawProjectile(arcaneProjectile_);
    for (const ArcaneProjectileState &projectile : cataclysmProjectiles_) {
        drawProjectile(projectile);
    }
    ctx_->rendering.model->ClearDrawEffect();
}

void GameScene::DrawPlayerChargedProjectile() {
    if (!playerChargedProjectile_.active || ctx_ == nullptr ||
        ctx_->rendering.model == nullptr || bulletModelId_ == 0) {
        return;
    }

    ApplyBulletTextureToModel(GetCurrentEnemyTextureId());

    ModelDrawEffect effect{};
    effect.enabled = true;
    effect.additiveBlend = true;
    effect.disableCulling = true;
    effect.blendOverride = ModelDrawEffectBlendOverride::Opaque;
    effect.color = {1.0f, 0.78f, 0.20f, 0.78f};
    effect.intensity = 0.58f;
    effect.fresnelPower = 0.86f;
    effect.surfaceTint = 0.34f;
    effect.time = sceneLightTime_ * 1.35f;
    ctx_->rendering.model->SetDrawEffect(effect);

    const float pulse = 0.5f + 0.5f * std::sinf(sceneLightTime_ * 22.0f);
    const float scale =
        (0.92f + pulse * 0.16f) * kArcaneProjectileVisualScaleMultiplier;
    const XMFLOAT3 direction = NormalizeParticleCompatVec3(
        playerChargedProjectile_.velocity, {0.0f, 0.0f, 1.0f});
    Transform projectile{};
    projectile.position = playerChargedProjectile_.position;
    const XMVECTOR modelAxis = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMVECTOR moveAxis = XMVector3Normalize(XMLoadFloat3(&direction));
    XMVECTOR rotationAxis = XMVector3Cross(modelAxis, moveAxis);
    float axisLength = XMVectorGetX(XMVector3Length(rotationAxis));
    const float dot = std::clamp(
        XMVectorGetX(XMVector3Dot(modelAxis, moveAxis)), -1.0f, 1.0f);
    XMVECTOR rotation{};
    if (axisLength < 0.0001f) {
        rotation = dot < 0.0f ? XMQuaternionRotationAxis(
                                    XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), kPi)
                              : XMQuaternionIdentity();
    } else {
        rotationAxis = XMVectorScale(rotationAxis, 1.0f / axisLength);
        rotation = XMQuaternionRotationAxis(rotationAxis, std::acos(dot));
    }
    XMStoreFloat4(&projectile.rotation, rotation);
    projectile.scale = {scale, scale, scale};
    ctx_->rendering.model->Draw(bulletModelId_, projectile, camera_);
    ctx_->rendering.model->ClearDrawEffect();
}

void GameScene::EmitReadyPreviewHeatParticles(float deltaTime) {
    if (!readyPreviewMode_ || readyPreviewHeat_ <= 0.015f) {
        return;
    }

    readyPreviewParticleTimer_ -= deltaTime;
    if (readyPreviewParticleTimer_ > 0.0f) {
        return;
    }

    constexpr float kReadyPreviewParticleVisibleHeat = 2.0f / 9.0f;
    constexpr float kReadyPreviewParticleHighHeat = 7.0f / 9.0f;
    const float midHeat = std::clamp(
        (readyPreviewHeat_ - kReadyPreviewParticleVisibleHeat) /
            (kReadyPreviewParticleHighHeat - kReadyPreviewParticleVisibleHeat),
        0.0f, 1.0f);
    const float highHeat =
        std::clamp((readyPreviewHeat_ - kReadyPreviewParticleHighHeat) /
                       (1.0f - kReadyPreviewParticleHighHeat),
                   0.0f, 1.0f);
    const float heat =
        readyPreviewHeat_ < kReadyPreviewParticleVisibleHeat
            ? readyPreviewHeat_ * (0.34f / kReadyPreviewParticleVisibleHeat)
            : 0.34f + 0.51f * SmoothStep01(midHeat);
    const float danger = heat * heat * (3.0f - 2.0f * heat);
    const float highDensity = 1.0f - 0.32f * SmoothStep01(highHeat);
    auto previewCount = [highDensity](float count) {
        return ReadyPreviewParticleCount(count * highDensity);
    };
    readyPreviewParticleTimer_ = 0.120f - 0.070f * danger;
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
                          std::cosf(wave * 0.8f) * (0.10f + 0.22f * danger)};
    const XMFLOAT4 flameColor = ReadyPreviewParticleColor(
        1.0f, 0.30f + 0.48f * heat, 0.04f, 0.88f + 0.12f * danger);
    const XMFLOAT4 smokeColor =
        ReadyPreviewParticleColor(0.26f + 0.36f * danger, 0.18f + 0.10f * heat,
                                  0.12f, 0.60f + 0.28f * danger);

    EmitParticleBurst(smokeParticles_, {core.x, core.y - 0.36f, core.z},
                      previewCount(34.0f + 88.0f * heat + 250.0f * danger),
                      0.82f + 0.58f * heat + 1.24f * danger,
                      AppParticleBurstStyle::Smoke, smokeColor, upward,
                      (0.85f + 1.18f * heat + 2.28f * danger) *
                          kReadyPreviewParticleVelocityScale);
    EmitParticleBurst(sparkParticles_, {core.x, core.y + 0.12f, core.z},
                      previewCount(96.0f + 220.0f * heat + 520.0f * danger),
                      0.30f + 0.34f * heat + 0.70f * danger,
                      AppParticleBurstStyle::Sparks, flameColor, upward,
                      (2.60f + 3.90f * heat + 6.90f * danger) *
                          kReadyPreviewParticleVelocityScale);

    const XMFLOAT3 secondOrigin{enemyPos.x - side * 0.68f,
                                enemyPos.y + 0.62f + 0.42f * danger,
                                enemyPos.z - 0.42f - depth * 0.36f};
    EmitParticleBurst(sparkParticles_, secondOrigin,
                      previewCount(74.0f + 176.0f * heat + 410.0f * danger),
                      0.24f + 0.30f * heat + 0.64f * danger,
                      AppParticleBurstStyle::Sparks,
                      ReadyPreviewParticleColor(1.0f, 0.22f + 0.44f * heat,
                                                0.03f, 0.86f + 0.12f * danger),
                      upward,
                      (2.10f + 3.50f * heat + 6.20f * danger) *
                          kReadyPreviewParticleVelocityScale);

    EmitIntenseReadyPreviewParticles(heat, danger, side, depth, wave, enemyPos,
                                     core, upward);
}

void GameScene::EmitIntenseReadyPreviewParticles(
    float heat, float danger, float side, float depth, float wave,
    const XMFLOAT3 &enemyPos, const XMFLOAT3 &core, const XMFLOAT3 &upward) {
    const float highHeat = std::clamp((readyPreviewHeat_ - 7.0f / 9.0f) /
                                          (1.0f - 7.0f / 9.0f),
                                      0.0f, 1.0f);
    const float density = 1.0f - 0.32f * SmoothStep01(highHeat);
    auto previewCount = [density](float count) {
        return ReadyPreviewParticleCount(count * density);
    };
    const XMFLOAT4 flameColor = ReadyPreviewParticleColor(
        1.0f, 0.30f + 0.48f * heat, 0.04f, 0.88f + 0.12f * danger);
    if (heat > 0.24f) {
        EmitParticleBurst(explosionParticles_, {core.x, core.y + 0.22f, core.z},
                          previewCount(54.0f + 152.0f * heat + 350.0f * danger),
                          0.24f + 0.30f * heat + 0.76f * danger,
                          AppParticleBurstStyle::Explosion, flameColor, upward,
                          (1.75f + 2.80f * heat + 5.20f * danger) *
                              kReadyPreviewParticleVelocityScale);
        EmitParticleBurst(
            smokeParticles_,
            {core.x - side * 0.24f, core.y - 0.04f, core.z - depth * 0.18f},
            previewCount(18.0f + 58.0f * heat + 170.0f * danger),
            0.38f + 0.78f * danger, AppParticleBurstStyle::Flash,
            ReadyPreviewParticleColor(1.0f, 0.44f + 0.18f * heat, 0.08f,
                                      0.30f + 0.42f * danger),
            upward,
            (0.86f + 2.55f * danger) * kReadyPreviewParticleVelocityScale);
    }

    if (heat > 0.52f) {
        const XMFLOAT3 ringOrigin{enemyPos.x - side * 0.92f,
                                  enemyPos.y + 0.98f + 0.36f * danger,
                                  enemyPos.z - 0.36f + depth * 0.74f};
        EmitParticleBurst(
            explosionParticles_, ringOrigin,
            previewCount(110.0f + 360.0f * danger), 0.38f + 0.78f * danger,
            AppParticleBurstStyle::SlashLine,
            ReadyPreviewParticleColor(1.0f, 0.64f, 0.08f,
                                      0.76f + 0.22f * danger),
            {std::cosf(wave), 0.18f + 0.32f * danger, std::sinf(wave)},
            (1.80f + 4.80f * danger) * kReadyPreviewParticleVelocityScale);
        EmitParticleBurst(
            sparkParticles_, {ringOrigin.x, ringOrigin.y + 0.18f, ringOrigin.z},
            previewCount(150.0f + 450.0f * danger), 0.30f + 0.70f * danger,
            AppParticleBurstStyle::Sparks,
            ReadyPreviewParticleColor(1.0f, 0.78f, 0.18f, 0.98f), upward,
            (2.70f + 6.30f * danger) * kReadyPreviewParticleVelocityScale);
    }

    if (heat > 0.72f) {
        const float burstSide =
            std::cosf(wave * 2.3f) * (1.05f + 0.72f * danger);
        const XMFLOAT3 panicOrigin{enemyPos.x + burstSide,
                                   enemyPos.y + 1.12f + 0.38f * danger,
                                   enemyPos.z - 0.30f - depth * 0.46f};
        EmitParticleBurst(
            explosionParticles_, panicOrigin,
            previewCount(260.0f + 520.0f * danger), 0.42f + 0.96f * danger,
            AppParticleBurstStyle::Explosion,
            ReadyPreviewParticleColor(1.0f, 0.10f + 0.30f * heat, 0.02f, 0.94f),
            upward,
            (3.10f + 7.40f * danger) * kReadyPreviewParticleVelocityScale);
        EmitParticleBurst(smokeParticles_,
                          {panicOrigin.x, panicOrigin.y - 0.16f, panicOrigin.z},
                          previewCount(86.0f + 250.0f * danger),
                          0.78f + 1.54f * danger, AppParticleBurstStyle::Flash,
                          ReadyPreviewParticleColor(1.0f, 0.34f, 0.08f,
                                                    0.58f + 0.34f * danger),
                          upward,
                          (1.50f + 4.10f * danger) *
                              kReadyPreviewParticleVelocityScale);
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
    case CombatFeedbackEventType::MistimedCounterSlash:
        EmitParticleBurst(sparkParticles_, position, 76, 0.16f,
                          AppParticleBurstStyle::Sparks,
                          {1.00f, 0.08f, 0.03f, 0.92f}, direction, 2.36f);
        EmitParticleBurst(swordFlashParticles_, position,
                          static_cast<uint32_t>(18.0f + power * 5.0f),
                          0.12f + power * 0.014f, AppParticleBurstStyle::Flash,
                          {1.0f, 0.18f, 0.10f, 0.82f}, direction,
                          0.48f + power * 0.06f);
        EmitParticleBurst(
            explosionParticles_, position,
            static_cast<uint32_t>(68.0f + power * 10.0f),
            0.21f + power * 0.014f, AppParticleBurstStyle::SlashLine,
            {1.00f, 0.04f, 0.02f, 0.86f}, direction, 2.34f + power * 0.24f);
        EmitParticleBurst(smokeParticles_, position, 18, 0.24f,
                          AppParticleBurstStyle::Smoke,
                          {0.34f, 0.08f, 0.06f, 0.34f}, direction, 0.86f);
        break;
    case CombatFeedbackEventType::CounterSuccess:
    case CombatFeedbackEventType::PlayerDamaged:
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
            if (enemy_.IsFarWarpSlashActive()) {
                EmitParticleBurst(swordFlashParticles_, origin, 32, 0.50f,
                                  AppParticleBurstStyle::Flash,
                                  {1.0f, 0.96f, 0.70f, 0.94f}, forward, 0.76f);
                EmitParticleBurst(sparkParticles_, origin, 62, 0.24f,
                                  AppParticleBurstStyle::SpiritSparkle,
                                  {1.0f, 0.88f, 0.42f, 0.70f}, forward, 1.10f);
                break;
            }
            EmitParticleBurst(explosionParticles_, origin, 150, 2.10f,
                              AppParticleBurstStyle::SpiritSparkle,
                              {1.0f, 1.0f, 0.96f, 0.72f}, forward, 1.68f);
            break;
        case ActionKind::Sweep:
            if (enemy_.IsFarWarpSlashActive()) {
                EmitParticleBurst(swordFlashParticles_, origin, 32, 0.50f,
                                  AppParticleBurstStyle::Flash,
                                  {1.0f, 0.96f, 0.70f, 0.94f}, forward, 0.76f);
                EmitParticleBurst(sparkParticles_, origin, 64, 0.24f,
                                  AppParticleBurstStyle::SpiritSparkle,
                                  {1.0f, 0.88f, 0.42f, 0.70f}, forward, 1.16f);
                break;
            }
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
        case ActionKind::ArcaneLaser:
            EmitParticleBurst(swordFlashParticles_,
                              enemy_.GetArcaneLaserMuzzlePosition(), 46, 0.34f,
                              AppParticleBurstStyle::Flash,
                              {0.26f, 1.0f, 0.78f, 0.92f},
                              enemy_.GetArcaneLaserDirection(), 0.72f);
            break;
        case ActionKind::CataclysmLaser:
            EmitParticleBurst(swordFlashParticles_,
                              enemy_.GetCataclysmLaserMuzzlePosition(), 120,
                              0.48f, AppParticleBurstStyle::Flash,
                              {0.18f, 0.90f, 1.0f, 0.98f},
                              enemy_.GetCataclysmLaserDirection(), 1.75f);
            EmitParticleBurst(explosionParticles_,
                              enemy_.GetCataclysmLaserMuzzlePosition(), 220,
                              0.72f, AppParticleBurstStyle::SpiritSparkle,
                              {0.20f, 0.96f, 1.0f, 0.88f},
                              enemy_.GetCataclysmLaserDirection(), 3.25f);
            break;
        default:
            break;
        }
    }
}

void GameScene::EmitArcaneLaserParticles(float deltaTime) {
    const bool isCataclysmLaser =
        enemy_.GetActionKind() == ActionKind::CataclysmLaser;
    const bool anyProjectileActive = HasActiveArcaneProjectiles();
    if (enemy_.GetActionKind() != ActionKind::ArcaneLaser &&
        !isCataclysmLaser && !anyProjectileActive) {
        arcaneLaserParticleTimer_ = 0.0f;
        return;
    }

    arcaneLaserParticleTimer_ =
        (std::max)(0.0f, arcaneLaserParticleTimer_ - deltaTime);
    if (arcaneLaserParticleTimer_ > 0.0f) {
        return;
    }

    const ActionStep step = enemy_.GetActionStep();
    const XMFLOAT3 muzzle = isCataclysmLaser
                                ? enemy_.GetCataclysmLaserMuzzlePosition()
                                : enemy_.GetArcaneLaserMuzzlePosition();
    const XMFLOAT3 direction = isCataclysmLaser
                                   ? enemy_.GetCataclysmLaserDirection()
                                   : enemy_.GetArcaneLaserDirection();
    const XMFLOAT3 circlePos{muzzle.x + direction.x * 0.34f, muzzle.y + 0.04f,
                             muzzle.z + direction.z * 0.34f};

    if (step == ActionStep::Charge) {
        EmitArcaneLaserChargeParticles(isCataclysmLaser, muzzle, direction);
        return;
    }

    if (step != ActionStep::Active && !anyProjectileActive) {
        arcaneLaserParticleTimer_ = 0.13f;
        EmitParticleBurst(smokeParticles_, circlePos, 18, 0.38f,
                          AppParticleBurstStyle::Flash,
                          {0.18f, 0.82f, 0.72f, 0.34f}, direction, 0.32f);
        return;
    }

    if (!anyProjectileActive) {
        return;
    }

    const bool anyReflectedProjectile = HasReflectedArcaneProjectiles();
    arcaneLaserParticleTimer_ = anyReflectedProjectile ? 0.014f : 0.034f;
    EmitArcaneProjectileTrail(arcaneProjectile_, direction);
    for (const ArcaneProjectileState &projectile : cataclysmProjectiles_) {
        EmitArcaneProjectileTrail(projectile, direction);
    }
}

void GameScene::EmitArcaneLaserChargeParticles(bool cataclysmLaser,
                                               const XMFLOAT3 &muzzle,
                                               const XMFLOAT3 &direction) {
    (void)muzzle;
    (void)direction;
    if (!cataclysmLaser) {
        arcaneLaserParticleTimer_ = 0.10f;
        return;
    }
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const float yaw = enemy_.GetTelegraphYaw();
    const XMFLOAT3 forward{std::sinf(yaw), 0.0f, std::cosf(yaw)};
    const XMFLOAT3 right{forward.z, 0.0f, -forward.x};
    const XMFLOAT3 thrustDir{-forward.x * 0.18f, -1.0f, -forward.z * 0.18f};
    const float chargeTime = enemy_.GetActionTimerForPresentation();
    const bool initialBlast = chargeTime < 0.075f;
    const float blastFade =
        1.0f -
        SmoothStep01(std::clamp((chargeTime - 0.075f) / 0.36f, 0.0f, 1.0f));
    const float jetScale =
        PickValue(initialBlast, 3.60f, 0.82f + 1.45f * blastFade);
    for (float side : {-0.42f, 0.42f}) {
        XMFLOAT3 footPos{enemyPos.x + right.x * side, enemyPos.y + 0.12f,
                         enemyPos.z + right.z * side};
        EmitParticleBurst(
            sparkParticles_, footPos, PickValue(initialBlast, 130u, 42u),
            PickValue(initialBlast, 0.34f, 0.20f),
            AppParticleBurstStyle::Sparks, {0.24f, 0.96f, 1.0f, 0.86f},
            thrustDir, 2.15f * jetScale);
        EmitParticleBurst(
            smokeParticles_, footPos, PickValue(initialBlast, 64u, 20u),
            PickValue(initialBlast, 0.58f, 0.34f), AppParticleBurstStyle::Smoke,
            {0.18f, 0.82f, 0.90f, 0.34f}, thrustDir, 1.10f * jetScale);
        if (initialBlast) {
            EmitParticleBurst(explosionParticles_, footPos, 72, 0.42f,
                              AppParticleBurstStyle::Flash,
                              {0.30f, 1.0f, 0.96f, 0.58f}, thrustDir,
                              0.86f * jetScale);
        }
    }
    arcaneLaserParticleTimer_ = PickValue(initialBlast, 0.11f, 0.045f);
}

void GameScene::EmitArcaneProjectileTrail(
    const ArcaneProjectileState &projectile,
    const XMFLOAT3 &fallbackDirection) {
    if (!projectile.active) {
        return;
    }
    const XMFLOAT3 projectileDir =
        NormalizeParticleCompatVec3(projectile.velocity, fallbackDirection);
    EmitArcaneProjectileTrailBursts(projectile, projectileDir);
    EmitArcaneProjectileTrailSmoke(projectile, projectileDir);
}

void GameScene::EmitArcaneProjectileTrailBursts(
    const ArcaneProjectileState &projectile, const XMFLOAT3 &projectileDir) {
    const XMFLOAT4 projectileColor = projectile.reflected
                                         ? XMFLOAT4{0.32f, 1.0f, 0.54f, 0.88f}
                                         : XMFLOAT4{0.20f, 0.96f, 1.0f, 0.86f};
    const XMFLOAT4 smokeColor = projectile.reflected
                                    ? XMFLOAT4{0.16f, 0.92f, 0.78f, 0.36f}
                                    : XMFLOAT4{0.96f, 0.76f, 0.34f, 0.28f};
    const uint32_t flashCount = projectile.reflected ? 18u : 10u;
    const uint32_t sparkleCount = projectile.reflected ? 52u : 26u;
    const uint32_t smokeCount = projectile.reflected ? 14u : 8u;
    const float trailScale = projectile.reflected ? 2.25f : 1.05f;

    EmitParticleBurst(swordFlashParticles_, projectile.position, flashCount,
                      projectile.reflected ? 0.18f : 0.14f,
                      AppParticleBurstStyle::Flash, projectileColor,
                      projectileDir, projectile.cataclysm ? 0.48f : 0.34f);
    EmitParticleBurst(explosionParticles_, projectile.position, sparkleCount,
                      projectile.reflected ? 0.30f : 0.26f,
                      AppParticleBurstStyle::SpiritSparkle, projectileColor,
                      {-projectileDir.x, -projectileDir.y, -projectileDir.z},
                      trailScale);
    EmitParticleBurst(smokeParticles_, projectile.position, smokeCount,
                      projectile.reflected ? 0.34f : 0.28f,
                      AppParticleBurstStyle::Smoke, smokeColor,
                      {-projectileDir.x, -projectileDir.y, -projectileDir.z},
                      projectile.reflected ? 0.56f : 0.38f);
}

void GameScene::EmitArcaneProjectileTrailSmoke(
    const ArcaneProjectileState &projectile, const XMFLOAT3 &projectileDir) {
    const XMFLOAT4 lingeringSmokeColor =
        PickValue(projectile.reflected, XMFLOAT4{0.13f, 0.82f, 0.76f, 0.34f},
                  XMFLOAT4{0.92f, 0.70f, 0.30f, 0.25f});
    const float backOffset = PickValue(projectile.cataclysm,
                                       kCataclysmProjectileSmokeTrailBackOffset,
                                       kEnemyProjectileSmokeTrailBackOffset);
    const XMFLOAT3 smokeOrigin{
        projectile.position.x - projectileDir.x * backOffset,
        projectile.position.y - projectileDir.y * backOffset - 0.03f,
        projectile.position.z - projectileDir.z * backOffset};

    ParticleEmitterSettings trailSmoke{};
    trailSmoke.position = smokeOrigin;
    trailSmoke.emissionType = ParticleEmissionType::Burst;
    trailSmoke.spawnShape = ParticleSpawnShape::Sphere;
    trailSmoke.burstCount = PickValue(
        projectile.reflected, PickValue(projectile.cataclysm, 22u, 17u),
        PickValue(projectile.cataclysm, 18u, 14u));
    trailSmoke.maxParticles = trailSmoke.burstCount;
    trailSmoke.spawnOffsetScale =
        PickValue(projectile.cataclysm, XMFLOAT3{0.24f, 0.18f, 0.24f},
                  XMFLOAT3{0.18f, 0.13f, 0.18f});
    trailSmoke.tintColor = lingeringSmokeColor;
    trailSmoke.direction = {-projectileDir.x, -projectileDir.y,
                            -projectileDir.z};
    trailSmoke.directionalVelocity =
        PickValue(projectile.reflected, 0.82f,
                  PickValue(projectile.cataclysm, 0.76f, 0.54f));
    trailSmoke.radialVelocity =
        PickValue(projectile.reflected, 0.24f,
                  PickValue(projectile.cataclysm, 0.22f, 0.16f));
    trailSmoke.velocityBias = {-projectileDir.x * 0.72f,
                               -projectileDir.y * 0.72f,
                               -projectileDir.z * 0.72f};
    trailSmoke.baseLifeTime =
        PickValue(projectile.reflected, 0.68f,
                  PickValue(projectile.cataclysm, 0.74f, 0.58f));
    trailSmoke.lifeTimeRandom = trailSmoke.baseLifeTime * 0.34f;
    trailSmoke.startScale =
        PickValue(projectile.reflected, 0.14f,
                  PickValue(projectile.cataclysm, 0.16f, 0.12f));
    trailSmoke.endScale =
        PickValue(projectile.reflected, 0.50f,
                  PickValue(projectile.cataclysm, 0.58f, 0.42f));
    trailSmoke.scaleRandom = 0.08f;
    trailSmoke.turbulence =
        PickValue(projectile.reflected, 0.18f,
                  PickValue(projectile.cataclysm, 0.15f, 0.10f));
    trailSmoke.damping = 0.96f;
    trailSmoke.fadeInTime = 0.04f;
    trailSmoke.fadeOutTime = trailSmoke.baseLifeTime * 0.62f;
    smokeParticles_.EmitOnce(trailSmoke);
}

void GameScene::EmitEnemyCueParticles(float deltaTime) {
    swordSlashArcRenderer_.ClearDirectionCueLines();

    if (ctx_ == nullptr || battleIntroActive_ || victorySequenceActive_ ||
        defeatSequenceActive_ || bladeClashFinishActive_) {
        return;
    }

    EnemyAttackCueEvent cueEvent{};
    while (enemy_.ConsumeAttackCueEvent(cueEvent)) {
        (void)cueEvent;
    }

    if (EmitArcaneProjectileCues()) {
        return;
    }

    EmitTripleIaiCueLines();

    const ActionKind kind = enemy_.GetActionKind();
    const ActionStep step = enemy_.GetActionStep();
    const bool farWarpSlashActive = enemy_.IsFarWarpSlashActive();
    if (!SupportsEnemyChargeCue(kind, step, farWarpSlashActive)) {
        farSlashChargeParticleTimer_ = 0.0f;
        return;
    }

    UpdateFarSlashCueParticles(deltaTime, step, farWarpSlashActive);

    const bool farWarpActiveCueLine =
        farWarpSlashActive && step == ActionStep::Active;
    bool releaseCounterCueVisible =
        kind == ActionKind::BladeClash ||
        (!farWarpSlashActive && enemy_.GetReleaseAnticipationRatio() > 0.0f) ||
        farWarpActiveCueLine;
    if (!AdjustTutorialReleaseCue(kind, releaseCounterCueVisible)) {
        return;
    }

    if (ShouldSuppressEnemyAttackCue(releaseCounterCueVisible,
                                     farWarpSlashActive)) {
        return;
    }

    EmitEnemyAttackCueLine(kind, releaseCounterCueVisible);
}

bool GameScene::EmitArcaneProjectileCue(
    const ArcaneProjectileState &projectile) {
    if (!projectile.active || projectile.reflected) {
        return false;
    }
    const XMFLOAT3 playerPos = player_.GetTransform().position;
    const float dx = projectile.position.x - playerPos.x;
    const float dz = projectile.position.z - playerPos.z;
    const bool canDeflect =
        (!projectile.waitingToFire || projectile.cataclysm) &&
        (!projectile.fromAbove ||
         projectile.position.y <=
             playerPos.y + kCataclysmProjectileDeflectHeight) &&
        dx * dx + dz * dz <=
            kArcaneProjectileDeflectRange * kArcaneProjectileDeflectRange;
    const XMFLOAT4 lineColor =
        PickValue(canDeflect, XMFLOAT4{0.20f, 1.0f, 0.32f, 1.0f},
                  XMFLOAT4{1.0f, 0.06f, 0.06f, 1.0f});
    XMFLOAT3 cuePos = projectile.position;
    cuePos.y += 0.08f;
    swordSlashArcRenderer_.EmitDirectionCueLine(
        cuePos, projectile.cueDirection, camera_, lineColor, canDeflect, 1.55f);
    return true;
}

bool GameScene::HasReflectedArcaneProjectiles() const {
    if (arcaneProjectile_.active && arcaneProjectile_.reflected) {
        return true;
    }
    return std::ranges::any_of(
        cataclysmProjectiles_, [](const ArcaneProjectileState &projectile) {
            return projectile.active && projectile.reflected;
        });
}

bool GameScene::EmitArcaneProjectileCues() {
    bool visible = EmitArcaneProjectileCue(arcaneProjectile_);
    for (const ArcaneProjectileState &projectile : cataclysmProjectiles_) {
        visible = EmitArcaneProjectileCue(projectile) || visible;
    }
    return visible;
}

void GameScene::EmitTripleIaiCueLines() {
    if (!enemy_.IsTripleIaiSlashActive()) {
        return;
    }
    const XMFLOAT3 enemyBodyPos = enemy_.GetTransform().position;
    for (int i = 0; i < 4; ++i) {
        XMFLOAT3 slotPos{};
        ActionKind slotKind = ActionKind::None;
        if (!enemy_.GetTripleIaiCueSlot(i, slotPos, slotKind)) {
            continue;
        }
        const SwordCounterAxis cueAxis =
            RequiredVisualCounterAxisForAction(slotKind);
        if (cueAxis == SwordCounterAxis::None) {
            continue;
        }
        XMFLOAT3 cuePos = slotPos;
        cuePos.y += 1.28f;
        const bool isCurrentAttacker =
            enemy_.IsFarWarpSlashActive() &&
            DistanceSq(slotPos, enemyBodyPos) <= 0.55f * 0.55f;
        const bool canCounterCurrent =
            isCurrentAttacker &&
            (enemy_.GetActionStep() == ActionStep::Active ||
             enemy_.GetReleaseAnticipationRatio() > 0.0f);
        const XMFLOAT4 lineColor =
            PickValue(canCounterCurrent, XMFLOAT4{0.20f, 1.0f, 0.32f, 1.0f},
                      XMFLOAT4{1.0f, 0.06f, 0.06f, 1.0f});
        const XMFLOAT2 cueDirection =
            PickValue(cueAxis == SwordCounterAxis::Vertical,
                      XMFLOAT2{0.0f, 1.0f}, XMFLOAT2{1.0f, 0.0f});
        swordSlashArcRenderer_.EmitDirectionCueLine(
            cuePos, cueDirection, camera_, lineColor, canCounterCurrent);
    }
}

void GameScene::UpdateFarSlashCueParticles(float deltaTime, ActionStep step,
                                           bool farWarpSlashActive) {
    if (!farWarpSlashActive) {
        farSlashChargeParticleTimer_ = 0.0f;
        return;
    }
    farSlashChargeParticleTimer_ =
        (std::max)(0.0f, farSlashChargeParticleTimer_ - deltaTime);
    if (farSlashChargeParticleTimer_ > 0.0f) {
        return;
    }
    XMFLOAT3 position = enemy_.GetTransform().position;
    const float yaw = enemy_.GetTelegraphYaw();
    if (step == ActionStep::Charge || step == ActionStep::Hold) {
        position.y += 1.05f;
        const XMFLOAT3 inward{-std::sinf(yaw), 0.26f, -std::cosf(yaw)};
        EmitParticleBurst(smokeParticles_, position, 18, 0.32f,
                          AppParticleBurstStyle::SpiritSparkle,
                          {1.0f, 0.92f, 0.58f, 0.42f}, inward, 0.72f);
        farSlashChargeParticleTimer_ = 0.075f;
        return;
    }
    if (step == ActionStep::Active) {
        position.y += 1.00f;
        const XMFLOAT3 rushTrail{-std::sinf(yaw), 0.08f, -std::cosf(yaw)};
        EmitParticleBurst(sparkParticles_, position, 24, 0.18f,
                          AppParticleBurstStyle::SlashLine,
                          {1.0f, 0.94f, 0.58f, 0.78f}, rushTrail, 1.18f);
        EmitParticleBurst(smokeParticles_, position, 16, 0.26f,
                          AppParticleBurstStyle::SpiritSparkle,
                          {1.0f, 0.88f, 0.48f, 0.46f}, rushTrail, 0.82f);
        farSlashChargeParticleTimer_ = 0.030f;
    }
}

bool GameScene::AdjustTutorialReleaseCue(ActionKind kind,
                                         bool &releaseCounterCueVisible) const {
    if (!tutorialMode_ ||
        (kind != ActionKind::Smash && kind != ActionKind::Sweep)) {
        return true;
    }
    if (tutorialStep_ == kTutorialStepRedSmash ||
        tutorialStep_ == kTutorialStepRedSweep) {
        releaseCounterCueVisible = false;
        return true;
    }
    return (tutorialStep_ != kTutorialStepGreenSmash &&
            tutorialStep_ != kTutorialStepGreenSweep) ||
           releaseCounterCueVisible;
}

bool GameScene::SupportsEnemyChargeCue(ActionKind kind, ActionStep step,
                                       bool farWarpSlashActive) const {
    const bool supportedAction = kind == ActionKind::Smash ||
                                 kind == ActionKind::Sweep ||
                                 kind == ActionKind::BladeClash;
    const bool supportedStep =
        step == ActionStep::Charge || step == ActionStep::Hold ||
        (farWarpSlashActive && step == ActionStep::Active) ||
        (kind == ActionKind::BladeClash && step == ActionStep::Active);
    return supportedAction && supportedStep;
}

bool GameScene::ShouldSuppressEnemyAttackCue(bool releaseCounterCueVisible,
                                             bool farWarpSlashActive) const {
    return !releaseCounterCueVisible && enemy_.ShouldSuppressRedAttackCue() &&
           !farWarpSlashActive;
}

void GameScene::EmitEnemyAttackCueLine(ActionKind kind,
                                       bool releaseCounterCueVisible) {
    const float yaw = enemy_.GetTelegraphYaw();
    const XMFLOAT3 forward = {std::sinf(yaw), 0.12f, std::cosf(yaw)};
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    XMFLOAT3 cuePos = enemyPos;
    cuePos.x += forward.x * 1.18f;
    cuePos.y += 1.28f;
    cuePos.z += forward.z * 1.18f;

    const XMFLOAT4 lineColor = releaseCounterCueVisible
                                   ? XMFLOAT4{0.20f, 1.0f, 0.32f, 1.0f}
                                   : XMFLOAT4{1.0f, 0.06f, 0.06f, 1.0f};

    const SwordCounterAxis cueAxis = RequiredVisualCounterAxisForAction(kind);
    if (cueAxis != SwordCounterAxis::None) {
        swordSlashArcRenderer_.EmitDirectionCueLine(
            cuePos,
            cueAxis == SwordCounterAxis::Vertical ? XMFLOAT2{0.0f, 1.0f}
                                                  : XMFLOAT2{1.0f, 0.0f},
            camera_, lineColor, releaseCounterCueVisible);
    }
}

void GameScene::UpdateBattlePostProcessState(float deltaTime) {
    (void)deltaTime;
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
    const float hudAlpha =
        defeatSequenceActive_ ? 1.0f - GetDefeatFadeToBlackRatio() : 1.0f;
    hud_.Draw(*ctx_, hudAlpha);
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
        ctx_->rendering.sprite == nullptr ||
        ctx_->rendering.texture == nullptr) {
        return;
    }
    cameraPreviewReceiver_.Draw(ctx_->rendering.sprite, ctx_->rendering.texture,
                                kHandCameraPreviewStaleSeconds);
}

void GameScene::UpdatePhaseTransitionCinematic(float deltaTime) {
    ClearSwordVfx();
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
        ApplyBattlePostProcess(ctx_, postEffectCinematicLayer_,
                               0.010f + 0.026f * hold + 0.036f * release,
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
        ClearBattlePostProcess(ctx_, postEffectCinematicLayer_);
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
                                  kEnemyReleaseSoundVolume *
                                      AppSceneServices::GetSeVolume());
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
    const bool toPhase3 = enemy_.GetBossPhase() == BossPhase::Phase3;
    const XMFLOAT4 sparkColor = toPhase3 ? XMFLOAT4{1.0f, 0.82f, 0.20f, 0.86f}
                                         : XMFLOAT4{1.0f, 0.28f, 0.04f, 0.86f};
    const XMFLOAT4 flashColor = toPhase3 ? XMFLOAT4{1.0f, 0.78f, 0.18f, 0.52f}
                                         : XMFLOAT4{1.0f, 0.20f, 0.04f, 0.52f};
    const uint32_t sparkCount = static_cast<uint32_t>(28.0f + 44.0f * hold);
    EmitParticleBurst(sparkParticles_, origin, sparkCount, 0.58f + 0.46f * hold,
                      AppParticleBurstStyle::Sparks, sparkColor,
                      {0.0f, 1.0f, 0.0f}, 2.4f + 2.0f * hold);
    if (hold > 0.35f) {
        EmitParticleBurst(smokeParticles_, origin, 3, 0.58f,
                          AppParticleBurstStyle::Flash, flashColor,
                          {0.0f, 1.0f, 0.0f}, 0.38f);
    }
}

void GameScene::EmitPhaseTransitionReleaseEffects() {
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 origin{enemyPos.x, enemyPos.y + 1.30f, enemyPos.z};
    const bool toPhase3 = enemy_.GetBossPhase() == BossPhase::Phase3;
    const XMFLOAT4 explosionColor = toPhase3
                                        ? XMFLOAT4{1.0f, 0.76f, 0.18f, 0.92f}
                                        : XMFLOAT4{1.0f, 0.30f, 0.05f, 0.92f};
    const XMFLOAT4 smokeColor = toPhase3 ? XMFLOAT4{1.0f, 0.74f, 0.22f, 0.76f}
                                         : XMFLOAT4{1.0f, 0.58f, 0.12f, 0.76f};
    EmitParticleBurst(explosionParticles_, origin, 110, 1.38f,
                      AppParticleBurstStyle::Explosion, explosionColor,
                      {0.0f, 1.0f, 0.0f}, 3.0f);
    EmitParticleBurst(sparkParticles_, origin, 230, 1.85f,
                      AppParticleBurstStyle::Sparks, {1.0f, 0.86f, 0.30f, 1.0f},
                      {0.0f, 1.0f, 0.0f}, 7.0f);
    EmitParticleBurst(smokeParticles_, origin, 52, 1.42f,
                      AppParticleBurstStyle::Flash, smokeColor,
                      {0.0f, 1.0f, 0.0f}, 1.0f);
    if (soundsLoaded_ && ctx_ != nullptr && ctx_->systems.sound != nullptr) {
        ctx_->systems.sound->Play(explosionSoundId_,
                                  kExplosionSoundVolume *
                                      AppSceneServices::GetSeVolume());
    }
}

void GameScene::UpdateBattleIntro(float deltaTime) {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr) {
        return;
    }
    auto *const modelManager = ctx_->rendering.model;
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
        ApplyBattlePostProcess(ctx_, postEffectCinematicLayer_,
                               0.035f * (1.0f - ratio), 0.16f + 0.06f * ratio,
                               0.0f, 0.48f, 18);
    } else if (!titleDemoMode_) {
        ApplyReleasedPostProcess(ctx_, postEffectCinematicLayer_);
    }
    if (!battleIntroRevealEmitted_ && battleIntroTimer_ >= 2.36f) {
        battleIntroRevealEmitted_ = true;
        combatFeedback_.AddCameraShake(0.38f, 0.055f, 0.034f);
        if (!titleDemoMode_) {
            ApplyReleasedPostProcess(ctx_, postEffectCinematicLayer_);
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
                                      kEnemyReleaseSoundVolume *
                                          AppSceneServices::GetSeVolume());
        }
    }

    modelManager->UpdateAnimation(playerModelId_, deltaTime * 0.04f);
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
    ClearBattlePostProcess(ctx_, postEffectCinematicLayer_);
    if (!titleDemoMode_) {
        ApplyReleasedPostProcess(ctx_, postEffectCinematicLayer_);
    }
    ApplyReleasedClearColor(ctx_->rendering.dxCommon);
    enemy_.FaceTargetImmediately(player_.GetTransform().position);
    enemy_.BeginDifficultyNineOpeningCutIn(player_.GetTransform().position);
    SyncEnemyAnimation();
    SetEnemyAnimationFrozen(false);
    UpdateSceneLighting();
    UpdateBattleCamera();
    camera_.UpdateMatrices();
}

void GameScene::ApplyEnemyPhaseMaterials() {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr ||
        enemyModelId_ == 0) {
        return;
    }

    const BossPhase phase = enemy_.GetBossPhase();
    const float transitionRatio = enemy_.IsPhaseTransitionActive()
                                      ? enemy_.GetPhaseTransitionRatio()
                                      : 1.0f;
    ApplyEnemyPhaseMaterial(ctx_->rendering.model, ctx_->rendering.texture,
                            enemyModelId_, enemyPhaseMaterials_, phase,
                            enemy_.IsPhaseTransitionActive(), transitionRatio);
    if (!arcaneProjectile_.active) {
        ApplyBulletTextureToModel(enemyPhaseMaterials_.currentTextureId);
    }
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
    const float glowAlpha = dissolveActive ? (0.18f + alpha * 0.82f) : 1.0f;
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
    EmitBladeClashWinGuardBreak();
    if (bladeClashFinishPlayerWon_) {
        UpdateBladeClashWinFinish(bladeClashWinActionTimer);
    } else {
        UpdateBladeClashLossFinish();
    }

    if (bladeClashFinishTimer_ >= bladeClashFinishDuration_) {
        CompleteBladeClashFinish();
    }
}

void GameScene::EmitBladeClashWinGuardBreak() {
    if (bladeClashFinishPlayerWon_ && !bladeClashFinishGuardBreakEmitted_ &&
        bladeClashFinishTimer_ >= Clash::kWinGuardBreakImpactTime) {
        bladeClashFinishGuardBreakEmitted_ = true;
        XMFLOAT3 breakCenter = enemy_.GetTransform().position;
        breakCenter.y += 1.22f;
        EmitParticleBurst(
            explosionParticles_, breakCenter, 126, 0.94f,
            AppParticleBurstStyle::Explosion, {1.0f, 0.88f, 0.48f, 0.82f},
            {-bladeClashDirection_.x, 0.16f, -bladeClashDirection_.z}, 2.05f);
        EmitParticleBurst(
            sparkParticles_, breakCenter, 220, 0.84f,
            AppParticleBurstStyle::Sparks, {1.0f, 0.94f, 0.52f, 0.96f},
            {bladeClashDirection_.z, 0.18f, -bladeClashDirection_.x}, 2.90f);
        EmitParticleBurst(
            explosionParticles_, breakCenter, 92, 0.72f,
            AppParticleBurstStyle::SpiritSparkle, {0.36f, 0.92f, 1.0f, 0.78f},
            {-bladeClashDirection_.x, 0.36f, -bladeClashDirection_.z}, 1.55f);
        EmitParticleBurst(
            swordFlashParticles_, breakCenter, 18, 0.66f,
            AppParticleBurstStyle::Flash, {1.0f, 1.0f, 0.86f, 0.82f},
            {-bladeClashDirection_.x, 0.0f, -bladeClashDirection_.z}, 0.48f);

        CombatFeedbackEvent guardBreakFeedback{};
        guardBreakFeedback.type = CombatFeedbackEventType::BladeClashGuardBreak;
        guardBreakFeedback.position = breakCenter;
        guardBreakFeedback.direction = {-bladeClashDirection_.x, 0.0f,
                                        -bladeClashDirection_.z};
        guardBreakFeedback.power = 14.0f;
        DispatchCombatFeedback(guardBreakFeedback);
    }
}

void GameScene::UpdateBladeClashWinFinish(float bladeClashWinActionTimer) {
    if (!bladeClashFinishImpactEmitted_ && bladeClashWinActionTimer >= 0.08f) {
        bladeClashFinishImpactEmitted_ = true;
        XMFLOAT3 cutCenter = enemy_.GetTransform().position;
        cutCenter.y += 1.18f;
        EmitParticleBurst(
            explosionParticles_, cutCenter, 190, 1.02f,
            AppParticleBurstStyle::SlashLine, {1.0f, 0.94f, 0.62f, 0.88f},
            {bladeClashDirection_.z, 0.12f, -bladeClashDirection_.x}, 2.45f);
        EmitParticleBurst(
            explosionParticles_, cutCenter, 118, 0.72f,
            AppParticleBurstStyle::SlashLine, {0.34f, 0.94f, 1.0f, 0.70f},
            {bladeClashDirection_.x, 0.02f, bladeClashDirection_.z}, 2.10f);
        EmitParticleBurst(
            explosionParticles_, cutCenter, 88, 0.76f,
            AppParticleBurstStyle::Explosion, {1.0f, 0.78f, 0.34f, 0.64f},
            {bladeClashDirection_.x, 0.16f, bladeClashDirection_.z}, 1.55f);
        EmitParticleBurst(
            swordFlashParticles_, cutCenter, 24, 0.52f,
            AppParticleBurstStyle::Flash, {1.0f, 0.98f, 0.84f, 0.84f},
            {bladeClashDirection_.z, 0.0f, -bladeClashDirection_.x}, 0.42f);
        EmitParticleBurst(
            sparkParticles_, cutCenter, 180, 0.42f,
            AppParticleBurstStyle::Sparks, {1.0f, 0.84f, 0.30f, 0.88f},
            {bladeClashDirection_.z, 0.16f, -bladeClashDirection_.x}, 2.20f);
        EmitParticleBurst(
            smokeParticles_, cutCenter, 42, 0.72f, AppParticleBurstStyle::Flash,
            {0.40f, 0.92f, 1.0f, 0.42f},
            {-bladeClashDirection_.x, 0.18f, -bladeClashDirection_.z}, 0.78f);

        CombatFeedbackEvent slashFeedback{};
        slashFeedback.type = CombatFeedbackEventType::BladeClashPierce;
        slashFeedback.position = cutCenter;
        slashFeedback.direction = {bladeClashDirection_.z, 0.0f,
                                   -bladeClashDirection_.x};
        slashFeedback.power = 13.0f;
        DispatchCombatFeedback(slashFeedback);

        XMFLOAT3 cinematicCutCenter = cutCenter;
        cinematicCutCenter.y += 0.04f;
        swordSlashArcRenderer_.EmitCinematicCutLine(
            cinematicCutCenter,
            {bladeClashDirection_.z, 0.0f, -bladeClashDirection_.x}, camera_,
            4.2f);
    }
    if (!bladeClashFinishSkidEmitted_ && bladeClashWinActionTimer >= 0.76f) {
        bladeClashFinishSkidEmitted_ = true;
        XMFLOAT3 skidCenter = {
            enemy_.GetTransform().position.x + bladeClashDirection_.x * 0.55f,
            player_.GetTransform().position.y + 0.48f,
            enemy_.GetTransform().position.z + bladeClashDirection_.z * 0.55f};
        EmitParticleBurst(
            explosionParticles_, skidCenter, 126, 0.78f,
            AppParticleBurstStyle::SlashLine, {1.0f, 0.82f, 0.36f, 0.66f},
            {bladeClashDirection_.z, 0.02f, -bladeClashDirection_.x}, 1.82f);
        EmitParticleBurst(
            sparkParticles_, skidCenter, 120, 0.54f,
            AppParticleBurstStyle::Sparks, {1.0f, 0.76f, 0.24f, 0.86f},
            {-bladeClashDirection_.x, 0.04f, -bladeClashDirection_.z}, 1.65f);
        EmitParticleBurst(smokeParticles_, skidCenter, 64, 0.88f,
                          AppParticleBurstStyle::Smoke,
                          {0.36f, 0.30f, 0.25f, 0.52f}, {0.0f, 0.12f, 0.0f},
                          0.76f);
    }

    UpdateBladeClashWinPose(bladeClashWinActionTimer);
}

void GameScene::UpdateBladeClashWinPose(float bladeClashWinActionTimer) {
    const float finishYaw =
        std::atan2f(bladeClashDirection_.x, bladeClashDirection_.z);
    player_.SetDefeatPoseRatio(0.0f);
    if (bladeClashWinActionTimer <= 0.0f) {
        const float guardT = std::clamp(
            bladeClashFinishTimer_ / Clash::kWinGuardBreakLead, 0.0f, 1.0f);
        const float brace = guardT < 0.08f ? guardT / 0.08f : 1.0f;
        const float strain =
            std::sinf(std::clamp(guardT / 0.50f, 0.0f, 1.0f) * kPi);
        const float collapseT = std::clamp(
            (bladeClashFinishTimer_ - Clash::kWinGuardBreakImpactTime) /
                (Clash::kWinGuardBreakLead - Clash::kWinGuardBreakImpactTime),
            0.0f, 1.0f);
        const float collapseEase = 1.0f - std::pow(1.0f - collapseT, 4.0f);
        const float snap =
            std::sinf(std::clamp((bladeClashFinishTimer_ -
                                  Clash::kWinGuardBreakImpactTime) /
                                     0.06f,
                                 0.0f, 1.0f) *
                      kPi);
        XMFLOAT3 bracePos = bladeClashFinishPlayerStart_;
        bracePos.x += bladeClashDirection_.x *
                      (0.16f * brace + 0.38f * strain + 0.18f * snap);
        bracePos.z += bladeClashDirection_.z *
                      (0.16f * brace + 0.38f * strain + 0.18f * snap);
        player_.SetCinematicBladeClashPose(
            bracePos, finishYaw,
            std::clamp(0.54f + 0.24f * strain + 0.18f * snap, 0.0f, 1.0f));

        XMFLOAT3 enemyPos = {
            bladeClashFinishEnemyStart_.x +
                bladeClashDirection_.x *
                    (-0.10f * strain + 0.06f * snap +
                     Clash::kGuardBreakRecoilDistance * collapseEase),
            bladeClashFinishEnemyStart_.y - 0.10f * strain + 0.04f * snap +
                (Clash::kGuardBreakLift - Clash::kGuardBreakDrop * 0.85f) *
                    collapseEase,
            bladeClashFinishEnemyStart_.z +
                bladeClashDirection_.z *
                    (-0.10f * strain + 0.06f * snap +
                     Clash::kGuardBreakRecoilDistance * collapseEase)};
        const float enemyYaw =
            std::atan2f(-bladeClashDirection_.x, -bladeClashDirection_.z);
        const float side = bladeClashDirection_.x >= 0.0f ? 1.0f : -1.0f;
        enemy_.SetCinematicTransform(
            enemyPos, enemyYaw, -Clash::kGuardBreakPose * collapseEase,
            side * (0.16f * snap + 0.18f * collapseEase));
        return;
    }
    UpdateBladeClashWinActionPose(bladeClashWinActionTimer, finishYaw);
}

void GameScene::UpdateBladeClashWinActionPose(
    float bladeClashWinActionTimer, float finishYaw) {
        const float windupT =
            std::clamp(bladeClashWinActionTimer / 0.05f, 0.0f, 1.0f);
        const float cutT =
            std::clamp((bladeClashWinActionTimer - 0.01f) / 0.08f, 0.0f, 1.0f);
        const float slideT =
            std::clamp((bladeClashWinActionTimer - 0.06f) / 0.14f, 0.0f, 1.0f);
        const float settleT =
            std::clamp((bladeClashWinActionTimer - 0.22f) / 0.28f, 0.0f, 1.0f);
        const float windupEase = windupT * windupT * (3.0f - 2.0f * windupT);
        const float cutEase = 1.0f - std::pow(1.0f - cutT, 4.0f);
        const float slideEase = 1.0f - std::pow(1.0f - slideT, 2.0f);
        const float settleEase = settleT * settleT * (3.0f - 2.0f * settleT);
        const float dashEase =
            std::clamp(0.72f * cutEase + 0.38f * slideEase, 0.0f, 1.0f);
        XMFLOAT3 dashPos = Lerp(bladeClashFinishPlayerStart_,
                                bladeClashFinishPlayerEnd_, dashEase);
        const float anticipation =
            std::sinf(windupEase * kPi) * (1.0f - cutEase);
        const float lift = std::sinf(cutT * kPi) * 0.10f * (1.0f - settleEase);
        dashPos.x -= bladeClashDirection_.x * 0.18f * anticipation;
        dashPos.y += lift;
        dashPos.z -= bladeClashDirection_.z * 0.18f * anticipation;
        player_.SetCinematicBladeClashPose(dashPos, finishYaw, 1.0f);

        const float enemyHitT =
            std::clamp((bladeClashWinActionTimer - 0.08f) / 0.14f, 0.0f, 1.0f);
        const float enemyBreakT =
            std::clamp((bladeClashWinActionTimer - 0.18f) / 0.30f, 0.0f, 1.0f);
        const float enemySlamT =
            std::clamp((bladeClashWinActionTimer - 0.42f) / 0.38f, 0.0f, 1.0f);
        const float enemySettleT =
            std::clamp((bladeClashWinActionTimer - 0.72f) / 0.40f, 0.0f, 1.0f);
        const float enemyHitEase = 1.0f - std::pow(1.0f - enemyHitT, 5.0f);
        const float enemyBreakEase =
            enemyBreakT * enemyBreakT * (3.0f - 2.0f * enemyBreakT);
        const float enemySlamEase = 1.0f - std::pow(1.0f - enemySlamT, 4.0f);
        const float enemySettleEase =
            enemySettleT * enemySettleT * (3.0f - 2.0f * enemySettleT);
        const float hitPop = std::sinf(enemyHitT * kPi);
        const float breakArc = std::sinf(enemyBreakT * kPi);
        const float slamArc = std::sinf(enemySlamT * kPi);
        const float recoil = 0.18f * Clash::kGuardBreakRecoilDistance +
                             0.20f * enemyHitEase + 0.22f * enemyBreakEase -
                             0.10f * enemySettleEase;
        const float side = bladeClashDirection_.x >= 0.0f ? 1.0f : -1.0f;
        const XMFLOAT3 right = {bladeClashDirection_.z, 0.0f,
                                -bladeClashDirection_.x};
        const float sideDrift = side * (0.14f * hitPop + 0.22f * enemySlamEase -
                                        0.16f * enemySettleEase);
        XMFLOAT3 enemyPos = {
            bladeClashFinishEnemyStart_.x + bladeClashDirection_.x * recoil +
                right.x * sideDrift,
            bladeClashFinishEnemyStart_.y +
                Clash::kGuardBreakLift * (1.0f - 0.58f * enemySlamEase) -
                Clash::kGuardBreakDrop + hitPop * 0.18f + breakArc * 0.24f +
                slamArc * 0.10f - 0.34f * enemySettleEase,
            bladeClashFinishEnemyStart_.z + bladeClashDirection_.z * recoil +
                right.z * sideDrift};
        const float enemyYaw =
            std::atan2f(-bladeClashDirection_.x, -bladeClashDirection_.z);
        enemy_.SetCinematicTransform(
            enemyPos,
            enemyYaw + side * (0.22f * hitPop + 0.42f * enemySlamEase -
                               0.10f * enemySettleEase),
            -0.18f * Clash::kGuardBreakPose - 0.32f * hitPop -
                1.30f * enemySlamEase + 0.06f * enemySettleEase,
            side * (0.18f * hitPop + 0.48f * enemySlamEase -
                    0.08f * enemySettleEase));
}

void GameScene::UpdateBladeClashLossFinish() {
    if (!bladeClashFinishSkidEmitted_ &&
        bladeClashFinishTimer_ >= Clash::kLossHitTime) {
        bladeClashFinishSkidEmitted_ = true;
        bladeClashFinishImpactEmitted_ = true;
        XMFLOAT3 sweepCenter = player_.GetTransform().position;
        sweepCenter.y += 1.02f;
        EmitParticleBurst(
            explosionParticles_, sweepCenter, 92, 1.24f,
            AppParticleBurstStyle::SlashLine, {1.0f, 0.28f, 0.06f, 0.96f},
            {bladeClashDirection_.z, -0.04f, -bladeClashDirection_.x}, 1.92f);
        EmitParticleBurst(
            swordFlashParticles_, sweepCenter, 14, 0.92f,
            AppParticleBurstStyle::Flash, {1.0f, 0.56f, 0.16f, 0.82f},
            {-bladeClashDirection_.x, 0.0f, -bladeClashDirection_.z}, 0.44f);
        EmitParticleBurst(
            sparkParticles_, sweepCenter, 62, 0.44f,
            AppParticleBurstStyle::Sparks, {1.0f, 0.48f, 0.12f, 0.82f},
            {-bladeClashDirection_.x, 0.08f, -bladeClashDirection_.z}, 1.45f);

        CombatFeedbackEvent lossFeedback{};
        lossFeedback.type = CombatFeedbackEventType::PlayerDamaged;
        lossFeedback.position = sweepCenter;
        lossFeedback.direction = {-bladeClashDirection_.x, 0.0f,
                                  -bladeClashDirection_.z};
        lossFeedback.power = 12.0f;
        DispatchCombatFeedback(lossFeedback);
        playerHitCooldown_ = 0.82f;
    }
    if (bladeClashFinishSkidEmitted_ && bladeClashFinishTimer_ >= 1.24f &&
        bladeClashFinishTimer_ < 1.28f) {
        XMFLOAT3 skidCenter = player_.GetTransform().position;
        skidCenter.y += 0.28f;
        EmitParticleBurst(
            explosionParticles_, skidCenter, 36, 0.56f,
            AppParticleBurstStyle::SlashLine, {1.0f, 0.42f, 0.10f, 0.38f},
            {bladeClashDirection_.z, 0.02f, -bladeClashDirection_.x}, 0.92f);
        EmitParticleBurst(
            smokeParticles_, skidCenter, 38, 0.64f,
            AppParticleBurstStyle::Smoke, {0.34f, 0.28f, 0.24f, 0.44f},
            {-bladeClashDirection_.x, 0.03f, -bladeClashDirection_.z}, 0.62f);
    }
    if (bladeClashFinishSkidEmitted_ && !bladeClashFinishWallImpactEmitted_ &&
        bladeClashFinishTimer_ >= Clash::kLossWallImpactTime) {
        bladeClashFinishWallImpactEmitted_ = true;
        XMFLOAT3 crashCenter = player_.GetTransform().position;
        crashCenter.y += 0.86f;
        EmitParticleBurst(
            explosionParticles_, crashCenter, 110, 0.96f,
            AppParticleBurstStyle::Explosion, {1.0f, 0.36f, 0.10f, 0.78f},
            {-bladeClashDirection_.x, 0.12f, -bladeClashDirection_.z}, 2.35f);
        EmitParticleBurst(
            sparkParticles_, crashCenter, 72, 0.58f,
            AppParticleBurstStyle::Sparks, {1.0f, 0.56f, 0.16f, 0.86f},
            {bladeClashDirection_.z, 0.10f, -bladeClashDirection_.x}, 1.42f);
        EmitParticleBurst(
            smokeParticles_, crashCenter, 64, 0.90f,
            AppParticleBurstStyle::Smoke, {0.36f, 0.30f, 0.26f, 0.56f},
            {-bladeClashDirection_.x, 0.04f, -bladeClashDirection_.z}, 0.92f);

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
            0.20f + 0.58f * lossPose.recoilEase + 0.32f * lossPose.slideEase,
            0.0f, 1.0f));
    } else {
        player_.SetDefeatPoseRatio(0.0f);
        player_.SetBladeClashPose(true, 0.0f);
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
        ctx_, postEffectCinematicLayer_,
        bladeClashFinishPlayerWon_ ? 0.018f * hold : 0.020f * hold,
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
    ClearBattlePostProcess(ctx_, postEffectCinematicLayer_);
    if (shouldResolveEnemyTransition) {
        enemy_.ResolveDeferredDamageTransitions();
        enemyHitCooldown_ = 0.22f;
    }
}

void GameScene::BeginVictorySequence() {
    battleResultRequested_ = true;
    victoryClearTime_ = battleElapsedTime_;
    StopBattleBgm();
    ClearGameplayParticles();

    victorySequenceActive_ = true;
    victorySequenceTimer_ = 0.0f;
    victoryFinalExplosionEmitted_ = false;
    victoryEnemyStartPos_ = enemy_.GetTransform().position;
    counterCinematicActive_ = false;
    SetEnemyAnimationFrozen(false);
    if (ctx_ != nullptr) {
        hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(), enemy_.GetMaxHP());
    }

    if (!titleDemoMode_) {
        ApplyBattlePostProcess(ctx_, postEffectCinematicLayer_, 0.055f, 0.58f,
                               0.10f, 0.48f, 24);
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
    StopBattleBgm();
    ClearGameplayParticles();
    defeatSequenceActive_ = true;
    defeatSequenceTimer_ = 0.0f;
    defeatImpactEmitted_ = false;
    counterCinematicActive_ = false;
    SetEnemyAnimationFrozen(false);
    player_.SetDefeatPoseRatio(0.0f);
    if (ctx_ != nullptr) {
        hud_.Update(*ctx_, player_.GetHP(), enemy_.GetHP(), enemy_.GetMaxHP());
    }

    if (!titleDemoMode_) {
        ApplyBattlePostProcess(ctx_, postEffectCinematicLayer_, 0.040f, 0.62f,
                               0.18f, 0.54f, 22);
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
        ApplyBattlePostProcess(ctx_, postEffectCinematicLayer_,
                               0.040f * (1.0f - postProcessRatio), 0.62f,
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
        ClearBattlePostProcess(ctx_, postEffectCinematicLayer_);
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
        sceneManager_->ChangeScene(std::make_unique<GameOverScene>(
            battleElapsedTime_, inputCalibration_, combatDifficulty_));
    }
}

void GameScene::UpdateVictorySequence(float deltaTime) {
    victorySequenceTimer_ += deltaTime;
    const float ratio = std::clamp(
        victorySequenceTimer_ / victorySequenceDuration_, 0.0f, 1.0f);

    const float stepped = std::floor(ratio * 14.0f) / 14.0f;
    const float blur = (1.0f - stepped) * 0.070f;
    if (!titleDemoMode_) {
        ApplyBattlePostProcess(ctx_, postEffectCinematicLayer_, blur, 0.58f,
                               0.10f + stepped * 0.18f, 0.48f, 24);
    }

    if (!victoryFinalExplosionEmitted_ &&
        victorySequenceTimer_ >= kVictoryEnemyVanishTime) {
        EmitVictoryEnemyVanishExplosion();
    }

    if (victorySequenceTimer_ >= victorySequenceDuration_) {
        victorySequenceActive_ = false;
        ClearBattlePostProcess(ctx_, postEffectCinematicLayer_);
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
        sceneManager_->ChangeScene(std::make_unique<GameVictoryScene>(
            victoryClearTime_, inputCalibration_, combatDifficulty_));
    }
}

void GameScene::EmitVictoryEnemyVanishExplosion() {
    victoryFinalExplosionEmitted_ = true;

    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    victoryFinalExplosionCenter_ = {enemyPos.x, enemyPos.y + 0.72f, enemyPos.z};
    const float difficultyT = GetDifficultyRatio();

    if (soundsLoaded_ && ctx_ != nullptr && ctx_->systems.sound != nullptr) {
        const uint32_t soundId =
            victoryExplosionSoundId_ != SoundManager::kInvalidSoundId
                ? victoryExplosionSoundId_
                : explosionSoundId_;
        ctx_->systems.sound->Play(soundId, kExplosionSoundVolume *
                                               (0.92f + 0.28f * difficultyT) *
                                               AppSceneServices::GetSeVolume());
    }
}

void GameScene::DrawVictoryEnemyVanishExplosionBillboards() {
    if (!victorySequenceActive_ || !victoryFinalExplosionEmitted_ ||
        ctx_ == nullptr || ctx_->rendering.model == nullptr) {
        return;
    }

    const float age =
        (std::max)(0.0f, victorySequenceTimer_ - kVictoryEnemyVanishTime);
    if (age > kVictoryEnemyExplosionBillboardFadeStart +
                  kVictoryEnemyExplosionBillboardFadeDuration) {
        return;
    }

    ModelManager *model = ctx_->rendering.model;
    const float difficultyT = GetDifficultyRatio();
    const float grow = SmoothStep01(age / kVictoryEnemyExplosionBillboardRise);
    const float fade =
        1.0f - SmoothStep01((age - kVictoryEnemyExplosionBillboardFadeStart) /
                            kVictoryEnemyExplosionBillboardFadeDuration);
    const float alpha = grow * fade * (1.10f + 0.24f * difficultyT);
    if (alpha <= 0.001f) {
        return;
    }

    const XMFLOAT3 center = victoryFinalExplosionCenter_;
    const XMFLOAT3 cameraPos = camera_.GetPosition();
    const float yaw = BillboardYawToCamera(center, cameraPos);
    const uint32_t smokeModelId = PickValue(
        victoryDarkSmokeBillboardModelId_ != 0,
        victoryDarkSmokeBillboardModelId_, victorySmokeBillboardModelId_);
    const VictoryBillboardPatch patches[] = {
        {victoryFireBillboardModelId_,
         {0.00f, 0.00f, -0.03f},
         {2.25f, 1.18f},
         0.04f,
         0.00f,
         true},
        {victoryFireBillboardModelId_,
         {-0.42f, 0.08f, -0.02f},
         {1.30f, 0.54f},
         -0.28f,
         0.05f,
         true},
        {victoryFireBillboardModelId_,
         {0.42f, 0.12f, -0.02f},
         {1.32f, 0.56f},
         0.30f,
         0.10f,
         true},
        {smokeModelId,
         {0.00f, 0.08f, -0.05f},
         {2.52f, 1.18f},
         0.08f,
         0.08f,
         false},
        {smokeModelId,
         {-0.55f, 0.18f, -0.04f},
         {1.70f, 0.82f},
         -0.26f,
         0.15f,
         false},
        {smokeModelId,
         {0.58f, 0.24f, -0.04f},
         {1.62f, 0.80f},
         0.32f,
         0.22f,
         false},
        {smokeModelId,
         {0.04f, 0.62f, -0.06f},
         {1.38f, 1.02f},
         -0.12f,
         0.31f,
         false},
        {smokeModelId,
         {-0.22f, -0.22f, -0.03f},
         {1.68f, 0.58f},
         0.22f,
         0.38f,
         false},
        {smokeModelId,
         {0.30f, -0.12f, -0.03f},
         {1.46f, 0.56f},
         -0.36f,
         0.48f,
         false},
    };

    for (const VictoryBillboardPatch &patch : patches) {
        DrawVictoryBillboardPatch(patch, center, yaw, age, alpha);
    }

    model->ClearDrawEffect();
}

void GameScene::DrawVictoryBillboardPatch(const VictoryBillboardPatch &patch,
                                          const XMFLOAT3 &center, float yaw,
                                          float age, float alpha) {
    if (patch.modelId == 0) {
        return;
    }
    const float local = SmoothStep01(
        (age - patch.delay) / kVictoryEnemyExplosionBillboardRise);
    const float fireFade =
        PickValue(patch.fire, 1.0f - SmoothStep01(age / 0.48f), 1.0f);
    if (local <= 0.001f || (patch.fire && fireFade <= 0.001f)) {
        return;
    }
    const float difficultyT = GetDifficultyRatio();
    const XMFLOAT4 heat = DifficultyGaugeHeatColor(combatDifficulty_, 1.0f);
    const XMFLOAT4 fireColor{std::clamp(heat.x + 0.30f, 0.0f, 1.0f),
                             std::clamp(heat.y + 0.24f, 0.0f, 1.0f),
                             std::clamp(heat.z + 0.12f, 0.0f, 1.0f), 1.0f};
    const XMFLOAT4 smokeColor =
        Lerp({0.20f, 0.18f, 0.16f, 1.0f}, heat, 0.22f + 0.18f * difficultyT);
    ModelDrawEffect effect{};
    effect.enabled = true;
    effect.additiveBlend = patch.fire;
    effect.disableCulling = true;
    effect.blendOverride = ModelDrawEffectBlendOverride::Alpha;
    effect.color = PickValue(patch.fire, fireColor, smokeColor);
    effect.color.w = alpha * local * PickValue(patch.fire, fireFade, 0.92f);
    effect.intensity = PickValue(patch.fire, 1.18f + 0.40f * difficultyT,
                                 0.18f + 0.16f * difficultyT);
    effect.fresnelPower = PickValue(patch.fire, 1.35f, 0.78f);
    effect.noiseAmount = PickValue(patch.fire, 0.42f, 0.96f);
    effect.alphaBoost = PickValue(patch.fire, 2.45f, 2.05f);
    effect.surfaceTint = PickValue(patch.fire, 0.72f, 0.48f);
    effect.time = sceneLightTime_ * PickValue(patch.fire, 1.10f, 0.36f) +
                  patch.delay * 9.0f;
    ctx_->rendering.model->SetDrawEffect(effect);

    const float drift = SmoothStep01(age / 1.45f);
    const float size = PickValue(patch.fire, 1.20f, 1.56f) *
                       (1.0f + 1.22f * drift) *
                       (1.0f + 0.34f * difficultyT);
    Transform billboard{};
    billboard.position = {
        center.x + patch.offset.x * (1.0f + 0.52f * drift),
        center.y + patch.offset.y * (1.0f + 0.46f * drift) +
            PickValue(patch.fire, 0.0f, 0.22f * drift),
        center.z + patch.offset.z};
    billboard.rotation = MakeQuat(0.0f, yaw, patch.roll);
    const float pulse = 1.0f + 0.04f * std::sinf(sceneLightTime_ * 3.2f +
                                                 patch.delay * 17.0f);
    billboard.scale = {patch.scale.x * size * pulse,
                       patch.scale.y * size * pulse, 1.0f};
    ctx_->rendering.model->Draw(patch.modelId, billboard, camera_);
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
    const float preExplosionFlash =
        flashPulse(kVictoryEnemyVanishTime - 0.32f, 0.42f, 1.0f);
    const float blink = (std::max)(introPulse * introPulse,
                                   (std::max)(fallFlash, preExplosionFlash));
    const float finalFlash =
        victoryFinalExplosionEmitted_
            ? (std::max)(0.0f, 1.0f - (victorySequenceTimer_ -
                                       kVictoryEnemyVanishTime) /
                                          0.84f)
            : 0.0f;
    const float alpha = std::clamp(
        (std::max)(earlyFlash, (std::max)(blink, finalFlash * 1.12f)), 0.0f,
        1.0f);
    if (alpha <= 0.01f) {
        return;
    }

    Sprite flash{};
    flash.textureId = 0;
    flash.position = {0.0f, 0.0f};
    flash.size = {static_cast<float>(ctx_->systems.winApp->GetWidth()),
                  static_cast<float>(ctx_->systems.winApp->GetHeight())};
    flash.color = DifficultyGaugeHeatColor(combatDifficulty_, alpha);
    flash.color.x = std::clamp(flash.color.x + 0.18f, 0.0f, 1.0f);
    flash.color.y = std::clamp(flash.color.y + 0.18f, 0.0f, 1.0f);
    flash.color.z = std::clamp(flash.color.z + 0.18f, 0.0f, 1.0f);

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
    const float black = GetDefeatFadeToBlackRatio();
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

float GameScene::GetDefeatFadeToBlackRatio() const {
    constexpr float kFadeToBlackStart = 1.55f;
    constexpr float kFadeToBlackDuration = 0.90f;
    return SmoothStep01((defeatSequenceTimer_ - kFadeToBlackStart) /
                        kFadeToBlackDuration);
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

void GameScene::DrawDistantHazardBackdrop(float buildProgress) {
    BattleArenaModelIds ids{};
    ids.arenaCityTowerModelId = arenaCityTowerModelId_;
    ids.arenaCityWindowModelId = arenaCityWindowModelId_;
    DrawBattleArena(ctx_->rendering.model, camera_, ids, sceneLightTime_, 0.0f,
                    0.0f, 0.0f, buildProgress, false, true);
}
void GameScene::DrawArena() {
    const float floorBuild = BackgroundBuildProgress(0.00f, 0.20f);
    const float tileBuild = BackgroundBuildProgress(0.10f, 0.30f);
    const float lineBuild = BackgroundBuildProgress(0.18f, 0.28f);
    const float distantBuild = BackgroundBuildProgress(0.24f, 0.38f);

    BattleArenaModelIds ids{};
    ids.arenaNoiseTextureId = arenaNoiseTextureId_;
    ids.arenaFloorModelId = arenaFloorModelId_;
    ids.arenaLowPolyTerrainModelId = arenaLowPolyTerrainModelId_;
    ids.arenaDistantTerrainModelId = arenaDistantTerrainModelId_;
    ids.arenaHazardSpireModelId = arenaHazardSpireModelId_;
    ids.arenaHazardGlowRingModelId = arenaHazardGlowRingModelId_;
    ids.arenaCityTowerModelId = arenaCityTowerModelId_;
    ids.arenaCityWindowModelId = arenaCityWindowModelId_;
    ids.arenaGiantBodyModelId = arenaGiantBodyModelId_;
    ids.arenaGiantHeadModelId = arenaGiantHeadModelId_;
    ids.arenaCenterDiskModelId = arenaCenterDiskModelId_;
    ids.arenaSpokeModelId = arenaSpokeModelId_;
    ids.arenaTutorialSpokeModelId = arenaTutorialSpokeModelId_;
    ids.arenaInnerRingModelId = arenaInnerRingModelId_;
    ids.arenaOuterRingModelId = arenaOuterRingModelId_;
    ids.arenaColumnModelId = arenaColumnModelId_;
    ids.arenaColumnCapModelId = arenaColumnCapModelId_;
    ids.arenaDomeModelId = arenaDomeModelId_;
    ids.arenaBarrierRingModelId = arenaBarrierRingModelId_;
    ids.chargeWeakPointModelId = chargeWeakPointModelId_;
    DrawBattleArena(ctx_->rendering.model, camera_, ids, sceneLightTime_,
                    floorBuild, tileBuild, lineBuild, distantBuild,
                    tutorialBackgroundMode_, backgroundOnlyMode_);
}
