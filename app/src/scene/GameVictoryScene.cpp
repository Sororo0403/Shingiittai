#include "GameVictoryScene.h"
#include "AppSceneServices.h"
#include "AssetManager.h"
#include "DirectXCommon.h"
#include "GameScene.h"
#include "Input.h"
#include "Material.h"
#include "ModelManager.h"
#include "PostEffectManager.h"
#include "SceneManager.h"
#include "SoundManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "CreditScene.h"
#include "TitleScene.h"
#include "WinApp.h"
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

using namespace DirectX;

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kImpactTime = 2.20f;  // Extended for slower animation
constexpr float kExplosionFreezeTime = 4.80f;  // Extended
constexpr float kDuration = kExplosionFreezeTime;
constexpr float kPierceAnimDuration = 1.10f;  // Much longer pierce animation
constexpr float kPlayerRevealDelay = 0.30f;
constexpr float kExplosionSpeedBoost = 1.25f;
constexpr float kExplosionSizeBoost = 1.65f;
constexpr float kExplosionFlashDuration = 0.06f;
constexpr float kExplosionCoreDuration = 0.10f;
constexpr float kExplosionBillboardStart = 0.05f;
constexpr float kExplosionBillboardRise = 0.32f;
constexpr float kExplosionBillboardFadeStart = 1.05f;
constexpr float kExplosionBillboardFadeDuration = 0.70f;
constexpr float kResultBlurDelay = 0.0f;
constexpr float kResultBlurDuration = 0.22f;
constexpr float kResultTextDelay = 0.0f;
constexpr float kResultExitFadeDuration = 0.35f;
constexpr size_t kRankingDrawCount = 5;
constexpr float kPreImpactStartTime = 1.20f;
constexpr float kPreImpactBurstTime = 1.65f;
constexpr float kSlashSound1Time = 1.80f;
constexpr float kSlashSound2Time = 1.90f;
constexpr float kSlashSound3Time = 2.00f;
constexpr float kConfettiGravity = 58.0f;
constexpr float kConfettiWind = 34.0f;
constexpr float kConfettiDriftDelay = 0.22f;
constexpr XMFLOAT3 kPlayerPierceSpawnOffset{0.0f, 0.12f, 0.12f};
constexpr XMFLOAT3 kPlayerAfterPos{0.0f, 0.0f, -1.32f};
constexpr XMFLOAT3 kEnemyStartPos{0.0f, 0.0f, 0.0f};
constexpr XMFLOAT3 kEnemyFallenPoseBasePos{0.0f, 0.30f, 16.00f};
constexpr XMFLOAT3 kEnemyFallenPoseReferencePos{0.0f, 0.0f, 10.0f};
constexpr float kEnemyFallenPoseRatio = 1.0f;
constexpr float kPlayerModelScaleMultiplier = 1.45f;
constexpr float kVictoryPlayerVisualScale = 0.68f;
constexpr float kVictoryEnemyVisualScale = 1.86f;
constexpr float kExplosionShakeDuration = 1.55f;

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

std::filesystem::path RankingPath() {
    return AssetManager::GetAssetRoot() / L"save" / L"ranking.tsv";
}

const char *RankingModeToken(InputControlType controlType) {
    return controlType == InputControlType::Hand ? "hand" : "kbm";
}

InputControlType RankingModeFromToken(const std::string &token) {
    return token == "hand" ? InputControlType::Hand
                           : InputControlType::KeyboardMouse;
}

XMFLOAT4 LerpColor(const XMFLOAT4 &a, const XMFLOAT4 &b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

float DifficultyRatio(float difficulty) {
    return std::clamp(difficulty, 0.0f, 9.0f) / 9.0f;
}

float Hash01(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return static_cast<float>(value & 0x00ffffffu) /
           static_cast<float>(0x01000000u);
}

XMFLOAT4 GaugeHeatColor(float t, float alpha) {
    t = std::clamp(t, 0.0f, 1.0f);
    const XMFLOAT4 blue = Color(0.015f, 0.075f, 0.50f, alpha);
    const XMFLOAT4 yellow = Color(1.0f, 0.78f, 0.10f, alpha);
    const XMFLOAT4 red = Color(0.62f, 0.018f, 0.010f, alpha);
    if (t < 0.62f) {
        return LerpColor(blue, yellow, t / 0.62f);
    }
    return LerpColor(yellow, red, (t - 0.62f) / 0.38f);
}

XMFLOAT4 HeatColorForDifficulty(float difficulty, float alpha) {
    return GaugeHeatColor(DifficultyRatio(difficulty), alpha);
}

XMFLOAT4 BrightHeatColorForDifficulty(float difficulty, float alpha,
                                      float boost = 0.22f) {
    const XMFLOAT4 heat = HeatColorForDifficulty(difficulty, alpha);
    return {std::clamp(heat.x + boost, 0.0f, 1.0f),
            std::clamp(heat.y + boost, 0.0f, 1.0f),
            std::clamp(heat.z + boost, 0.0f, 1.0f), alpha};
}

XMFLOAT4 ReadableResultTextColor(float difficulty, float alpha) {
    XMFLOAT4 color =
        LerpColor(Color(1.0f, 1.0f, 1.0f, alpha),
                  HeatColorForDifficulty(difficulty, alpha), 0.18f);
    color.w = alpha;
    return color;
}

XMFLOAT4 SmokeHeatColorForDifficulty(float difficulty, float alpha) {
    const XMFLOAT4 heat = HeatColorForDifficulty(difficulty, alpha);
    return {0.070f + heat.x * 0.62f, 0.066f + heat.y * 0.54f,
            0.072f + heat.z * 0.66f, alpha};
}

XMFLOAT4 DarkSmokeHeatColorForDifficulty(float difficulty, float alpha) {
    const XMFLOAT4 heat = HeatColorForDifficulty(difficulty, alpha);
    return {0.014f + heat.x * 0.24f, 0.013f + heat.y * 0.20f,
            0.016f + heat.z * 0.28f, alpha};
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

float PierceRawT(float sceneTime) {
    return std::clamp((sceneTime - kPlayerRevealDelay) / kPierceAnimDuration,
                      0.0f, 1.0f);
}

float PierceT(float sceneTime) { return EaseOutCubic(PierceRawT(sceneTime)); }

bool IsPlayerRevealed(float sceneTime) { return sceneTime >= kPlayerRevealDelay; }

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

Material MakeOpaqueColorMaterial(const XMFLOAT4 &color) {
    Material material{};
    material.color = {color.x, color.y, color.z, 1.0f};
    material.enableTexture = 0;
    material.blendMode = static_cast<int32_t>(BlendMode::Opaque);
    material.cullMode = static_cast<int32_t>(MaterialCullMode::Back);
    material.depthWrite = 1;
    material.reflectionStrength = 0.06f;
    material.reflectionFresnelStrength = 0.18f;
    material.roughness = 0.72f;
    XMStoreFloat4x4(&material.uvTransform,
                    XMMatrixTranspose(XMMatrixIdentity()));
    return material;
}

PostProcessProfile MakeVictoryPostProcessProfile(float stylizeStrength,
                                                 bool enableToon) {
    PostProcessProfile profile{};
    profile.filter.mode = stylizeStrength > 0.12f
                              ? PostProcessFilterMode::GaussianBlur7x7
                              : PostProcessFilterMode::None;
    const float punch = std::clamp(stylizeStrength / 0.18f, 0.0f, 1.0f);
    profile.vignette.enabled = true;
    profile.vignette.strength = 0.68f + 0.22f * punch;
    profile.vignette.radius = 0.56f - 0.10f * punch;
    profile.vignette.scale = 18.0f;
    profile.radialBlur.center[0] = 0.5f;
    profile.radialBlur.center[1] = 0.50f;
    profile.radialBlur.sampleCount = stylizeStrength > 0.001f ? 12 : 1;
    profile.radialBlur.strength = stylizeStrength * 0.72f;
    profile.sceneDim.strength = 0.20f + 0.12f * punch;
    profile.bloom.enabled = true;
    profile.bloom.intensity = 0.48f + 0.82f * punch;
    profile.bloom.threshold = 0.34f - 0.12f * punch;
    profile.toon.enabled = enableToon;
    profile.toon.strength = enableToon ? (0.20f + 0.35f * punch) : 0.0f;
    profile.toon.colorSteps = 5.0f;
    profile.toon.edgeStrength = enableToon ? (0.18f + 0.22f * punch) : 0.0f;
    return profile;
}

} // namespace

GameVictoryScene::GameVictoryScene(
    float clearTime, const SwordInputCalibration &inputCalibration,
    float combatDifficulty)
    : inputCalibration_(inputCalibration),
      combatDifficulty_(std::clamp(combatDifficulty, 0.0f, 9.0f)),
      clearTime_((std::max)(0.0f, clearTime)) {}

GameVictoryScene::~GameVictoryScene() { StopResultCrowdAudio(); }

void GameVictoryScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    realSceneTime_ = 0.0f;
    resultTimer_ = 0.0f;
    exitTimer_ = 0.0f;
    currentScore_ = ComputeScore(clearTime_, combatDifficulty_);
    preImpactEmitted_ = false;
    impactEmitted_ = false;
    resultMode_ = false;
    rankingVisible_ = false;
    rankingRegistered_ = false;
    exitRequested_ = false;
    exitTargetIndex_ = 1;
    currentRank_ = -1;
    rankingDisplayFirstRank_ = 1;
    actionButtonIndex_ = 1;
    explosionSoundId_ = SoundManager::kInvalidSoundId;
    slashSoundId_ = SoundManager::kInvalidSoundId;
    slashSound1VoiceHandle_ = SoundManager::kInvalidVoiceHandle;
    slashSound2VoiceHandle_ = SoundManager::kInvalidVoiceHandle;
    slashSound3VoiceHandle_ = SoundManager::kInvalidVoiceHandle;
    slashSound1Played_ = false;
    slashSound2Played_ = false;
    slashSound3Played_ = false;
    resultCrowdIntroSoundId_ = SoundManager::kInvalidSoundId;
    resultCrowdIntroVoiceHandle_ = SoundManager::kInvalidVoiceHandle;

    const float aspect = static_cast<float>(ctx_->systems.winApp->GetWidth()) /
                         static_cast<float>(ctx_->systems.winApp->GetHeight());
    camera_.Initialize(aspect);
    camera_.SetClipRange(0.05f, 90.0f);

    if (ctx_->rendering.postEffectManager != nullptr) {
        ctx_->rendering.postEffectManager->SetBaseProfile(
            MakeVictoryPostProcessProfile(0.0f, false));
    }
    if (ctx_->rendering.dxCommon != nullptr) {
        ctx_->rendering.dxCommon->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }

    ModelManager *model = ctx_->rendering.model;
    arenaModels_ = EnsureBattleArenaModels(model, ctx_->rendering.texture);
    playerModelId_ = model->Load(L"app/resources/models/player/player.gltf");
    swordModelId_ = model->Load(L"app/resources/models/player/sword.glb");
    enemyModelId_ = model->Load(L"app/resources/models/boss/boss.gltf");
    ApplyEnemyPhaseMaterial(model, ctx_->rendering.texture, enemyModelId_,
                            enemyPhaseMaterials_, BossPhase::Phase3, false,
                            1.0f);

    player_.Initialize(playerModelId_, swordModelId_);
    player_.SetInputCalibration(inputCalibration_);
    player_.SetCinematicBladeClashPose(kPlayerAfterPos, kPi, 1.0f, true);

    explosionEnemyTransform_ = Transform{};

    enemy_.SetDifficulty(combatDifficulty_);
    enemy_.Initialize(enemyModelId_);
    enemy_.SetBossPhaseForPresentation(BossPhase::Phase3);
    enemy_.ApplyVictoryDefeatPose(kEnemyFallenPoseRatio,
                                  kEnemyFallenPoseBasePos,
                                  kEnemyFallenPoseReferencePos);

    if (ctx_->systems.sound != nullptr) {
        ctx_->systems.sound->TryLoad(
            L"app/resources/audio/se/combat/explosion_4.mp3",
                                     explosionSoundId_);
        ctx_->systems.sound->TryLoad(
            L"app/resources/audio/se/combat/se_Slash.wav",
                                     slashSoundId_);
        ctx_->systems.sound->TryLoad(
            L"app/resources/audio/se/victory/crowd_cheer_applause_1.mp3",
            resultCrowdIntroSoundId_);
    }

    slashTextureId_ = CreateSlashTexture(ctx_->rendering.texture);
    slashModelId_ = model->CreatePlane(
        slashTextureId_, MakeTransparentMaterial(slashTextureId_,
                                                 {2.7f, 2.25f, 0.70f, 0.95f}));
    explosionCoreModelId_ = model->CreateSphere(
        0, MakeOpaqueColorMaterial(HeatColorForDifficulty(combatDifficulty_, 1.0f)),
        20, 10, 1.0f);
    explosionFlashModelId_ = model->CreatePlane(
        0, MakeTransparentMaterial(0, {1.0f, 0.94f, 0.62f, 0.88f}));

    particleTextureId_ =
        ctx_->rendering.texture->Load(L"app/resources/effects/particles/smoke.png");
    fireBillboardModelId_ = 0;
    smokeBillboardModelId_ = 0;
    darkSmokeBillboardModelId_ = 0;
    if (particleTextureId_ != 0) {
        const XMFLOAT4 fireMaterialColor =
            BrightHeatColorForDifficulty(combatDifficulty_, 0.82f, 0.12f);
        const XMFLOAT4 smokeMaterialColor =
            SmokeHeatColorForDifficulty(combatDifficulty_, 0.86f);
        const XMFLOAT4 darkSmokeMaterialColor =
            DarkSmokeHeatColorForDifficulty(combatDifficulty_, 0.94f);
        fireBillboardModelId_ = model->CreatePlane(
            particleTextureId_,
            MakeTransparentMaterial(particleTextureId_, fireMaterialColor));
        smokeBillboardModelId_ = model->CreatePlane(
            particleTextureId_,
            MakeTransparentMaterial(particleTextureId_, smokeMaterialColor));
        darkSmokeBillboardModelId_ = model->CreatePlane(
            particleTextureId_,
            MakeTransparentMaterial(particleTextureId_,
                                    darkSmokeMaterialColor));
    }

    missionCompleteLabel_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/mission_complete.png");
    clearTimeLabel_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/clear_time.png");
    scoreTitleLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/score_title.png");
    difficultyLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_difficulty.png");
    rankingTitleLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_title.png");
    rankingRankHeaderLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_rank.png");
    rankingScoreHeaderLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_score.png");
    rankingTimeHeaderLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_time.png");
    rankingDifficultyHeaderLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_difficulty.png");
    retryButtonLabel_ = LoadTextureImage(L"app/resources/ui/gameover/retry.png");
    titleButtonLabel_ = LoadTextureImage(L"app/resources/ui/gameover/title.png");
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
    const float digitInkLeft[10] = {14.0f, 16.0f, 15.0f, 16.0f, 12.0f,
                                    16.0f, 14.0f, 16.0f, 14.0f, 14.0f};
    const float digitInkRight[10] = {50.0f, 41.0f, 48.0f, 48.0f, 50.0f,
                                     49.0f, 50.0f, 49.0f, 50.0f, 50.0f};
    for (int i = 0; i < 10; ++i) {
        Image &digit = digitImages_[static_cast<size_t>(i)];
        digit.inkLeft = digitInkLeft[i];
        digit.inkRight = digitInkRight[i];
        digit.inkTop = (i == 1 || i == 3 || i == 4 || i == 5 || i == 7)
                           ? 21.0f
                           : 20.0f;
        digit.inkBottom = 67.0f;
    }
    colonImage_.inkLeft = 19.0f;
    colonImage_.inkRight = 32.0f;
    colonImage_.inkTop = 31.0f;
    colonImage_.inkBottom = 67.0f;
    dotImage_.inkLeft = 17.0f;
    dotImage_.inkRight = 29.0f;
    dotImage_.inkTop = 54.0f;
    dotImage_.inkBottom = 67.0f;
    dashImage_.inkLeft = 16.0f;
    dashImage_.inkRight = 38.0f;
    dashImage_.inkTop = 44.0f;
    dashImage_.inkBottom = 53.0f;
    secondImage_.inkLeft = 15.0f;
    secondImage_.inkRight = 43.0f;
    secondImage_.inkTop = 33.0f;
    secondImage_.inkBottom = 67.0f;

    // Play resonant slash sounds immediately upon scene initialization
    if (ctx_->systems.sound != nullptr &&
        slashSoundId_ != SoundManager::kInvalidSoundId) {
        // First layer - very deep, slow resonance
        slashSound1VoiceHandle_ =
            ctx_->systems.sound->Play(slashSoundId_, 1.0f, false);
        if (slashSound1VoiceHandle_ != SoundManager::kInvalidVoiceHandle) {
            ctx_->systems.sound->SetVoiceFrequencyRatio(
                slashSound1VoiceHandle_, 0.35f);  // Very slow and deep
        }

        // Second layer - medium resonance
        slashSound2VoiceHandle_ =
            ctx_->systems.sound->Play(slashSoundId_, 0.8f, false);
        if (slashSound2VoiceHandle_ != SoundManager::kInvalidVoiceHandle) {
            ctx_->systems.sound->SetVoiceFrequencyRatio(
                slashSound2VoiceHandle_, 0.45f);  // Slower mid-resonance
        }

        // Third layer - brightest resonance
        slashSound3VoiceHandle_ =
            ctx_->systems.sound->Play(slashSoundId_, 0.6f, false);
        if (slashSound3VoiceHandle_ != SoundManager::kInvalidVoiceHandle) {
            ctx_->systems.sound->SetVoiceFrequencyRatio(
                slashSound3VoiceHandle_, 0.55f);  // Still slow but brighter
        }
    }
}

void GameVictoryScene::Update() {
    const float deltaTime = ctx_->frame.deltaTime;
    Input *input = ctx_->systems.input;
    const bool skip =
        input != nullptr &&
        (input->IsKeyTrigger(DIK_SPACE) || input->IsKeyTrigger(DIK_RETURN) ||
         (input->IsGamepadConnected() &&
          input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_A)));
    if (resultMode_) {
        resultTimer_ += deltaTime;
        if (exitRequested_) {
            exitTimer_ += deltaTime;
            if (exitTimer_ >= kResultExitFadeDuration && sceneManager_ != nullptr) {
                StopResultCrowdAudio();
                if (exitTargetIndex_ == 0) {
                    sceneManager_->ChangeScene(std::make_unique<GameScene>(
                        inputCalibration_, combatDifficulty_));
                } else {
                    sceneManager_->ChangeScene(
                        std::make_unique<CreditScene>(
                            CreditScene::ReturnTarget::Title));
                }
                return;
            }
        }
        const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
        const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());
        UpdateVictoryConfetti(deltaTime, w, h);
        UpdateResultPostProcess();
        UpdateResultInput();
        return;
    }

    realSceneTime_ += deltaTime;
    const float explosionAge = sceneTime_ - kImpactTime;
    const float timeScale =
        explosionAge < 0.0f
            ? 0.65f  // Much slower before impact (strongest slow-motion)
            : (explosionAge < 1.0f ? 0.75f : 1.05f);  // Extended slow-motion period
    sceneTime_ += deltaTime * timeScale;
    if (!impactEmitted_ && sceneTime_ > kImpactTime) {
        sceneTime_ = kImpactTime;
    }
    UpdateCinematic(deltaTime);

    if (skip || sceneTime_ >= kDuration) {
        BeginResult();
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

    ctx_->rendering.sprite->PreDraw(true);
    DrawOverlay(w, h);
    if (resultMode_) {
        DrawResultOverlay(w, h);
    }
    ctx_->rendering.sprite->PostDraw();
}

bool GameVictoryScene::UsesForeground3DPass() const {
    return impactEmitted_ &&
           (explosionCoreModelId_ != 0 || fireBillboardModelId_ != 0 ||
            smokeBillboardModelId_ != 0 ||
            darkSmokeBillboardModelId_ != 0);
}

void GameVictoryScene::DrawForeground3D() {
    ModelManager *model = ctx_ != nullptr ? ctx_->rendering.model : nullptr;
    if (model == nullptr) {
        return;
    }
    model->PrepareSkinning({playerModelId_, enemyModelId_});
    model->PreDraw();
    DrawExplosionFlash();
    DrawExplosionCore();
    DrawExplosionBillboards();
    DrawForegroundEnemy();
    model->PostDraw();
}

void GameVictoryScene::DrawTransparent() {
}

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
    (void)deltaTime;

    const float rawPierceT = PierceRawT(sceneTime_);
    const float pierceT = EaseOutCubic(rawPierceT);
    const float thrustPulse =
        std::sinf(rawPierceT * kPi) *
        (0.35f + 0.65f * (1.0f - SmoothStep01(rawPierceT)));
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 playerStartPos{enemyPos.x + kPlayerPierceSpawnOffset.x,
                                  enemyPos.y + kPlayerPierceSpawnOffset.y,
                                  enemyPos.z + kPlayerPierceSpawnOffset.z};
    const XMFLOAT3 playerPos = Lerp3(playerStartPos, kPlayerAfterPos, pierceT);
    const float playerYaw = kPi + 0.08f * thrustPulse;
    const float poseRatio = 0.38f + 0.62f * pierceT;
    if (IsPlayerRevealed(sceneTime_)) {
        player_.SetCinematicBladeClashPose(playerPos, playerYaw, poseRatio, true);
    }

    if (!preImpactEmitted_ && sceneTime_ >= kPreImpactBurstTime) {
        preImpactEmitted_ = true;
        EmitPreImpactBurst();
    }

    if (!impactEmitted_ && sceneTime_ >= kImpactTime) {
        impactEmitted_ = true;
        explosionEnemyTransform_ = enemy_.GetTransform();
        if (ctx_ != nullptr && ctx_->systems.sound != nullptr &&
            explosionSoundId_ != SoundManager::kInvalidSoundId) {
            ctx_->systems.sound->Play(explosionSoundId_, 1.0f, false);
        }
        EmitImpactBurst();
    }

    enemy_.ApplyVictoryDefeatPose(kEnemyFallenPoseRatio,
                                  kEnemyFallenPoseBasePos,
                                  kEnemyFallenPoseReferencePos);

    if (ctx_ != nullptr && ctx_->rendering.postEffectManager != nullptr) {
        const float charge =
            SmoothStep01((sceneTime_ - kPreImpactStartTime) /
                         (kImpactTime - kPreImpactStartTime));
        const float age = sceneTime_ - kImpactTime;
        const float flash =
            age >= 0.0f ? 1.0f - SmoothStep01(age / 0.16f) : 0.0f;
        const float aftershock =
            age >= 0.0f ? 1.0f - SmoothStep01(age / 0.42f) : 0.0f;
        const float toonCover = age >= 0.0f
                                    ? SmoothStep01((age - 0.18f) / 0.14f)
                                    : 0.0f;
        const float blur =
            0.018f * charge + 0.155f * flash + 0.055f * aftershock;
        ctx_->rendering.postEffectManager->SetBaseProfile(
            MakeVictoryPostProcessProfile(blur * toonCover,
                                          toonCover > 0.001f));
    }
}

void GameVictoryScene::ResetVictoryConfetti(float screenWidth,
                                            float screenHeight) {
    for (size_t i = 0; i < victoryConfetti_.size(); ++i) {
        RespawnVictoryConfetti(victoryConfetti_[i], screenWidth, screenHeight,
                               i, true);
    }
}

void GameVictoryScene::RespawnVictoryConfetti(VictoryConfetti &confetti,
                                              float screenWidth,
                                              float screenHeight, size_t index,
                                              bool initial) {
    const uint32_t seed = static_cast<uint32_t>(index * 977u + 31u);
    const float side = Hash01(seed + 5u) < 0.5f ? -1.0f : 1.0f;
    const float burstT = Hash01(seed + (initial ? 11u : 13u));
    if (initial && index < victoryConfetti_.size() * 3u / 4u) {
        const float spread = (Hash01(seed + 3u) - 0.5f) * screenWidth * 0.62f;
        const float heightBand = Hash01(seed + 7u);
        confetti.position.x = screenWidth * 0.5f + spread;
        confetti.position.y = screenHeight * (0.18f + heightBand * 0.18f);

        const float speed = 58.0f + Hash01(seed + 23u) * 168.0f;
        confetti.velocity.x =
            side * speed + (Hash01(seed + 31u) - 0.5f) * 92.0f;
        confetti.velocity.y = -(24.0f + burstT * 92.0f);
        confetti.startTime = Hash01(seed + 19u) * 0.10f;
    } else {
        confetti.position.x =
            screenWidth * (0.08f + Hash01(seed + 3u) * 0.84f);
        confetti.position.y =
            -screenHeight * (0.08f + Hash01(seed + 7u) * 0.20f);
        confetti.velocity.x =
            (Hash01(seed + 23u) - 0.5f) * (54.0f + Hash01(seed + 29u) * 70.0f);
        confetti.velocity.y = 16.0f + Hash01(seed + 31u) * 42.0f;
        confetti.startTime =
            resultTimer_ + (initial ? kConfettiDriftDelay : 0.14f) +
            Hash01(seed + 19u) * (initial ? 1.12f : 0.74f);
    }
    confetti.width = 7.0f + Hash01(seed + 37u) * 12.0f;
    confetti.height = 6.0f + Hash01(seed + 41u) * 12.0f;
    if ((index % 5u) == 0u) {
        std::swap(confetti.width, confetti.height);
    }
    confetti.phase = Hash01(seed + 43u) * kPi * 2.0f;
    confetti.spinSpeed = 2.0f + Hash01(seed + 47u) * 5.6f;
    confetti.resetDelay = Hash01(seed + 53u) * 0.90f;
    confetti.shine = 0.18f + Hash01(seed + 59u) * 0.42f;

    static const std::array<XMFLOAT4, 8> kPaperColors{
        Color(1.00f, 0.22f, 0.28f, 1.0f),
        Color(1.00f, 0.74f, 0.14f, 1.0f),
        Color(0.18f, 0.84f, 0.42f, 1.0f),
        Color(0.18f, 0.58f, 1.00f, 1.0f),
        Color(0.58f, 0.34f, 1.00f, 1.0f),
        Color(1.00f, 0.36f, 0.74f, 1.0f),
        Color(0.92f, 0.96f, 1.00f, 1.0f),
        Color(0.28f, 0.94f, 0.92f, 1.0f)};
    confetti.color = kPaperColors[index % kPaperColors.size()];
    confetti.highlightColor =
        LerpColor(confetti.color, Color(1.0f, 1.0f, 1.0f, 1.0f),
                  0.35f + Hash01(seed + 61u) * 0.25f);
}

void GameVictoryScene::UpdateVictoryConfetti(float deltaTime, float screenWidth,
                                             float screenHeight) {
    const float burstGate =
        std::clamp(resultTimer_ / 0.18f, 0.0f, 1.0f);
    const float gust =
        std::sinf(resultTimer_ * 0.82f) * kConfettiWind +
        std::sinf(resultTimer_ * 1.74f + 1.4f) * (kConfettiWind * 0.46f);
    for (size_t i = 0; i < victoryConfetti_.size(); ++i) {
        VictoryConfetti &confetti = victoryConfetti_[i];
        if (resultTimer_ < confetti.startTime) {
            continue;
        }
        const float flutter =
            std::sinf((resultTimer_ - confetti.startTime) *
                          confetti.spinSpeed +
                      confetti.phase);
        confetti.velocity.y =
            (std::min)(confetti.velocity.y + kConfettiGravity * deltaTime,
                       82.0f + confetti.resetDelay * 44.0f);
        confetti.position.x +=
            ((confetti.velocity.x * burstGate) + gust + flutter * 32.0f) *
            deltaTime;
        confetti.position.y +=
            (confetti.velocity.y +
             std::sinf(resultTimer_ * (confetti.spinSpeed * 0.55f) +
                       confetti.phase * 1.7f) *
                 9.0f) *
            deltaTime;

        if (confetti.position.x < -screenWidth * 0.08f) {
            confetti.position.x += screenWidth * 1.16f;
        } else if (confetti.position.x > screenWidth * 1.08f) {
            confetti.position.x -= screenWidth * 1.16f;
        }

        if (confetti.position.y >
            screenHeight + 52.0f + confetti.resetDelay * 220.0f) {
            RespawnVictoryConfetti(confetti, screenWidth, screenHeight, i,
                                   false);
        }
    }
}

void GameVictoryScene::DrawVictoryConfetti(float screenWidth, float screenHeight,
                                           float alpha) {
    if (alpha <= 0.001f) {
        return;
    }

    for (const VictoryConfetti &confetti : victoryConfetti_) {
        if (resultTimer_ < confetti.startTime) {
            continue;
        }
        if (confetti.position.y < -48.0f ||
            confetti.position.y > screenHeight + 48.0f) {
            continue;
        }
        const float flutter =
            std::sinf((resultTimer_ - confetti.startTime) *
                          confetti.spinSpeed +
                      confetti.phase);
        const float face = 0.22f + 0.78f * std::fabs(flutter);
        const float sway =
            std::sinf((resultTimer_ - confetti.startTime) * 1.35f +
                      confetti.phase) *
            5.0f;
        XMFLOAT4 body = confetti.color;
        body.w = alpha * (0.64f + 0.24f * face);
        XMFLOAT4 shine = confetti.highlightColor;
        shine.w = alpha * confetti.shine * (0.16f + 0.28f * face);

        const float drawW = confetti.width * (0.36f + face * 0.64f);
        const float drawH = confetti.height * (0.86f + (1.0f - face) * 0.28f);
        const float x =
            std::clamp(confetti.position.x + sway, -32.0f, screenWidth + 32.0f);
        const float y = confetti.position.y;
        DrawRect(x, y, drawW, drawH, body);

        if (face > 0.58f) {
            DrawRect(x + drawW * 0.12f, y + drawH * 0.12f, drawW * 0.72f,
                     (std::max)(1.0f, drawH * 0.14f), shine);
        }
    }
}

void GameVictoryScene::StartResultCrowdAudio() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr ||
        resultCrowdIntroSoundId_ == SoundManager::kInvalidSoundId ||
        resultCrowdIntroVoiceHandle_ != SoundManager::kInvalidVoiceHandle) {
        return;
    }

    resultCrowdIntroVoiceHandle_ =
        ctx_->systems.sound->Play(resultCrowdIntroSoundId_, 0.86f, false);
}

void GameVictoryScene::StopResultCrowdAudio() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr) {
        return;
    }

    if (resultCrowdIntroVoiceHandle_ != SoundManager::kInvalidVoiceHandle) {
        ctx_->systems.sound->Stop(resultCrowdIntroVoiceHandle_);
        resultCrowdIntroVoiceHandle_ = SoundManager::kInvalidVoiceHandle;
    }
}

void GameVictoryScene::EmitPreImpactBurst() {
}

void GameVictoryScene::EmitImpactBurst() {
}

void GameVictoryScene::UpdateCamera(float screenWidth, float screenHeight) {
    camera_.SetAspect(screenWidth / (std::max)(screenHeight, 1.0f));
    const float slowT = PierceT(sceneTime_);
    const XMFLOAT3 target{0.04f, 1.18f, 0.12f - 0.16f * slowT};
    XMFLOAT3 eye{-0.66f + 0.08f * slowT, 1.18f,
                 -4.16f + 0.16f * slowT};
    XMFLOAT3 lookAt = target;

    const float age = sceneTime_ - kImpactTime;
    const float shake =
        age >= 0.0f
            ? (1.0f - SmoothStep01(age / kExplosionShakeDuration)) *
                  (0.055f + 0.185f * (1.0f - SmoothStep01(age / 0.34f)))
            : 0.0f;
    const float phase = sceneTime_ * 116.0f;
    if (shake > 0.0001f) {
        const XMFLOAT3 right{std::cosf(0.18f), 0.0f, std::sinf(0.18f)};
        const float sx = std::sinf(phase) * shake;
        const float sy = std::cosf(phase * 1.31f + 0.45f) * shake * 0.58f;
        const float sz = std::sinf(phase * 0.73f + 1.1f) * shake * 0.34f;
        eye.x += right.x * sx;
        eye.y += sy;
        eye.z += right.z * sx + sz;
        lookAt.x -= right.x * sx * 0.28f;
        lookAt.y -= sy * 0.20f;
        lookAt.z -= sz * 0.18f;
    }

    const float fovKick =
        age >= 0.0f ? (1.0f - SmoothStep01(age / 0.46f)) * 5.2f : 0.0f;
    camera_.SetPerspectiveFovDeg(35.5f - 1.2f * slowT + fovKick);
    camera_.SetPosition(eye);
    camera_.SetRotation(CameraRotationLookAt(eye, lookAt));
}

void GameVictoryScene::DrawWorld() {
    ModelManager *model = ctx_->rendering.model;
    if (model == nullptr) {
        return;
    }

    SceneLighting lighting{};
    const float explosionAge = sceneTime_ - kImpactTime;
    const float explosionLight =
        explosionAge >= 0.0f
            ? SmoothStep01(explosionAge / 0.46f) *
                  (1.0f - 0.18f * SmoothStep01((explosionAge - 1.10f) / 0.80f))
            : 0.0f;
    lighting.keyLightDirection = {-0.52f, -0.70f, 0.34f};
    lighting.keyLightColor = {1.00f + 0.36f * explosionLight,
                              1.12f + 0.64f * explosionLight,
                              1.42f + 1.15f * explosionLight, 1.0f};
    lighting.fillLightDirection = {0.68f, -0.26f, -0.46f};
    lighting.fillLightColor = {0.16f + 0.16f * explosionLight,
                               0.34f + 0.42f * explosionLight,
                               0.78f + 0.92f * explosionLight, 0.58f};
    lighting.ambientColor = {0.14f + 0.12f * explosionLight,
                             0.20f + 0.22f * explosionLight,
                             0.38f + 0.48f * explosionLight, 1.0f};
    lighting.pointLights[0].positionRange = {0.0f, 1.18f, -0.34f,
                                             7.0f + 5.0f * explosionLight};
    lighting.pointLights[0].colorIntensity = {0.10f, 0.58f, 1.0f,
                                              0.65f + 4.10f * explosionLight};
    lighting.pointLights[1].positionRange = {-0.42f, 1.04f, 0.72f,
                                             4.8f + 2.8f * explosionLight};
    lighting.pointLights[1].colorIntensity = {0.32f, 0.86f, 1.0f,
                                              0.45f + 2.20f * explosionLight};
    lighting.lightingParams = {46.0f, 0.32f, 1.22f + 0.86f * explosionLight,
                               0.18f};
    model->SetSceneLighting(lighting);

    SceneFog fog{};
    fog.params = {0.0f, 0.0f, 1.0f, 0.0f};
    model->SetSceneFog(fog);

    if (ctx_->rendering.dxCommon != nullptr) {
        const float blueBack =
            impactEmitted_
                ? SmoothStep01((sceneTime_ - kImpactTime) / 0.42f)
                : 0.0f;
        ctx_->rendering.dxCommon->SetClearColor(0.0f, 0.018f + 0.070f * blueBack,
                                                0.06f + 0.235f * blueBack,
                                                1.0f);
    }

    model->PrepareSkinning({playerModelId_, enemyModelId_});
    model->PreDraw();
    DrawStage();
    if (!impactEmitted_) {
        DrawPreExplosionCharge();
    }
    if (IsPlayerRevealed(sceneTime_) && !impactEmitted_) {
        player_.Draw(model, camera_, true, true, kVictoryPlayerVisualScale);
        DrawSlash();
    }
    model->PostDraw();
}

void GameVictoryScene::DrawStage() {
    DrawBattleArena(ctx_->rendering.model, camera_, arenaModels_, sceneTime_);
}

void GameVictoryScene::DrawPreExplosionCharge() {
    ModelManager *model = ctx_->rendering.model;
    if (model == nullptr) {
        return;
    }

    const float charge =
        1.0f - SmoothStep01((sceneTime_ - kImpactTime) / 0.22f);
    const float start =
        SmoothStep01((sceneTime_ - kPreImpactStartTime) /
                     (kImpactTime - kPreImpactStartTime));
    const float alpha = start * charge;

    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 center{enemyPos.x, enemyPos.y + 0.74f,
                          enemyPos.z - 0.24f};
    const XMFLOAT3 cameraPos = camera_.GetPosition();
    const float yaw = std::atan2f(cameraPos.x - center.x, cameraPos.z - center.z);
    const float pulse = 0.5f + 0.5f * std::sinf(sceneTime_ * 34.0f);
    const float difficultyT = DifficultyRatio(combatDifficulty_);
    const float difficultyScale = 0.62f + 1.08f * difficultyT;
    const float finalSurge =
        SmoothStep01((sceneTime_ - (kImpactTime - 0.24f)) / 0.24f);
    const float scalePulse =
        1.0f + 0.045f * start * std::sinf(sceneTime_ * 28.0f) +
        0.19f * finalSurge;
    const float burst =
        (0.86f + 0.58f * pulse + 0.86f * start + 0.72f * finalSurge) *
        difficultyScale;
    Transform chargedEnemy = enemy_.GetTransform();
    chargedEnemy.scale.x *= kVictoryEnemyVisualScale * scalePulse;
    chargedEnemy.scale.y *=
        kVictoryEnemyVisualScale * (1.0f + (scalePulse - 1.0f) * 0.72f);
    chargedEnemy.scale.z *= kVictoryEnemyVisualScale * scalePulse;

    if (alpha <= 0.002f) {
        model->Draw(enemyModelId_, chargedEnemy, camera_);
        return;
    }

    ModelDrawEffect baseCharge{};
    baseCharge.enabled = true;
    baseCharge.forceOpaqueMaterial = true;
    baseCharge.blendOverride = ModelDrawEffectBlendOverride::Opaque;
    const XMFLOAT4 heatColor = HeatColorForDifficulty(combatDifficulty_, 1.0f);
    const XMFLOAT4 brightHeat =
        BrightHeatColorForDifficulty(combatDifficulty_, 1.0f);
    const XMFLOAT4 smokeHeat =
        SmokeHeatColorForDifficulty(combatDifficulty_, 1.0f);

    baseCharge.color = {heatColor.x, heatColor.y, heatColor.z, 0.20f * alpha};
    baseCharge.intensity = (0.22f + 0.42f * pulse + 0.82f * finalSurge) * alpha;
    baseCharge.fresnelPower = 0.92f;
    baseCharge.noiseAmount = 0.18f + 0.34f * start;
    baseCharge.alphaBoost = 0.80f;
    baseCharge.surfaceTint = 0.10f + 0.12f * finalSurge;
    baseCharge.time = sceneTime_ * 5.0f;
    model->SetDrawEffect(baseCharge);
    model->Draw(enemyModelId_, chargedEnemy, camera_);
    model->ClearDrawEffect();

    ModelDrawEffect enemyCharge{};
    enemyCharge.enabled = true;
    enemyCharge.additiveBlend = true;
    enemyCharge.disableCulling = true;
    enemyCharge.blendOverride = ModelDrawEffectBlendOverride::Additive;
    enemyCharge.color = {brightHeat.x, brightHeat.y, brightHeat.z,
                         (0.58f + 0.32f * finalSurge) * alpha};
    enemyCharge.intensity =
        (1.08f + 1.36f * pulse + 2.10f * finalSurge) * alpha;
    enemyCharge.fresnelPower = 0.62f;
    enemyCharge.noiseAmount = 0.55f + 0.30f * pulse;
    enemyCharge.alphaBoost = 1.0f;
    enemyCharge.surfaceTint = 0.34f + 0.20f * finalSurge;
    enemyCharge.time = sceneTime_ * 8.0f;
    model->SetDrawEffect(enemyCharge);
    Transform glowEnemy = enemy_.GetTransform();
    glowEnemy.scale.x *= kVictoryEnemyVisualScale * scalePulse * (1.070f + 0.048f * pulse);
    glowEnemy.scale.y *= kVictoryEnemyVisualScale * scalePulse * (1.042f + 0.036f * pulse);
    glowEnemy.scale.z *= kVictoryEnemyVisualScale * scalePulse * (1.070f + 0.048f * pulse);
    model->Draw(enemyModelId_, glowEnemy, camera_);
    model->ClearDrawEffect();

    if (fireBillboardModelId_ == 0 && smokeBillboardModelId_ == 0) {
        return;
    }

    struct ChargePatch {
        uint32_t modelId;
        XMFLOAT3 offset;
        XMFLOAT2 scale;
        float roll;
        XMFLOAT4 color;
        bool additive;
    };

    const ChargePatch patches[] = {
        {fireBillboardModelId_, {0.0f, 0.0f, 0.0f}, {2.34f, 1.34f}, 0.00f,
         {0.08f, 0.76f, 1.0f, 0.76f}, true},
        {fireBillboardModelId_, {-0.48f, 0.08f, -0.02f}, {1.50f, 0.54f},
         -0.55f, {0.22f, 0.94f, 1.0f, 0.58f}, true},
        {fireBillboardModelId_, {0.48f, 0.18f, -0.03f}, {1.58f, 0.58f},
         0.52f, {0.22f, 0.94f, 1.0f, 0.58f}, true},
        {fireBillboardModelId_, {0.02f, 0.42f, -0.05f}, {1.12f, 1.52f},
         1.56f, {0.42f, 0.96f, 1.0f, 0.50f}, true},
        {fireBillboardModelId_, {0.02f, -0.30f, -0.02f}, {1.70f, 0.44f},
         -0.08f, {0.08f, 0.58f, 1.0f, 0.46f}, true},
        {smokeBillboardModelId_, {0.0f, 0.10f, -0.04f}, {2.82f, 1.42f},
         0.12f, {0.020f, 0.10f, 0.40f, 0.70f}, false},
    };

    for (const ChargePatch &patch : patches) {
        if (patch.modelId == 0) {
            continue;
        }
        ModelDrawEffect effect{};
        effect.enabled = true;
        effect.additiveBlend = patch.additive;
        effect.disableCulling = true;
        effect.blendOverride = patch.additive
                                   ? ModelDrawEffectBlendOverride::Additive
                                   : ModelDrawEffectBlendOverride::Alpha;
        effect.color = patch.additive ? brightHeat : smokeHeat;
        effect.color.w = patch.color.w;
        effect.color.w *= alpha;
        effect.intensity =
            patch.additive ? (1.70f + 1.45f * pulse + 1.90f * finalSurge) * alpha
                           : (0.16f + 0.18f * finalSurge) * alpha;
        effect.fresnelPower = 0.58f;
        effect.noiseAmount = 0.38f + 0.22f * pulse;
        effect.alphaBoost = 1.0f;
        effect.surfaceTint = patch.additive ? 0.92f : 0.46f;
        effect.time = sceneTime_ * 7.0f;
        model->SetDrawEffect(effect);

        Transform billboard{};
        billboard.position = {center.x + patch.offset.x, center.y + patch.offset.y,
                              center.z + patch.offset.z};
        billboard.rotation = MakeQuat(0.0f, yaw, patch.roll);
        billboard.scale = {patch.scale.x * burst, patch.scale.y * burst, 1.0f};
        model->Draw(patch.modelId, billboard, camera_);
    }

    model->ClearDrawEffect();
}

void GameVictoryScene::DrawExplosionFlash() {
    ModelManager *model = ctx_->rendering.model;
    if (!impactEmitted_ || model == nullptr || explosionFlashModelId_ == 0) {
        return;
    }

    const float age = (std::max)(0.0f, sceneTime_ - kImpactTime);
    const float flash = 1.0f - SmoothStep01(age / kExplosionFlashDuration);
    if (flash <= 0.001f) {
        return;
    }

    const float difficultyT = DifficultyRatio(combatDifficulty_);
    const float scale = (0.98f + 0.62f * difficultyT) * kExplosionSizeBoost;
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 center{enemyPos.x + 0.02f, enemyPos.y + 1.10f,
                          enemyPos.z - 0.42f};
    const XMFLOAT3 cameraPos = camera_.GetPosition();
    const float yaw = std::atan2f(cameraPos.x - center.x, cameraPos.z - center.z);
    const XMFLOAT4 heat = HeatColorForDifficulty(combatDifficulty_, 1.0f);

    ModelDrawEffect effect{};
    effect.enabled = true;
    effect.additiveBlend = true;
    effect.disableCulling = true;
    effect.blendOverride = ModelDrawEffectBlendOverride::Additive;
    effect.color = {std::clamp(0.90f + heat.x * 0.20f, 0.0f, 1.0f),
                    std::clamp(0.84f + heat.y * 0.18f, 0.0f, 1.0f),
                    std::clamp(0.56f + heat.z * 0.16f, 0.0f, 1.0f),
                    0.82f * flash};
    effect.intensity = 2.35f * flash;
    effect.fresnelPower = 0.32f;
    effect.alphaBoost = 1.0f;
    effect.surfaceTint = 0.92f;
    effect.time = sceneTime_ * 10.0f;
    model->SetDrawEffect(effect);

    Transform flashTransform{};
    flashTransform.position = center;
    flashTransform.rotation = MakeQuat(0.0f, yaw, 0.0f);
    flashTransform.scale = {scale * 1.48f, scale * 1.08f, 1.0f};
    model->Draw(explosionFlashModelId_, flashTransform, camera_);
    model->ClearDrawEffect();
}

void GameVictoryScene::DrawExplosionCore() {
    ModelManager *model = ctx_->rendering.model;
    if (!impactEmitted_ || model == nullptr || explosionCoreModelId_ == 0) {
        return;
    }

    const float age = (std::max)(0.0f, sceneTime_ - kImpactTime);
    const float fade = 1.0f - SmoothStep01(age / kExplosionCoreDuration);
    if (fade <= 0.001f) {
        return;
    }

    const float difficultyT = DifficultyRatio(combatDifficulty_);
    const float coreScale =
        (0.16f + 0.18f * difficultyT) * kExplosionSizeBoost;
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 center{enemyPos.x + 0.02f, enemyPos.y + 1.10f,
                          enemyPos.z - 0.42f};
    const XMFLOAT4 heat = HeatColorForDifficulty(combatDifficulty_, 1.0f);

    ModelDrawEffect effect{};
    effect.enabled = true;
    effect.forceOpaqueMaterial = true;
    effect.blendOverride = ModelDrawEffectBlendOverride::Opaque;
    effect.color = {std::clamp(0.96f + heat.x * 0.08f, 0.0f, 1.0f),
                    std::clamp(0.82f + heat.y * 0.12f, 0.0f, 1.0f),
                    std::clamp(0.38f + heat.z * 0.10f, 0.0f, 1.0f), 1.0f};
    effect.intensity = 0.78f * fade;
    effect.fresnelPower = 0.54f;
    effect.noiseAmount = 0.12f;
    effect.surfaceTint = 0.60f;
    effect.time = sceneTime_ * 7.0f;
    model->SetDrawEffect(effect);

    Transform core{};
    core.position = center;
    core.rotation = MakeQuat(sceneTime_ * 1.5f, sceneTime_ * 0.9f,
                             sceneTime_ * 0.7f);
    core.scale = {coreScale * 1.15f, coreScale * 0.84f, coreScale};
    model->Draw(explosionCoreModelId_, core, camera_);
    model->ClearDrawEffect();
}

void GameVictoryScene::DrawExplosionBillboards() {
    ModelManager *model = ctx_->rendering.model;
    if (!impactEmitted_ || model == nullptr) {
        return;
    }

    const float age = (std::max)(0.0f, sceneTime_ - kImpactTime);
    if (age < kExplosionBillboardStart ||
        age > (kExplosionBillboardFadeStart + kExplosionBillboardFadeDuration)) {
        return;
    }

    const float difficultyT = DifficultyRatio(combatDifficulty_);
    const float smokeT =
        SmoothStep01((age - kExplosionBillboardStart) / kExplosionBillboardRise);
    const float fade =
        1.0f -
        SmoothStep01((age - kExplosionBillboardFadeStart) /
                     kExplosionBillboardFadeDuration);
    const float alpha =
        smokeT * fade * (1.28f + 0.30f * difficultyT) * kExplosionSizeBoost;
    if (alpha <= 0.001f) {
        return;
    }

    const XMFLOAT4 smokeHeat =
        SmokeHeatColorForDifficulty(combatDifficulty_, 1.0f);
    const XMFLOAT4 sootHeat =
        DarkSmokeHeatColorForDifficulty(combatDifficulty_, 1.0f);
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 center{enemyPos.x + 0.02f, enemyPos.y + 1.10f,
                          enemyPos.z - 0.42f};
    const XMFLOAT3 cameraPos = camera_.GetPosition();
    const float yaw = std::atan2f(cameraPos.x - center.x, cameraPos.z - center.z);

    struct SmokePatch {
        XMFLOAT3 offset;
        XMFLOAT2 scale;
        float roll;
        float delay;
    };

    const SmokePatch patches[] = {
        {{0.00f, 0.08f, -0.04f}, {2.10f, 1.02f}, 0.06f, 0.00f},
        {{-0.46f, 0.15f, -0.03f}, {1.66f, 0.80f}, -0.24f, 0.06f},
        {{0.46f, 0.21f, -0.02f}, {1.60f, 0.78f}, 0.28f, 0.11f},
        {{0.04f, 0.52f, -0.04f}, {1.46f, 0.98f}, -0.12f, 0.17f},
        {{-0.18f, -0.14f, -0.02f}, {1.74f, 0.70f}, 0.18f, 0.21f},
        {{0.28f, 0.02f, -0.03f}, {1.50f, 0.68f}, -0.34f, 0.26f},
        {{-0.66f, 0.34f, -0.02f}, {1.14f, 0.62f}, 0.42f, 0.33f},
        {{0.66f, 0.38f, -0.03f}, {1.10f, 0.60f}, -0.46f, 0.39f},
        {{-0.02f, 0.82f, -0.05f}, {1.14f, 0.88f}, 0.72f, 0.48f},
        {{0.02f, -0.34f, -0.02f}, {1.58f, 0.52f}, -0.66f, 0.53f},
        {{-0.38f, -0.40f, -0.01f}, {1.12f, 0.40f}, 0.36f, 0.62f},
        {{0.40f, -0.36f, -0.01f}, {1.08f, 0.40f}, -0.40f, 0.70f},
        {{-0.70f, -0.02f, -0.02f}, {0.90f, 0.54f}, 0.12f, 0.78f},
        {{0.72f, 0.02f, -0.02f}, {0.86f, 0.52f}, -0.16f, 0.88f},
    };

    const uint32_t smokeModelId =
        darkSmokeBillboardModelId_ != 0 ? darkSmokeBillboardModelId_
                                        : smokeBillboardModelId_;
    if (smokeModelId == 0) {
        return;
    }

    for (const SmokePatch &patch : patches) {
        const float local = SmoothStep01((age - kExplosionBillboardStart -
                                          patch.delay * 0.75f) /
                                         kExplosionBillboardRise);
        if (local <= 0.001f) {
            continue;
        }

        ModelDrawEffect effect{};
        effect.enabled = true;
        effect.additiveBlend = false;
        effect.disableCulling = true;
        effect.blendOverride = ModelDrawEffectBlendOverride::Alpha;
        const float tintAmount =
            0.46f + 0.26f * difficultyT +
            0.10f * std::sinf(sceneTime_ * 1.35f + patch.delay * 11.0f);
        effect.color = LerpColor(sootHeat, smokeHeat, tintAmount);
        effect.color.w = alpha * local;
        effect.intensity = 0.16f + 0.18f * difficultyT;
        effect.fresnelPower = 0.78f;
        effect.noiseAmount = 0.96f;
        effect.baseDim = 0.0f;
        effect.alphaBoost = 2.12f;
        effect.surfaceTint = 0.48f;
        effect.time = sceneTime_ * 0.38f + patch.delay * 7.0f;
        model->SetDrawEffect(effect);

        Transform billboard{};
        const float drift =
            SmoothStep01((age - kExplosionBillboardStart) / 1.65f);
        const float size =
            (1.48f + 0.56f * difficultyT) * (1.0f + 1.46f * drift) *
            kExplosionSizeBoost;
        billboard.position = {center.x + patch.offset.x * (1.0f + 0.50f * drift),
                              center.y + patch.offset.y * (1.0f + 0.42f * drift),
                              center.z + patch.offset.z};
        billboard.rotation = MakeQuat(0.0f, yaw, patch.roll);
        const float pulse =
            1.0f +
            0.035f * std::sinf(sceneTime_ * 2.4f + patch.delay * 13.0f);
        billboard.scale = {patch.scale.x * size * pulse,
                           patch.scale.y * size * pulse, 1.0f};
        model->Draw(smokeModelId, billboard, camera_);
    }

    model->ClearDrawEffect();
}

void GameVictoryScene::DrawForegroundEnemy() {
    ModelManager *model = ctx_ != nullptr ? ctx_->rendering.model : nullptr;
    if (model == nullptr || !impactEmitted_) {
        return;
    }

    model->ClearDrawEffect();

    const float rawPierceT = PierceRawT(sceneTime_);
    const float poseRatio = 0.38f + 0.62f * EaseOutCubic(rawPierceT);
    const float thrustPulse =
        std::sinf(rawPierceT * kPi) *
        (0.35f + 0.65f * (1.0f - SmoothStep01(rawPierceT)));
    const float playerYaw = player_.GetYaw();
    const XMFLOAT4 heatColor = HeatColorForDifficulty(combatDifficulty_, 1.0f);
    const XMFLOAT4 brightHeat =
        BrightHeatColorForDifficulty(combatDifficulty_, 1.0f);
    auto makePlayerVisual = [&](float scaleBoost) {
        Transform visual = player_.GetTransform();
        const float scale =
            kPlayerModelScaleMultiplier * kVictoryPlayerVisualScale * scaleBoost;
        visual.scale.x *= scale;
        visual.scale.y *= scale;
        visual.scale.z *= scale;
        const float lean = (0.28f + 0.46f * poseRatio);
        XMVECTOR baseRot = XMLoadFloat4(&visual.rotation);
        XMVECTOR qLean =
            XMQuaternionRotationAxis(XMVectorSet(1, 0, 0, 0), lean);
        XMStoreFloat4(&visual.rotation,
                      XMQuaternionNormalize(XMQuaternionMultiply(qLean, baseRot)));
        visual.position.y -= 0.04f * poseRatio;
        visual.position.z += std::cosf(playerYaw) * 0.18f * poseRatio;
        visual.position.x += std::sinf(playerYaw) * 0.18f * poseRatio;
        return visual;
    };

    ModelDrawEffect halo{};
    halo.enabled = true;
    halo.additiveBlend = true;
    halo.disableCulling = true;
    halo.blendOverride = ModelDrawEffectBlendOverride::Additive;
    halo.color = {brightHeat.x, brightHeat.y, brightHeat.z, 0.46f};
    halo.intensity = 1.10f + 0.42f * thrustPulse;
    halo.fresnelPower = 0.82f;
    halo.noiseAmount = 0.12f;
    halo.alphaBoost = 1.0f;
    halo.surfaceTint = 0.76f;
    halo.time = sceneTime_ * 5.0f;
    if (!IsPlayerRevealed(sceneTime_)) {
        return;
    }

    model->SetDrawEffect(halo);
    model->Draw(playerModelId_, makePlayerVisual(1.15f), camera_);
    model->ClearDrawEffect();

    player_.Draw(model, camera_, true, true, kVictoryPlayerVisualScale);

    ModelDrawEffect swordGlow{};
    swordGlow.enabled = true;
    swordGlow.additiveBlend = true;
    swordGlow.disableCulling = true;
    swordGlow.blendOverride = ModelDrawEffectBlendOverride::Additive;
    swordGlow.color = {brightHeat.x, brightHeat.y, brightHeat.z, 0.48f};
    swordGlow.intensity = 1.18f + 0.42f * thrustPulse;
    swordGlow.fresnelPower = 0.46f;
    swordGlow.noiseAmount = 0.06f;
    swordGlow.alphaBoost = 1.0f;
    swordGlow.surfaceTint = 0.86f;
    swordGlow.time = sceneTime_ * 4.8f;
    model->SetDrawEffect(swordGlow);
    for (const Sword *sword : player_.GetSwords()) {
        if (sword != nullptr) {
            const_cast<Sword *>(sword)->Draw(model, camera_,
                                             kVictoryPlayerVisualScale * 1.04f);
        }
    }
    model->ClearDrawEffect();

    ModelDrawEffect rim{};
    rim.enabled = true;
    rim.additiveBlend = true;
    rim.disableCulling = true;
    rim.blendOverride = ModelDrawEffectBlendOverride::Additive;
    rim.color = {heatColor.x, heatColor.y, heatColor.z, 0.30f};
    rim.intensity = 0.58f + 0.22f * thrustPulse;
    rim.fresnelPower = 1.18f;
    rim.noiseAmount = 0.04f;
    rim.alphaBoost = 0.86f;
    rim.surfaceTint = 0.62f;
    rim.time = sceneTime_ * 3.2f;
    model->SetDrawEffect(rim);
    model->Draw(playerModelId_, makePlayerVisual(1.04f), camera_);
    model->ClearDrawEffect();
}

void GameVictoryScene::DrawSlash() {
    const float rawPierceT = PierceRawT(sceneTime_);
    const float pierceT = EaseOutCubic(rawPierceT);
    const float life = 1.0f - SmoothStep01((sceneTime_ - 1.70f) / 2.20f);
    const float speedStress = std::sinf(rawPierceT * kPi);
    const float difficultyScale = 0.72f + 0.86f * DifficultyRatio(combatDifficulty_);
    const float alpha =
        std::clamp((0.32f + 0.34f * speedStress) * life, 0.0f, 0.62f);
    if (!IsPlayerRevealed(sceneTime_) || alpha <= 0.01f || slashModelId_ == 0) {
        return;
    }

    ModelDrawEffect effect{};
    effect.enabled = true;
    effect.additiveBlend = true;
    effect.disableCulling = true;
    const XMFLOAT4 slashColor =
        BrightHeatColorForDifficulty(combatDifficulty_, alpha, 0.18f);
    effect.color = slashColor;
    effect.intensity = 0.62f * alpha;
    effect.fresnelPower = 0.35f;
    effect.noiseAmount = 0.0f;
    effect.time = sceneTime_;
    ctx_->rendering.model->SetDrawEffect(effect);

    Transform slash{};
    slash.position = {0.0f, 1.08f, 0.80f - 1.42f * pierceT};
    slash.rotation = MakeQuat(-0.04f, 0.0f, -kPi * 0.5f);
    slash.scale = {5.45f * difficultyScale,
                   (0.34f + 0.12f * speedStress) * difficultyScale, 1.0f};
    ctx_->rendering.model->Draw(slashModelId_, slash, camera_);

    Transform after{};
    after.position = {0.0f, 0.82f, 1.26f - 1.72f * pierceT};
    after.rotation = MakeQuat(-0.02f, 0.0f, -kPi * 0.5f);
    after.scale = {4.45f * difficultyScale, 0.18f * difficultyScale, 1.0f};
    ctx_->rendering.model->Draw(slashModelId_, after, camera_);

    Transform ghost{};
    ghost.position = {0.0f, 1.28f, 1.58f - 1.96f * pierceT};
    ghost.rotation = MakeQuat(-0.08f, 0.0f, -kPi * 0.5f);
    ghost.scale = {3.90f * difficultyScale, 0.13f * difficultyScale, 1.0f};
    ctx_->rendering.model->Draw(slashModelId_, ghost, camera_);
    ctx_->rendering.model->ClearDrawEffect();
}

void GameVictoryScene::DrawOverlay(float screenWidth, float screenHeight) {
    const float difficultyT = DifficultyRatio(combatDifficulty_);
    const float impactFlash =
        sceneTime_ >= kImpactTime
            ? 1.0f - SmoothStep01((sceneTime_ - kImpactTime) / 0.08f)
            : 0.0f;
    const float blastAfter =
        sceneTime_ >= kImpactTime
            ? 1.0f - SmoothStep01((sceneTime_ - kImpactTime) / 0.25f)
            : 0.0f;
    const float opening = 1.0f - SmoothStep01(sceneTime_ / 0.40f);
    const float fadeIn = SmoothStep01(sceneTime_ / 1.10f);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f, 1.0f - fadeIn));
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f, 0.30f * opening * fadeIn));
    const XMFLOAT4 heatColor = HeatColorForDifficulty(combatDifficulty_, 1.0f);
    if (impactFlash > 0.01f) {
        XMFLOAT4 flashColor{std::clamp(heatColor.x + 0.24f, 0.0f, 1.0f),
                            std::clamp(heatColor.y + 0.24f, 0.0f, 1.0f),
                            std::clamp(heatColor.z + 0.24f, 0.0f, 1.0f),
                            (0.40f + 0.28f * difficultyT) * impactFlash};
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                 flashColor);
    }
    if (blastAfter > 0.01f) {
        XMFLOAT4 afterColor{heatColor.x * 0.42f, heatColor.y * 0.42f,
                            heatColor.z * 0.58f,
                            (0.035f + 0.045f * difficultyT) * blastAfter};
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                 afterColor);
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
    if (sceneTime_ < kExplosionFreezeTime) {
        sceneTime_ = kExplosionFreezeTime;
        UpdateCinematic(0.0f);
    }
    resultMode_ = true;
    resultTimer_ = 0.0f;
    RegisterRanking();
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());
    ResetVictoryConfetti(w, h);
    StartResultCrowdAudio();
    UpdateResultPostProcess();
}

void GameVictoryScene::UpdateResultPostProcess() {
    if (ctx_ != nullptr && ctx_->rendering.postEffectManager != nullptr) {
        const float blurT =
            SmoothStep01((resultTimer_ - kResultBlurDelay) / kResultBlurDuration);
        const float toonPunch = 0.16f + 0.18f * blurT;
        ctx_->rendering.postEffectManager->SetBaseProfile(
            MakeVictoryPostProcessProfile(toonPunch, true));
    }
}

void GameVictoryScene::UpdateResultInput() {
    if (ctx_ == nullptr || ctx_->systems.input == nullptr) {
        return;
    }

    Input &input = *ctx_->systems.input;
    const bool left = input.IsKeyTrigger(DIK_A) || input.IsKeyTrigger(DIK_LEFT) ||
                      (input.IsGamepadConnected() &&
                       input.IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_LEFT));
    const bool right =
        input.IsKeyTrigger(DIK_D) || input.IsKeyTrigger(DIK_RIGHT) ||
        (input.IsGamepadConnected() &&
         input.IsGamepadButtonTrigger(XINPUT_GAMEPAD_DPAD_RIGHT));

    if (left && actionButtonIndex_ != 0) {
        actionButtonIndex_ = 0;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Select);
    }
    if (right && actionButtonIndex_ != 1) {
        actionButtonIndex_ = 1;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Select);
    }

    if (input.IsKeyTrigger(DIK_TAB) || input.IsKeyTrigger(DIK_ESCAPE) ||
        (input.IsGamepadConnected() &&
         input.IsGamepadButtonTrigger(XINPUT_GAMEPAD_B))) {
        actionButtonIndex_ = 1;
    }

    const bool scoreReady =
        resultTimer_ >= kResultTextDelay + 0.30f;
    if (!rankingVisible_ && scoreReady && input.IsKeyTrigger(DIK_SPACE)) {
        rankingVisible_ = true;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
        return;
    }

    const bool confirm =
        input.IsKeyTrigger(DIK_RETURN) ||
        (rankingVisible_ && input.IsKeyTrigger(DIK_SPACE)) ||
        (input.IsGamepadConnected() &&
         input.IsGamepadButtonTrigger(XINPUT_GAMEPAD_A));
    if (!confirm || exitRequested_) {
        return;
    }

    AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
    exitRequested_ = true;
    exitTimer_ = 0.0f;
    exitTargetIndex_ = actionButtonIndex_;
}

void GameVictoryScene::RegisterRanking() {
    if (rankingRegistered_) {
        return;
    }

    rankingRegistered_ = true;
    currentRank_ = -1;
    LoadRanking();
    rankingEntries_.push_back({currentScore_, combatDifficulty_, clearTime_,
                               inputCalibration_.controlType, true});
    std::stable_sort(rankingEntries_.begin(), rankingEntries_.end(),
                     [](const RankingEntry &a, const RankingEntry &b) {
                         if (a.score != b.score) {
                             return a.score > b.score;
                         }
                         if (std::fabs(a.clearTime - b.clearTime) > 0.001f) {
                             return a.clearTime < b.clearTime;
                         }
                         return a.difficulty > b.difficulty;
                     });

    for (size_t i = 0; i < rankingEntries_.size(); ++i) {
        if (rankingEntries_[i].isCurrent) {
            currentRank_ = static_cast<int>(i) + 1;
            break;
        }
    }
    SaveRanking();

    const size_t firstVisible =
        currentRank_ > static_cast<int>(kRankingDrawCount)
            ? static_cast<size_t>(currentRank_ - kRankingDrawCount)
            : 0u;
    rankingDisplayFirstRank_ = static_cast<int>(firstVisible) + 1;
    if (firstVisible > 0 && firstVisible < rankingEntries_.size()) {
        const size_t count =
            (std::min)(kRankingDrawCount, rankingEntries_.size() - firstVisible);
        std::vector<RankingEntry> visible(
            rankingEntries_.begin() + static_cast<std::ptrdiff_t>(firstVisible),
            rankingEntries_.begin() +
                static_cast<std::ptrdiff_t>(firstVisible + count));
        rankingEntries_ = std::move(visible);
    } else if (rankingEntries_.size() > kRankingDrawCount) {
        rankingEntries_.resize(kRankingDrawCount);
    }
}

void GameVictoryScene::LoadRanking() {
    rankingEntries_.clear();

    std::ifstream file(RankingPath(), std::ios::binary);
    if (!file) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream stream(line);
        RankingEntry entry{};
        std::string mode;
        if (stream >> mode >> entry.score >> entry.difficulty >>
            entry.clearTime) {
            entry.controlType = RankingModeFromToken(mode);
        } else {
            stream.clear();
            stream.str(line);
            if (!(stream >> entry.score >> entry.difficulty >>
                  entry.clearTime)) {
                continue;
            }
            entry.controlType = InputControlType::KeyboardMouse;
        }

        if (entry.controlType != inputCalibration_.controlType) {
            continue;
        }
        entry.score = (std::max)(0, entry.score);
        entry.difficulty = std::clamp(entry.difficulty, 0.0f, 9.0f);
        entry.clearTime = (std::max)(0.0f, entry.clearTime);
        rankingEntries_.push_back(entry);
    }
}

void GameVictoryScene::SaveRanking() const {
    const std::filesystem::path path = RankingPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return;
    }

    std::vector<RankingEntry> allEntries;
    {
        std::ifstream file(path, std::ios::binary);
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }

            std::istringstream stream(line);
            RankingEntry entry{};
            std::string mode;
            if (stream >> mode >> entry.score >> entry.difficulty >>
                entry.clearTime) {
                entry.controlType = RankingModeFromToken(mode);
            } else {
                stream.clear();
                stream.str(line);
                if (!(stream >> entry.score >> entry.difficulty >>
                      entry.clearTime)) {
                    continue;
                }
                entry.controlType = InputControlType::KeyboardMouse;
            }
            if (entry.controlType != inputCalibration_.controlType) {
                allEntries.push_back(entry);
            }
        }
    }

    for (RankingEntry entry : rankingEntries_) {
        entry.isCurrent = false;
        allEntries.push_back(entry);
    }

    std::stable_sort(allEntries.begin(), allEntries.end(),
                     [](const RankingEntry &a, const RankingEntry &b) {
                         if (a.controlType != b.controlType) {
                             return static_cast<int>(a.controlType) <
                                    static_cast<int>(b.controlType);
                         }
                         if (a.score != b.score) {
                             return a.score > b.score;
                         }
                         if (std::fabs(a.clearTime - b.clearTime) > 0.001f) {
                             return a.clearTime < b.clearTime;
                         }
                         return a.difficulty > b.difficulty;
                     });

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return;
    }

    file << "# mode score difficulty clear_time\n";
    for (const RankingEntry &entry : allEntries) {
        file << RankingModeToken(entry.controlType) << '\t' << entry.score
             << '\t' << std::fixed << std::setprecision(1) << entry.difficulty
             << '\t' << std::setprecision(2) << entry.clearTime << '\n';
    }
}

void GameVictoryScene::DrawResultOverlay(float screenWidth, float screenHeight) {
    const float fade =
        SmoothStep01((resultTimer_ - kResultTextDelay) / 0.14f);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f, 0.46f * fade));
    DrawVictoryConfetti(screenWidth, screenHeight, fade);

    const float panelT =
        SmoothStep01((resultTimer_ - (kResultTextDelay + 0.02f)) / 0.16f);
    if (!rankingVisible_) {
        const float titleScale =
            std::clamp(screenWidth * 0.58f /
                           (std::max)(missionCompleteLabel_.width, 1.0f),
                       0.50f, 0.88f);
        const float titleW = missionCompleteLabel_.width * titleScale;
        DrawImage(missionCompleteLabel_, (screenWidth - titleW) * 0.5f,
                  screenHeight * 0.055f, titleScale, fade);
    }

    const float panelW = std::clamp(screenWidth * 0.58f, 720.0f, 960.0f);
    const float panelH = std::clamp(screenHeight * 0.30f, 250.0f, 320.0f);
    const float panelX = (screenWidth - panelW) * 0.5f;
    const float panelY = screenHeight * 0.35f + (1.0f - panelT) * 18.0f;

    if (rankingVisible_) {
        DrawRankingPanel(screenWidth, screenHeight, panelT);
        DrawActionButtons(screenWidth, screenHeight, panelT);
        if (exitRequested_) {
            const float exitFade =
                SmoothStep01(exitTimer_ / kResultExitFadeDuration);
            DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                     Color(0.0f, 0.0f, 0.0f, exitFade));
        }
        return;
    }

    DrawRect(panelX + 14.0f, panelY + 16.0f, panelW, panelH,
             Color(0.0f, 0.0f, 0.0f, 0.32f * panelT));
    DrawRect(panelX, panelY, panelW, panelH,
             Color(0.018f, 0.021f, 0.026f, 0.82f * panelT));
    DrawFrame(panelX, panelY, panelW, panelH, 2.0f,
              Color(0.95f, 0.72f, 0.28f, 0.74f * panelT));
    const XMFLOAT4 resultTextColor =
        ReadableResultTextColor(combatDifficulty_, panelT);

    const float scoreLabelScale =
        std::clamp((panelW * 0.17f) /
                       (std::max)(scoreTitleLabel_.width, 1.0f),
                   0.42f, 0.70f);
    const float scoreLabelW = scoreTitleLabel_.width * scoreLabelScale;
    DrawImage(scoreTitleLabel_, panelX + (panelW - scoreLabelW) * 0.5f,
              panelY + panelH * 0.13f, scoreLabelScale, 0.92f * panelT);

    const int animatedScore = static_cast<int>(
        std::round(static_cast<float>(currentScore_) *
                   SmoothStep01((resultTimer_ - (kResultTextDelay + 0.05f)) /
                                0.18f)));
    const std::string score = FormatScore(animatedScore);
    const float scoreScale =
        std::clamp((panelW * 0.34f) / MeasureTextLine(score, 1.0f), 0.96f,
                   1.24f);
    DrawTextLine(score, panelX + panelW * 0.5f, panelY + panelH * 0.30f,
                 scoreScale, resultTextColor);

    const float timeLabelScale =
        std::clamp((panelW * 0.13f) /
                       (std::max)(clearTimeLabel_.width, 1.0f),
                   0.30f, 0.45f);
    const float rowY = panelY + panelH * 0.76f;
    const float timeBlockX = panelX + panelW * 0.21f;
    DrawImage(clearTimeLabel_, timeBlockX, rowY + 4.0f, timeLabelScale,
              0.72f * panelT);

    const std::string time = FormatTime(clearTime_);
    const float timeScale =
        std::clamp((panelW * 0.18f) / MeasureTextLine(time, 1.0f), 0.32f,
                   0.48f);
    XMFLOAT4 rowTextColor = resultTextColor;
    rowTextColor.w = 0.86f * panelT;
    DrawTextLineLeft(time, panelX + panelW * 0.37f, rowY, timeScale,
                     rowTextColor);

    const float difficultyLabelScale =
        std::clamp((panelW * 0.12f) /
                       (std::max)(difficultyLabel_.width, 1.0f),
                   0.28f, 0.42f);
    DrawImage(difficultyLabel_, panelX + panelW * 0.61f, rowY + 6.0f,
              difficultyLabelScale, 0.72f * panelT);

    const std::string difficulty = FormatDifficulty(combatDifficulty_);
    const float difficultyScale =
        std::clamp((panelW * 0.08f) / MeasureTextLine(difficulty, 1.0f),
                   0.32f, 0.48f);
    DrawTextLineLeft(difficulty, panelX + panelW * 0.79f, rowY, difficultyScale,
                     rowTextColor);

    if (exitRequested_) {
        const float exitFade =
            SmoothStep01(exitTimer_ / kResultExitFadeDuration);
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                 Color(0.0f, 0.0f, 0.0f, exitFade));
    }
}

void GameVictoryScene::DrawRankingPanel(float screenWidth, float screenHeight,
                                        float alpha) {
    const float panelW = std::clamp(screenWidth * 0.55f, 620.0f, 860.0f);
    const float buttonTop = screenHeight * 0.805f;
    const float topMargin = std::clamp(screenHeight * 0.12f, 72.0f, 108.0f);
    const float bottomGap = std::clamp(screenHeight * 0.055f, 34.0f, 48.0f);
    const float maxPanelH = (std::max)(360.0f, buttonTop - topMargin - bottomGap);
    const float panelH =
        std::clamp(screenHeight * 0.62f, 380.0f, (std::min)(500.0f, maxPanelH));
    const float x = (screenWidth - panelW) * 0.5f;
    const float y = (std::max)(topMargin,
                               (buttonTop - bottomGap - panelH) * 0.5f);

    DrawRect(x + 12.0f, y + 14.0f, panelW, panelH,
             Color(0.0f, 0.0f, 0.0f, 0.44f * alpha));
    DrawRect(x, y, panelW, panelH,
             Color(0.010f, 0.013f, 0.018f, 0.92f * alpha));
    DrawRect(x + panelW * 0.05f, y + panelH * 0.245f, panelW * 0.90f,
             panelH * 0.60f, Color(0.0f, 0.0f, 0.0f, 0.24f * alpha));
    DrawFrame(x, y, panelW, panelH, 2.0f,
              Color(0.95f, 0.72f, 0.28f, 0.82f * alpha));

    const float titleScale =
        std::clamp((panelW * 0.42f) /
                       (std::max)(rankingTitleLabel_.width, 1.0f),
                   0.48f, 0.78f);
    const float titleW = rankingTitleLabel_.width * titleScale;
    DrawImage(rankingTitleLabel_, x + (panelW - titleW) * 0.5f,
              y + panelH * 0.085f, titleScale, 0.94f * alpha);

    const float contentLeft = x + panelW * 0.08f;
    const float contentRight = x + panelW * 0.92f;
    const float contentW = contentRight - contentLeft;
    const float columnGap = panelW * 0.035f;
    const float rankColumnW = MeasureTextLine("10:", 1.0f);
    const float scoreColumnW = MeasureTextLine("000000", 1.0f);
    const float timeColumnW = MeasureTextLine("99:59.99s", 1.0f);
    const float difficultyColumnW = MeasureTextLine("9.0", 1.0f);
    const float nominalRowWidth =
        rankColumnW + scoreColumnW + timeColumnW + difficultyColumnW +
        columnGap * 3.0f;
    const float rowScale =
        std::clamp(contentW / (std::max)(nominalRowWidth, 1.0f), 0.42f, 0.62f);
    const float scaledColumns =
        (rankColumnW + scoreColumnW + timeColumnW + difficultyColumnW) *
        rowScale;
    const float gap =
        (std::max)(columnGap * rowScale, (contentW - scaledColumns) / 3.0f);
    const float scoreRight =
        contentLeft + rankColumnW * rowScale + gap + scoreColumnW * rowScale;
    const float timeRight = scoreRight + gap + timeColumnW * rowScale;
    const float difficultyRight =
        timeRight + gap + difficultyColumnW * rowScale;
    const float headerY = y + panelH * 0.245f;
    const float rowStartY = y + panelH * 0.335f;
    const float rowGap = std::clamp(panelH * 0.115f, 50.0f, 68.0f);
    const float rowFramePadY = std::clamp(rowGap * 0.18f, 9.0f, 13.0f);
    const float rowFrameH = std::clamp(rowGap * 0.86f, 43.0f, 58.0f);
    const float headerScale = std::clamp(rowScale * 0.86f, 0.34f, 0.50f);
    DrawImage(rankingRankHeaderLabel_, contentLeft, headerY, headerScale,
              0.72f * alpha);
    DrawImage(rankingScoreHeaderLabel_,
              scoreRight - rankingScoreHeaderLabel_.width * headerScale,
              headerY, headerScale, 0.72f * alpha);
    DrawImage(rankingTimeHeaderLabel_,
              timeRight - rankingTimeHeaderLabel_.width * headerScale, headerY,
              headerScale, 0.72f * alpha);
    DrawImage(rankingDifficultyHeaderLabel_,
              difficultyRight -
                  rankingDifficultyHeaderLabel_.width * headerScale,
              headerY, headerScale, 0.72f * alpha);
    DrawRect(contentLeft - panelW * 0.020f, y + panelH * 0.305f,
             contentW + panelW * 0.040f, 1.0f,
             Color(0.95f, 0.72f, 0.28f, 0.30f * alpha));
    const float tableTop = y + panelH * 0.245f;
    const float tableBottom = rowStartY + rowGap * 4.72f;
    const float lineAlpha = 0.22f * alpha;
    DrawRect(contentLeft + rankColumnW * rowScale + gap * 0.50f, tableTop,
             1.0f, tableBottom - tableTop,
             Color(0.95f, 0.72f, 0.28f, lineAlpha));
    DrawRect(scoreRight + gap * 0.50f, tableTop, 1.0f,
             tableBottom - tableTop, Color(0.95f, 0.72f, 0.28f, lineAlpha));
    DrawRect(timeRight + gap * 0.50f, tableTop, 1.0f,
             tableBottom - tableTop, Color(0.95f, 0.72f, 0.28f, lineAlpha));

    const size_t rows = (std::min)(rankingEntries_.size(), kRankingDrawCount);
    for (size_t i = 0; i < rows; ++i) {
        const RankingEntry &entry = rankingEntries_[i];
        const float rowY = rowStartY + static_cast<float>(i) * rowGap;
        const bool highlight = entry.isCurrent;
        const float pulse =
            highlight ? 0.5f + 0.5f * std::sinf(resultTimer_ * 8.0f) : 0.0f;
        DrawRect(contentLeft - panelW * 0.020f, rowY - rowFramePadY,
                 contentW + panelW * 0.040f, rowFrameH,
                 Color(1.0f, 1.0f, 1.0f,
                       (i % 2 == 0 ? 0.030f : 0.015f) * alpha));
        if (highlight) {
            DrawRect(contentLeft - panelW * 0.020f, rowY - rowFramePadY,
                     contentW + panelW * 0.040f, rowFrameH,
                     Color(1.0f, 0.74f, 0.24f,
                           (0.20f + 0.20f * pulse) * alpha));
            DrawFrame(contentLeft - panelW * 0.020f, rowY - rowFramePadY,
                      contentW + panelW * 0.040f, rowFrameH,
                      1.0f + pulse * 2.0f,
                      Color(1.0f, 0.86f, 0.32f,
                            (0.44f + 0.42f * pulse) * alpha));
        }

        std::ostringstream rank;
        const int rankNumber = rankingDisplayFirstRank_ + static_cast<int>(i);
        rank << rankNumber << ":";
        const float textAlpha = (highlight ? 1.0f : 0.88f) * alpha;
        const XMFLOAT4 textColor = Color(1.0f, 1.0f, 1.0f, textAlpha);
        const float rowFrameY = rowY - rowFramePadY;
        const float baselineY =
            rowFrameY + rowFrameH * 0.5f +
            MeasureTextInkCenterOffset("00:00.00s", rowScale);
        const std::string score = FormatScore(entry.score);
        const std::string time = FormatTime(entry.clearTime);
        const std::string difficulty = FormatDifficulty(entry.difficulty);
        DrawTextLineLeftBaseline(rank.str(), contentLeft, baselineY, rowScale,
                                 textColor);
        DrawTextLineLeftBaseline(score,
                                 scoreRight - MeasureTextLine(score, rowScale),
                                 baselineY, rowScale, textColor);
        DrawTextLineLeftBaseline(time,
                                 timeRight - MeasureTextLine(time, rowScale),
                                 baselineY, rowScale, textColor);
        DrawTextLineLeftBaseline(
            difficulty, difficultyRight - MeasureTextLine(difficulty, rowScale),
            baselineY, rowScale, textColor);
    }
}

void GameVictoryScene::DrawActionButtons(float screenWidth, float screenHeight,
                                         float alpha) {
    if (alpha <= 0.001f) {
        return;
    }

    const float buttonW = std::clamp(screenWidth * 0.13f, 176.0f, 232.0f);
    const float buttonH = std::clamp(screenHeight * 0.064f, 56.0f, 72.0f);
    const float buttonGap = std::clamp(screenWidth * 0.026f, 32.0f, 50.0f);
    const float totalW = buttonW * 2.0f + buttonGap;
    const float firstX = (screenWidth - totalW) * 0.5f;
    const float y = screenHeight * 0.805f;
    const Image *labels[2] = {&retryButtonLabel_, &titleButtonLabel_};

    for (int i = 0; i < 2; ++i) {
        const float x = firstX + static_cast<float>(i) * (buttonW + buttonGap);
        const bool selected = i == actionButtonIndex_;
        const XMFLOAT4 body =
            selected ? Color(0.18f, 0.13f, 0.055f, 0.98f * alpha)
                     : Color(0.040f, 0.046f, 0.058f, 0.90f * alpha);
        const XMFLOAT4 line =
            selected ? Color(1.0f, 0.78f, 0.34f, 0.98f * alpha)
                     : Color(0.62f, 0.66f, 0.72f, 0.40f * alpha);

        DrawRect(x + 6.0f, y + 8.0f, buttonW, buttonH,
                 Color(0.0f, 0.0f, 0.0f,
                       (selected ? 0.32f : 0.22f) * alpha));
        DrawRect(x, y, buttonW, buttonH, body);
        DrawFrame(x, y, buttonW, buttonH, selected ? 3.0f : 2.0f, line);

        const Image &label = *labels[i];
        const float labelScale =
            (std::min)({1.0f,
                        (buttonH * 0.60f) / (std::max)(label.height, 1.0f),
                        (buttonW * 0.72f) / (std::max)(label.width, 1.0f)});
        const float labelW = label.width * labelScale;
        const float labelH = label.height * labelScale;
        DrawImage(label, x + (buttonW - labelW) * 0.5f,
                  y + (buttonH - labelH) * 0.5f, labelScale,
                  (selected ? 1.0f : 0.80f) * alpha);
    }
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
    DrawImage(image, x, y, scale, Color(1.0f, 1.0f, 1.0f, alpha));
}

void GameVictoryScene::DrawImage(const Image &image, float x, float y,
                                 float scale, const XMFLOAT4 &color) {
    if (image.textureId == 0 || image.width <= 0.0f || image.height <= 0.0f) {
        return;
    }

    Sprite sprite{};
    sprite.textureId = image.textureId;
    sprite.position = {x, y};
    sprite.size = {image.width * scale, image.height * scale};
    sprite.color = color;
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
    DrawTextLine(text, centerX, y, scale, Color(1.0f, 1.0f, 1.0f, alpha));
}

void GameVictoryScene::DrawTextLine(const std::string &text, float centerX,
                                    float y, float scale,
                                    const XMFLOAT4 &color) {
    const float x = centerX - MeasureTextLine(text, scale) * 0.5f;
    DrawTextLineLeft(text, x, y, scale, color);
}

void GameVictoryScene::DrawTextLineLeft(const std::string &text, float x,
                                        float y, float scale, float alpha) {
    DrawTextLineLeft(text, x, y, scale, Color(1.0f, 1.0f, 1.0f, alpha));
}

void GameVictoryScene::DrawTextLineLeft(const std::string &text, float x,
                                        float y, float scale,
                                        const XMFLOAT4 &color) {
    for (char c : text) {
        if (c == ' ') {
            x += GetCharAdvance(c) * scale;
            continue;
        }
        const Image *image = FindCharImage(c);
        if (image == nullptr) {
            continue;
        }
        const float inkLeft =
            image->inkRight > image->inkLeft ? image->inkLeft : 0.0f;
        DrawImage(*image, x - inkLeft * scale, y, scale, color);
        x += GetCharAdvance(c) * scale;
    }
}

void GameVictoryScene::DrawTextLineLeftBaseline(const std::string &text,
                                                float x, float baselineY,
                                                float scale,
                                                const XMFLOAT4 &color) {
    for (char c : text) {
        if (c == ' ') {
            x += GetCharAdvance(c) * scale;
            continue;
        }
        const Image *image = FindCharImage(c);
        if (image == nullptr) {
            continue;
        }
        const float inkLeft =
            image->inkRight > image->inkLeft ? image->inkLeft : 0.0f;
        const float inkBottom =
            image->inkBottom > image->inkTop ? image->inkBottom : image->height;
        DrawImage(*image, x - inkLeft * scale,
                  baselineY - inkBottom * scale, scale, color);
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
    float cursorX = 0.0f;
    float width = 0.0f;
    for (char c : text) {
        if (c == ' ') {
            cursorX += GetCharAdvance(c);
            width = cursorX;
            continue;
        }
        const Image *image = FindCharImage(c);
        if (image != nullptr) {
            const float inkWidth =
                image->inkRight > image->inkLeft
                    ? image->inkRight - image->inkLeft
                    : image->width;
            width = cursorX + inkWidth;
            cursorX += GetCharAdvance(c);
        }
    }
    return (std::max)(0.0f, width * scale);
}

float GameVictoryScene::MeasureTextInkCenterOffset(const std::string &text,
                                                   float scale) const {
    bool hasInk = false;
    float top = 0.0f;
    float bottom = 0.0f;
    for (char c : text) {
        const Image *image = FindCharImage(c);
        if (image == nullptr) {
            continue;
        }
        const float inkTop =
            image->inkBottom > image->inkTop ? image->inkTop : 0.0f;
        const float inkBottom =
            image->inkBottom > image->inkTop ? image->inkBottom : image->height;
        if (!hasInk) {
            top = inkTop;
            bottom = inkBottom;
            hasInk = true;
        } else {
            top = (std::min)(top, inkTop);
            bottom = (std::max)(bottom, inkBottom);
        }
    }
    return hasInk ? (top + bottom) * 0.5f * scale : 0.0f;
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

std::string GameVictoryScene::FormatDifficulty(float difficulty) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1)
        << std::clamp(difficulty, 0.0f, 9.0f);
    return oss.str();
}

std::string GameVictoryScene::FormatScore(int score) const {
    return std::to_string((std::max)(0, score));
}
