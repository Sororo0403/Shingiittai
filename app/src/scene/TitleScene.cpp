#include "TitleScene.h"
#include "AppSceneServices.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "Lighting.h"
#include "Material.h"
#include "Model.h"
#include "ModelManager.h"
#include "PostProcessSystem.h"
#include "SceneManager.h"
#include "SoundManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WeaponSelectScene.h"
#include "WinApp.h"
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>
#include <vector>

using namespace DirectX;

namespace {
constexpr float kFadeDuration = 0.35f;
constexpr float kFrameIntroDuration = 1.12f;
constexpr float kMovieDuration = 58.0f;
constexpr float kTitleLogoDuration = 2.15f;
constexpr float kPi = 3.14159265f;

const std::string kBossAnimIdle = "Action";
const std::string kBossAnimMove = "Action.001";
const std::string kBossAnimSweep =
    "\xE6\xA8\xAA\xE8\x96\x99\xE3\x81\x8E\xE6\x89\x95\xE3\x81\x84";
const std::string kBossAnimSmash =
    "\xE7\xB8\xA6\xE6\x8C\xAF\xE3\x82\x8A\xE4\xB8\x8B\xE3\x82\x8D\xE3\x81\x97";
const std::string kBossAnimTeleport =
    "\xE3\x83\x86\xE3\x83\xAC\xE3\x83\x9D\xE3\x83\xBC\xE3\x83\x88";
const std::string kBossAnimPhaseChange =
    "\xE7\xAC\xAC\xE4\xBA\x8C\xE5\xBD\xA2\xE6\x85\x8B\xE7\xA7\xBB\xE8\xA1\x8C";

XMFLOAT4 MakeColor(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float Saturate(float t) { return std::clamp(t, 0.0f, 1.0f); }
float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }
float Smooth01(float t) { return SmoothStep(Saturate(t)); }

float PulseRange(float value, float enter, float enterDuration, float exit,
                 float exitDuration) {
    return Smooth01((value - enter) / enterDuration) *
           (1.0f - Smooth01((value - exit) / exitDuration));
}

XMFLOAT4 MakeQuat(float pitch, float yaw, float roll) {
    XMFLOAT4 q{};
    XMStoreFloat4(&q, XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
    return q;
}

XMFLOAT3 Lerp(const XMFLOAT3 &a, const XMFLOAT3 &b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t};
}

bool HasAnimation(const Model *model, const std::string &animationName) {
    return model != nullptr &&
           model->animations.find(animationName) != model->animations.end();
}

std::string PickFirstAnimation(const Model *model,
                               std::initializer_list<std::string> names) {
    for (const std::string &name : names) {
        if (HasAnimation(model, name)) {
            return name;
        }
    }
    if (model != nullptr && !model->animations.empty()) {
        return model->animations.begin()->first;
    }
    return {};
}

Material MovieMaterial(const XMFLOAT4 &color, float reflection = 0.05f,
                       float roughness = 0.78f) {
    Material material{};
    material.color = color;
    material.reflectionStrength = reflection;
    material.reflectionFresnelStrength = reflection * 0.35f;
    material.reflectionRoughness = roughness;
    return material;
}

uint32_t Hash2D(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return h ^ (h >> 16u);
}

uint32_t CreateMovieRustTexture(TextureManager *texture, uint32_t width,
                                uint32_t height, const XMFLOAT3 &baseColor,
                                const XMFLOAT3 &rustColor, uint32_t seed) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t coarse = Hash2D(x / 9u, y / 9u, seed);
            const uint32_t streak = Hash2D(x / 3u, y / 23u, seed + 41u);
            const uint32_t pitted = Hash2D(x / 2u, y / 2u, seed + 113u);
            const float rust =
                std::clamp(static_cast<float>(coarse & 255u) / 255.0f * 0.56f +
                               static_cast<float>(streak & 255u) / 255.0f * 0.34f +
                               static_cast<float>(pitted & 63u) / 255.0f,
                           0.0f, 1.0f);
            const float scratch =
                static_cast<float>(Hash2D(x, y / 5u, seed + 211u) & 31u) /
                255.0f;
            XMFLOAT3 color{
                std::clamp(baseColor.x + (rustColor.x - baseColor.x) * rust +
                               scratch,
                           0.0f, 1.0f),
                std::clamp(baseColor.y + (rustColor.y - baseColor.y) * rust +
                               scratch,
                           0.0f, 1.0f),
                std::clamp(baseColor.z + (rustColor.z - baseColor.z) * rust +
                               scratch,
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

void ApplyMovieRustMaterial(ModelManager *modelManager, uint32_t modelId,
                            uint32_t textureId, const XMFLOAT4 &tint,
                            float reflection, float roughness) {
    if (modelManager == nullptr || modelId == UINT32_MAX ||
        textureId == UINT32_MAX) {
        return;
    }

    Model *model = modelManager->GetModel(modelId);
    if (model == nullptr) {
        return;
    }

    model->textureId = textureId;
    for (ModelSubMesh &subMesh : model->subMeshes) {
        subMesh.textureId = textureId;
        Material material = modelManager->GetMaterial(subMesh.materialId);
        material.enableTexture = 1;
        material.baseColorTextureId = textureId;
        material.color = tint;
        material.reflectionStrength = reflection;
        material.reflectionFresnelStrength = reflection * 0.35f;
        material.reflectionRoughness = roughness;
        material.roughness = roughness;
        material.metallic = 0.18f;
        material.enableDissolve = 0.0f;
        modelManager->SetMaterial(subMesh.materialId, material);
    }
}
} // namespace

TitleScene::~TitleScene() { StopTitleBgm(); }

void TitleScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    movieTimer_ = 0.0f;
    titleLogoTimer_ = 0.0f;
    frameIntroTimer_ = 0.0f;
    fadeTimer_ = 0.0f;
    phase_ = Phase::Title;
    startRequested_ = false;
    titleBgmSoundId_ = 0;
    titleBgmVoice_ = SoundManager::kInvalidVoiceHandle;
    movieFeedbackBeat_ = -1;
    movieCounterSoundId_ = SoundManager::kInvalidSoundId;
    movieDamageSoundId_ = SoundManager::kInvalidSoundId;
    movieHitSoundId_ = SoundManager::kInvalidSoundId;
    playerModelId_ = UINT32_MAX;
    swordModelId_ = UINT32_MAX;
    enemyModelId_ = UINT32_MAX;
    movieFloorModelId_ = UINT32_MAX;
    movieRingModelId_ = UINT32_MAX;
    moviePillarModelId_ = UINT32_MAX;
    movieCharacterRustTextureId_ = UINT32_MAX;
    movieEnemyRustTextureId_ = UINT32_MAX;
    movieArenaRustTextureId_ = UINT32_MAX;

    if (AppSceneServices::HasHandTrackingStart()) {
        AppSceneServices::RequestHandTrackingStart();
    }

    logoImage_ = LoadTitleImage(L"app/resources/ui/title/gamelogo.png");
    pressAnyButtonImage_ =
        LoadTitleImage(L"app/resources/ui/title/press_any_button.png");
    titleDemoScene_ = std::make_unique<GameScene>(true);
    titleDemoScene_->Initialize(ctx);

    if (ctx_->systems.sound != nullptr) {
        titleBgmSoundId_ = ctx_->systems.sound->LoadOrCreateSilent(
            L"app/resources/audio/bgm/maou_game_battle20.mp3");
        titleBgmVoice_ = ctx_->systems.sound->Play(titleBgmSoundId_, 0.62f, true);
    }
}

void TitleScene::Update() {
    sceneTime_ += ctx_->frame.deltaTime;
    frameIntroTimer_ =
        (std::min)(frameIntroTimer_ + ctx_->frame.deltaTime,
                   kFrameIntroDuration);

    if (startRequested_) {
        fadeTimer_ += ctx_->frame.deltaTime;
        if (fadeTimer_ >= kFadeDuration) {
            if (ctx_->rendering.postProcessSystem != nullptr) {
                ctx_->rendering.postProcessSystem->SetProfile(
                    PostProcessProfile{});
            }
            StopTitleBgm();
            sceneManager_->ChangeScene(std::make_unique<WeaponSelectScene>());
        }
        UpdateTitleBgmVolume();
        return;
    }

    const Input &input = *ctx_->systems.input;
    switch (phase_) {
    case Phase::Movie:
        movieTimer_ += ctx_->frame.deltaTime;
        UpdateMovieScene(ctx_->frame.deltaTime);
        if (IsSkipTriggered(input)) {
            CompleteTitleIntro();
        } else if (movieTimer_ >= kMovieDuration) {
            BeginTitleLogo();
        }
        break;
    case Phase::TitleLogo:
        if (IsSkipTriggered(input)) {
            CompleteTitleIntro();
            break;
        }
        titleLogoTimer_ += ctx_->frame.deltaTime;
        if (titleLogoTimer_ >= kTitleLogoDuration) {
            CompleteTitleIntro();
        }
        break;
    case Phase::Title:
        if (titleDemoScene_) {
            titleDemoScene_->Update();
        }
        if (IsAnyButtonTriggered(input)) {
            startRequested_ = true;
            fadeTimer_ = 0.0f;
        }
        break;
    }
    UpdateTitleBgmVolume();
}

void TitleScene::Draw() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    if (phase_ == Phase::Title) {
        if (titleDemoScene_) {
            titleDemoScene_->Draw();
        }
        return;
    }

    if (phase_ == Phase::Movie) {
        DrawMovieScene();
        return;
    }

    ctx_->rendering.sprite->PreDraw();
    if (phase_ == Phase::TitleLogo) {
        DrawRect(0.0f, 0.0f, w, h, MakeColor(0.0f, 0.0f, 0.0f, 1.0f));
    } else {
        DrawTitleBackground(w, h);
    }
    ctx_->rendering.sprite->PostDraw();
}

void TitleScene::DrawTransparent() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    if (phase_ == Phase::Title && titleDemoScene_) {
        titleDemoScene_->DrawTransparent();
    }

    ctx_->rendering.sprite->PreDraw();

    if (phase_ == Phase::Movie) {
        DrawMovieOverlay(w, h);
        ctx_->rendering.sprite->PostDraw();
        return;
    }

    if (phase_ == Phase::TitleLogo) {
        const float t = Smooth01(titleLogoTimer_ / kTitleLogoDuration);
        DrawTitleLogo(w, h, (std::min)(1.0f, t * 1.6f),
                      1.0f + 0.05f * (1.0f - t));
        ctx_->rendering.sprite->PostDraw();
        return;
    }

    const float frameT =
        std::clamp(frameIntroTimer_ / kFrameIntroDuration, 0.0f, 1.0f);
    const float screenIdle = Smooth01((frameT - 0.88f) / 0.12f);
    const float screenBreath =
        screenIdle * (0.5f + 0.5f * std::sinf(sceneTime_ * 0.9f));
    const float logoScale =
        std::clamp(w * 0.50f / logoImage_.width, 0.58f, 1.0f);
    const float logoY = (h - logoImage_.height * logoScale) * 0.5f - 10.0f;
    const float logoHeight = logoImage_.height * logoScale;
    DrawRect(0.0f, 0.0f, w, h,
             MakeColor(0.0f, 0.0f, 0.0f, 0.46f + 0.025f * screenBreath));
    DrawRect(w * 0.18f, 0.0f, w * 0.64f, h,
             MakeColor(0.0f, 0.0f, 0.0f, 0.035f * screenIdle));
    DrawRect(0.0f, 0.0f, w * 0.075f, h,
             MakeColor(0.0f, 0.0f, 0.0f, 0.080f * screenIdle));
    DrawRect(w * 0.925f, 0.0f, w * 0.075f, h,
             MakeColor(0.0f, 0.0f, 0.0f, 0.080f * screenIdle));
    const float focusY =
        logoY - 22.0f + std::sinf(sceneTime_ * 0.54f) * 1.2f * screenIdle;
    DrawRect(0.0f, focusY, w, logoHeight + 62.0f,
             MakeColor(0.0f, 0.0f, 0.0f, 0.038f * screenIdle));
    DrawRect(0.0f,
             logoY + logoHeight * 0.52f +
                 std::sinf(sceneTime_ * 0.48f) * 10.0f * screenIdle,
             w, 1.0f, MakeColor(0.0f, 0.0f, 0.0f, 0.070f * screenIdle));
    DrawStartupFrame(w, h);

    const float titleReveal = Smooth01((frameT - 0.78f) / 0.18f);
    if (titleReveal > 0.0f) {
        const float titleIdle = Smooth01((frameT - 0.96f) / 0.04f);
        const float titleY = -14.0f * (1.0f - titleReveal) - 10.0f +
                             titleIdle * std::sinf(sceneTime_ * 0.72f) * 1.35f;
        const float titleScale =
            1.0f + titleIdle * std::sinf(sceneTime_ * 0.82f) * 0.0045f;
        DrawTitleLogo(w, h, titleReveal, titleScale, titleY);
    }

    const float pressScale =
        std::clamp(w * 0.28f / pressAnyButtonImage_.width, 0.48f, 0.82f);
    const float pressX =
        (w - pressAnyButtonImage_.width * pressScale) * 0.5f;
    const float pressReveal = Smooth01((frameT - 0.94f) / 0.08f);
    const float pressBeat = std::fmod(sceneTime_ * 0.52f, 1.0f);
    const float pressHold = 1.0f - PulseRange(pressBeat, 0.78f, 0.05f,
                                              0.93f, 0.07f) *
                                        0.28f;
    const float pressY =
        logoY + logoImage_.height * logoScale + 26.0f +
        pressReveal * std::sinf(sceneTime_ * 1.15f) * 1.4f;
    const float pressAlpha =
        pressReveal *
        (0.48f + 0.18f * (0.5f + 0.5f * std::sinf(sceneTime_ * 3.0f))) *
        pressHold;
    DrawImage(pressAnyButtonImage_, pressX, pressY, pressAlpha, pressScale);

    if (startRequested_) {
        const float fadeT =
            std::clamp(fadeTimer_ / kFadeDuration, 0.0f, 1.0f);
        DrawRect(0.0f, 0.0f, w, h,
                 MakeColor(0.0f, 0.0f, 0.0f, SmoothStep(fadeT)));
    }

    ctx_->rendering.sprite->PostDraw();
}

void TitleScene::DrawStartupFrame(float screenWidth, float screenHeight) {
    const float t = Smooth01(frameIntroTimer_ / kFrameIntroDuration);
    const float rawT = std::clamp(frameIntroTimer_ / kFrameIntroDuration,
                                  0.0f, 1.0f);
    const float overshoot =
        std::sinf(std::clamp(t, 0.0f, 1.0f) * kPi) * 0.045f;
    const float barHeight = screenHeight * (0.124f + overshoot);
    const float topY = -barHeight * (1.0f - t);
    const float bottomY = screenHeight - barHeight * t;

    DrawRect(0.0f, topY, screenWidth, barHeight,
             MakeColor(0.0f, 0.0f, 0.0f, 0.96f));
    DrawRect(0.0f, bottomY, screenWidth, barHeight,
             MakeColor(0.0f, 0.0f, 0.0f, 0.96f));
    DrawRect(0.0f, topY + barHeight * 0.62f, screenWidth,
             barHeight * 0.38f,
             MakeColor(0.020f, 0.016f, 0.012f, 0.44f));
    DrawRect(0.0f, bottomY, screenWidth, barHeight * 0.38f,
             MakeColor(0.020f, 0.016f, 0.012f, 0.44f));

    const float idle = Smooth01((rawT - 0.86f) / 0.14f);
    if (idle > 0.0f) {
        const float sheenWidth = screenWidth * 0.30f;
        const float sheenTravel = std::fmod(sceneTime_ * 0.105f, 1.0f);
        const float sheenX =
            -sheenWidth + (screenWidth + sheenWidth * 2.0f) * sheenTravel;
        const float sheenAlpha =
            idle * (0.038f + 0.012f * std::sinf(sceneTime_ * 1.1f));
        DrawRect(sheenX, topY + barHeight * 0.18f, sheenWidth,
                 barHeight * 0.22f,
                 MakeColor(0.095f, 0.080f, 0.055f, sheenAlpha));
        DrawRect(screenWidth - sheenX - sheenWidth,
                 bottomY + barHeight * 0.60f, sheenWidth, barHeight * 0.22f,
                 MakeColor(0.095f, 0.080f, 0.055f, sheenAlpha));

        const float undertoneWidth = screenWidth * 0.42f;
        const float undertoneTravel = std::fmod(sceneTime_ * 0.062f + 0.31f, 1.0f);
        const float undertoneX =
            -undertoneWidth +
            (screenWidth + undertoneWidth * 2.0f) * undertoneTravel;
        DrawRect(undertoneX, topY + barHeight * 0.69f, undertoneWidth,
                 barHeight * 0.12f,
                 MakeColor(0.0f, 0.0f, 0.0f, idle * 0.12f));
        DrawRect(screenWidth - undertoneX - undertoneWidth,
                 bottomY + barHeight * 0.19f, undertoneWidth,
                 barHeight * 0.12f,
                 MakeColor(0.0f, 0.0f, 0.0f, idle * 0.12f));
    }

    const float edgeAlpha =
        std::clamp((1.0f - std::fabs(t - 0.62f) / 0.38f), 0.22f, 1.0f);
    const float lineBreath = 0.88f + 0.12f * std::sinf(sceneTime_ * 1.35f);
    const float lineYTop = topY + barHeight - 3.0f;
    const float lineYBottom = bottomY;
    DrawRect(0.0f, lineYTop, screenWidth, 2.0f,
             MakeColor(0.92f, 0.68f, 0.28f,
                       0.56f * edgeAlpha * lineBreath));
    DrawRect(0.0f, lineYBottom, screenWidth, 2.0f,
             MakeColor(0.92f, 0.68f, 0.28f,
                       0.56f * edgeAlpha * lineBreath));
    DrawRect(0.0f, lineYTop + 4.0f, screenWidth, 1.0f,
             MakeColor(1.0f, 0.92f, 0.60f,
                       0.24f * edgeAlpha * lineBreath));
    DrawRect(0.0f, lineYBottom - 4.0f, screenWidth, 1.0f,
             MakeColor(1.0f, 0.92f, 0.60f,
                       0.24f * edgeAlpha * lineBreath));
    if (idle > 0.0f) {
        const float innerLineAlpha =
            idle * edgeAlpha *
            (0.060f + 0.014f * std::sinf(sceneTime_ * 1.6f));
        DrawRect(0.0f, lineYTop - 7.0f, screenWidth, 1.0f,
                 MakeColor(0.88f, 0.62f, 0.24f, innerLineAlpha));
        DrawRect(0.0f, lineYBottom + 8.0f, screenWidth, 1.0f,
                 MakeColor(0.88f, 0.62f, 0.24f, innerLineAlpha));
    }

    if (t < 1.0f) {
        const float sweepWidth = screenWidth * 0.34f;
        const float sweepX = -sweepWidth + (screenWidth + sweepWidth * 2.0f) * t;
        const float sweepAlpha = std::sinf(t * kPi) * 0.84f;
        DrawRect(sweepX, lineYTop - 1.0f, sweepWidth, 4.0f,
                 MakeColor(1.0f, 0.86f, 0.42f, sweepAlpha));
        DrawRect(screenWidth - sweepX - sweepWidth, lineYBottom - 1.0f,
                 sweepWidth, 4.0f,
                 MakeColor(1.0f, 0.86f, 0.42f, sweepAlpha));
        DrawRect(sweepX - sweepWidth * 0.38f, lineYTop + 5.0f,
                 sweepWidth * 0.62f, 1.0f,
                 MakeColor(1.0f, 0.96f, 0.72f, sweepAlpha * 0.54f));
        DrawRect(screenWidth - sweepX - sweepWidth * 0.24f,
                 lineYBottom - 6.0f, sweepWidth * 0.62f, 1.0f,
                 MakeColor(1.0f, 0.96f, 0.72f, sweepAlpha * 0.54f));
    }

    if (idle > 0.0f) {
        const float glintWidth = screenWidth * 0.18f;
        const float glintTravel =
            std::fmod(sceneTime_ * 0.18f, 1.0f);
        const float glintX =
            -glintWidth + (screenWidth + glintWidth * 2.0f) * glintTravel;
        const float glintAlpha =
            idle * edgeAlpha *
            (0.10f + 0.025f * std::sinf(sceneTime_ * 1.9f));
        DrawRect(glintX, lineYTop - 1.0f, glintWidth, 3.0f,
                 MakeColor(1.0f, 0.92f, 0.62f, glintAlpha));
        DrawRect(screenWidth - glintX - glintWidth, lineYBottom, glintWidth,
                 3.0f, MakeColor(1.0f, 0.92f, 0.62f, glintAlpha));

        const float fineWidth = screenWidth * 0.10f;
        const float fineTravel = std::fmod(sceneTime_ * 0.27f + 0.36f, 1.0f);
        const float fineX =
            -fineWidth + (screenWidth + fineWidth * 2.0f) * fineTravel;
        const float fineAlpha =
            idle * edgeAlpha *
            (0.070f + 0.018f * std::sinf(sceneTime_ * 2.4f));
        DrawRect(fineX, lineYTop + 4.0f, fineWidth, 1.0f,
                 MakeColor(1.0f, 0.98f, 0.76f, fineAlpha));
        DrawRect(screenWidth - fineX - fineWidth, lineYBottom - 4.0f,
                 fineWidth, 1.0f,
                 MakeColor(1.0f, 0.98f, 0.76f, fineAlpha));

        const float shortWidth = screenWidth * 0.060f;
        const float shortTravel = std::fmod(sceneTime_ * 0.41f + 0.72f, 1.0f);
        const float shortX =
            -shortWidth + (screenWidth + shortWidth * 2.0f) * shortTravel;
        const float shortAlpha =
            idle * edgeAlpha *
            (0.090f + 0.020f * std::sinf(sceneTime_ * 3.1f));
        DrawRect(shortX, lineYTop - 1.0f, shortWidth, 2.0f,
                 MakeColor(1.0f, 0.96f, 0.70f, shortAlpha));
        DrawRect(screenWidth - shortX - shortWidth, lineYBottom + 1.0f,
                 shortWidth, 2.0f,
                 MakeColor(1.0f, 0.96f, 0.70f, shortAlpha));

        const float darkWidth = screenWidth * 0.22f;
        const float darkTravel = std::fmod(sceneTime_ * 0.12f + 0.58f, 1.0f);
        const float darkX =
            -darkWidth + (screenWidth + darkWidth * 2.0f) * darkTravel;
        DrawRect(darkX, lineYTop - 5.0f, darkWidth, 1.0f,
                 MakeColor(0.0f, 0.0f, 0.0f, idle * 0.13f));
        DrawRect(screenWidth - darkX - darkWidth, lineYBottom + 6.0f,
                 darkWidth, 1.0f, MakeColor(0.0f, 0.0f, 0.0f, idle * 0.13f));
    }

}

void TitleScene::BeginTitleLogo() {
    movieFeedback_.Reset();
    phase_ = Phase::TitleLogo;
    titleLogoTimer_ = 0.0f;
    movieTimer_ = kMovieDuration;
}

void TitleScene::CompleteTitleIntro() {
    movieFeedback_.Reset();
    phase_ = Phase::Title;
    titleLogoTimer_ = kTitleLogoDuration;
}

void TitleScene::StopTitleBgm() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr ||
        titleBgmVoice_ == SoundManager::kInvalidVoiceHandle) {
        return;
    }
    ctx_->systems.sound->Stop(titleBgmVoice_);
    titleBgmVoice_ = SoundManager::kInvalidVoiceHandle;
}

void TitleScene::UpdateTitleBgmVolume() {
    if (ctx_ == nullptr || ctx_->systems.sound == nullptr ||
        titleBgmVoice_ == SoundManager::kInvalidVoiceHandle) {
        return;
    }
    const float fadeOut =
        startRequested_ ? 1.0f - Smooth01(fadeTimer_ / kFadeDuration) : 1.0f;
    const float movieLift = phase_ == Phase::Movie ? 0.68f : 0.54f;
    ctx_->systems.sound->SetVoiceVolume(titleBgmVoice_, movieLift * fadeOut);
}

void TitleScene::TriggerMovieFeedback(int beatIndex,
                                      CombatFeedbackEventType type,
                                      float power) {
    if (movieFeedbackBeat_ >= beatIndex) {
        return;
    }
    movieFeedbackBeat_ = beatIndex;

    CombatFeedbackEvent event{};
    event.type = type;
    event.position = {0.0f, 1.18f, 0.92f};
    event.direction = type == CombatFeedbackEventType::PlayerDamaged
                          ? XMFLOAT3{-0.20f, 0.0f, -1.0f}
                          : XMFLOAT3{0.18f, 0.0f, 1.0f};
    event.power = power;
    movieFeedback_.PushEvent(event);

    if (ctx_ == nullptr || ctx_->systems.sound == nullptr) {
        return;
    }

    switch (type) {
    case CombatFeedbackEventType::CounterSuccess:
        if (movieCounterSoundId_ != SoundManager::kInvalidSoundId) {
            ctx_->systems.sound->Play(movieCounterSoundId_, 0.86f);
        }
        break;
    case CombatFeedbackEventType::PlayerDamaged:
        if (movieDamageSoundId_ != SoundManager::kInvalidSoundId) {
            ctx_->systems.sound->Play(movieDamageSoundId_, 0.72f);
        }
        break;
    case CombatFeedbackEventType::PlayerSlashHit:
        if (movieHitSoundId_ != SoundManager::kInvalidSoundId) {
            ctx_->systems.sound->Play(movieHitSoundId_, 0.72f);
        }
        break;
    }
}

void TitleScene::InitializeMovieScene() {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr ||
        ctx_->rendering.texture == nullptr || ctx_->systems.winApp == nullptr) {
        return;
    }

    ModelManager *model = ctx_->rendering.model;
    TextureManager *texture = ctx_->rendering.texture;
    movieCharacterRustTextureId_ =
        CreateMovieRustTexture(texture, 512, 512, {0.13f, 0.18f, 0.29f},
                               {0.66f, 0.28f, 0.10f}, 0x3145u);
    movieEnemyRustTextureId_ =
        CreateMovieRustTexture(texture, 512, 512, {0.18f, 0.16f, 0.13f},
                               {0.74f, 0.24f, 0.07f}, 0xB055u);
    movieArenaRustTextureId_ =
        CreateMovieRustTexture(texture, 768, 768, {0.07f, 0.08f, 0.08f},
                               {0.44f, 0.20f, 0.09f}, 0xA9E1u);

    playerModelId_ = model->Load(L"app/resources/models/player/player.glb");
    swordModelId_ = model->Load(L"app/resources/models/player/sword.glb");
    enemyModelId_ = model->Load(L"app/resources/models/boss/boss.gltf");

    movieFloorModelId_ = model->CreatePlane(
        movieArenaRustTextureId_,
        MovieMaterial({0.08f, 0.09f, 0.09f, 1.0f}, 0.035f, 0.88f));
    movieRingModelId_ = model->CreateRing(
        movieArenaRustTextureId_,
        MovieMaterial({0.95f, 0.48f, 0.16f, 0.78f}, 0.16f, 0.42f),
        96, 4.35f, 3.85f);
    moviePillarModelId_ = model->CreateCylinder(
        movieArenaRustTextureId_,
        MovieMaterial({0.16f, 0.18f, 0.18f, 1.0f}, 0.055f, 0.78f),
        20, 0.22f, 0.34f, 4.2f);

    ApplyMovieRustMaterial(model, playerModelId_, movieCharacterRustTextureId_,
                           {0.42f, 0.50f, 0.72f, 1.0f}, 0.10f, 0.72f);
    ApplyMovieRustMaterial(model, swordModelId_, movieCharacterRustTextureId_,
                           {0.72f, 0.82f, 0.95f, 1.0f}, 0.34f, 0.48f);
    ApplyMovieRustMaterial(model, enemyModelId_, movieEnemyRustTextureId_,
                           {0.46f, 0.40f, 0.32f, 1.0f}, 0.055f, 0.90f);

    const int width = ctx_->systems.winApp->GetWidth();
    const int height = ctx_->systems.winApp->GetHeight();
    const float aspect = height > 0 ? static_cast<float>(width) /
                                          static_cast<float>(height)
                                    : 16.0f / 9.0f;
    movieCamera_.Initialize(aspect);
    movieCamera_.SetPerspectiveFovDeg(58.0f);
    movieCamera_.SetClipRange(0.05f, 260.0f);

    if (Model *playerModel = model->GetModel(playerModelId_)) {
        moviePlayerAnimation_ =
            PickFirstAnimation(playerModel, std::initializer_list<std::string>{});
        if (!moviePlayerAnimation_.empty()) {
            model->PlayAnimation(playerModelId_, moviePlayerAnimation_, true);
        }
    }

    if (Model *enemyModel = model->GetModel(enemyModelId_)) {
        movieEnemyAnimation_ =
            PickFirstAnimation(enemyModel, {kBossAnimTeleport,
                                            kBossAnimPhaseChange,
                                            kBossAnimIdle});
        movieEnemyAnimationLoop_ = false;
        if (!movieEnemyAnimation_.empty()) {
            model->PlayAnimation(enemyModelId_, movieEnemyAnimation_,
                                 movieEnemyAnimationLoop_);
        }
    }

    movieFeedback_.Initialize(ctx_->rendering.postProcessSystem);
    UpdateMovieScene(0.0f);
}

void TitleScene::UpdateMovieScene(float deltaTime) {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr ||
        ctx_->systems.winApp == nullptr) {
        return;
    }

    const int width = ctx_->systems.winApp->GetWidth();
    const int height = ctx_->systems.winApp->GetHeight();
    if (width > 0 && height > 0) {
        movieCamera_.SetAspect(static_cast<float>(width) /
                               static_cast<float>(height));
    }

    const bool titleDemo = phase_ == Phase::Title;
    const float demoDuration = titleDemo ? 18.0f : kMovieDuration;
    const float demoTimer =
        titleDemo ? std::fmod(movieTimer_, demoDuration) : movieTimer_;
    const float t = Saturate(demoTimer / demoDuration);
    const float clashBeat = std::fmod(movieTimer_ * 1.85f, 1.0f);
    const int clashIndex = static_cast<int>(std::floor(movieTimer_ * 1.85f));
    const float sparkPulse = PulseRange(clashBeat, 0.08f, 0.035f, 0.22f, 0.10f);
    const float pressure =
        0.5f + 0.5f * std::sinf(movieTimer_ * 2.7f + std::sinf(movieTimer_ * 0.8f));
    const float releasePulse =
        titleDemo ? 0.0f
                  : Smooth01((t - 0.86f) / 0.08f) *
                        (1.0f - Smooth01((t - 0.96f) / 0.04f));
    const float cameraHit =
        titleDemo ? sparkPulse * 0.30f : sparkPulse + pressure * 0.16f + releasePulse;

    if (!titleDemo && clashBeat >= 0.10f && t < 0.88f) {
        TriggerMovieFeedback(clashIndex, CombatFeedbackEventType::PlayerSlashHit,
                             1.25f + pressure * 0.65f);
    } else if (!titleDemo && t >= 0.90f) {
        TriggerMovieFeedback(99, CombatFeedbackEventType::CounterSuccess, 4.0f);
    }
    movieFeedback_.Update(deltaTime, sceneTime_);

    const float orbit = sceneTime_ * (titleDemo ? 0.12f : 0.18f) + Smooth01(t) * 1.15f;
    const float push = titleDemo ? 0.0f : Smooth01((t - 0.16f) / 0.40f);
    XMFLOAT3 cameraWide{
        titleDemo ? (std::sinf(orbit) * 5.4f) : (std::sinf(orbit) * 4.8f),
        titleDemo ? 2.60f : 2.08f,
        titleDemo ? (-5.85f + std::cos(orbit) * 1.20f)
                  : (-4.45f + std::cos(orbit) * 1.00f)};
    XMFLOAT3 cameraDuel{-1.72f + pressure * 0.14f, 1.36f + sparkPulse * 0.08f,
                        -2.58f + pressure * 0.20f};
    XMFLOAT3 cameraClose{1.32f + std::sinf(sceneTime_ * 0.9f) * 0.16f,
                         1.28f + releasePulse * 0.50f,
                         -2.05f + releasePulse * 0.65f};
    XMFLOAT3 cameraPos =
        titleDemo ? cameraWide
                  : Lerp(cameraWide, cameraDuel, Smooth01((t - 0.06f) / 0.24f));
    cameraPos = Lerp(cameraPos, cameraClose, push);
    cameraPos.x += std::sinf(sceneTime_ * 31.0f) * 0.055f * cameraHit;
    cameraPos.y += std::sinf(sceneTime_ * 43.0f) * 0.040f * cameraHit;

    const XMFLOAT3 lookStart{0.0f, titleDemo ? 1.32f : 1.18f,
                             titleDemo ? 0.75f : 0.58f};
    const XMFLOAT3 lookEnd{0.0f, 1.42f + releasePulse * 0.42f, 1.22f};
    XMFLOAT3 lookAt = Lerp(lookStart, lookEnd, push);
    movieFeedback_.ApplyCameraImpulse(cameraPos, lookAt, sceneTime_);
    movieCamera_.SetPerspectiveFovDeg(60.0f - 8.0f * push +
                                      4.0f * cameraHit +
                                      movieFeedback_.GetFovKickDeg());
    movieCamera_.SetPosition(cameraPos);
    AppLookAt(movieCamera_, lookAt);

    ModelManager *model = ctx_->rendering.model;
    if (playerModelId_ != UINT32_MAX) {
        model->UpdateAnimation(playerModelId_, deltaTime * 0.16f);
    }

    Model *enemyModel = model->GetModel(enemyModelId_);
    if (enemyModel != nullptr && !enemyModel->animations.empty()) {
        const bool nextLoop = true;
        std::string nextAnimation =
            PickFirstAnimation(enemyModel, {kBossAnimIdle, kBossAnimMove});

        if (!nextAnimation.empty() &&
            (movieEnemyAnimation_ != nextAnimation ||
             movieEnemyAnimationLoop_ != nextLoop)) {
            model->PlayAnimation(enemyModelId_, nextAnimation, nextLoop);
            movieEnemyAnimation_ = nextAnimation;
            movieEnemyAnimationLoop_ = nextLoop;
        }
        model->UpdateAnimation(enemyModelId_, deltaTime * 0.18f);
    }
}

void TitleScene::DrawMovieScene() {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr ||
        playerModelId_ == UINT32_MAX || swordModelId_ == UINT32_MAX ||
        enemyModelId_ == UINT32_MAX || movieFloorModelId_ == UINT32_MAX ||
        movieRingModelId_ == UINT32_MAX || moviePillarModelId_ == UINT32_MAX) {
        return;
    }

    ModelManager *model = ctx_->rendering.model;
    model->PrepareSkinning({playerModelId_, enemyModelId_});

    SceneLighting lighting{};
    lighting.keyLightDirection = {-0.42f, -1.0f, 0.30f};
    lighting.keyLightColor = {1.25f, 0.88f, 0.62f, 1.0f};
    lighting.fillLightColor = {0.20f, 0.34f, 0.50f, 0.44f};
    lighting.ambientColor = {0.10f, 0.12f, 0.14f, 1.0f};
    lighting.pointLights[0] = {{0.0f, 2.3f, 1.4f, 9.0f},
                               {1.0f, 0.44f, 0.14f, 2.2f}};
    lighting.pointLights[1] = {{-2.6f, 1.4f, -1.4f, 7.0f},
                               {0.18f, 0.52f, 1.0f, 0.95f}};
    model->SetSceneLighting(lighting);
    model->PreDraw();

    Transform floor{};
    floor.position = {0.0f, -0.05f, 1.0f};
    floor.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    floor.scale = {42.0f, 42.0f, 1.0f};
    model->Draw(movieFloorModelId_, floor, movieCamera_);

    Transform ring{};
    ring.position = {0.0f, 0.015f, 1.4f};
    ring.rotation = MakeQuat(-kPi * 0.5f, sceneTime_ * 0.05f, 0.0f);
    ring.scale = {1.0f, 1.0f, 1.0f};
    model->Draw(movieRingModelId_, ring, movieCamera_);

    for (int i = 0; i < 8; ++i) {
        const float a = static_cast<float>(i) * kPi * 0.25f + sceneTime_ * 0.03f;
        Transform pillar{};
        pillar.position = {std::sinf(a) * 7.2f, 1.88f,
                           1.4f + std::cosf(a) * 7.2f};
        pillar.rotation = MakeQuat(0.0f, a, 0.0f);
        pillar.scale = {0.75f, 1.0f, 0.75f};
        model->Draw(moviePillarModelId_, pillar, movieCamera_);
    }

    const bool titleDemo = phase_ == Phase::Title;
    const float demoDuration = titleDemo ? 18.0f : kMovieDuration;
    const float demoTimer =
        titleDemo ? std::fmod(movieTimer_, demoDuration) : movieTimer_;
    const float t = Saturate(demoTimer / demoDuration);
    const float clashBeat = std::fmod(movieTimer_ * 1.85f, 1.0f);
    const float sparkPulse = PulseRange(clashBeat, 0.08f, 0.035f, 0.22f, 0.10f);
    const float pressure =
        0.5f + 0.5f * std::sinf(movieTimer_ * 2.7f + std::sinf(movieTimer_ * 0.8f));
    const float release = titleDemo ? 0.0f : Smooth01((t - 0.86f) / 0.08f);
    const float finalHold = release * (1.0f - Smooth01((t - 0.97f) / 0.035f));
    const float actionCycle = std::fmod(movieTimer_ * 0.28f, 1.0f);
    const float enemyAdvance =
        titleDemo ? PulseRange(actionCycle, 0.08f, 0.18f, 0.42f, 0.16f) : 0.0f;
    const float playerAdvance =
        titleDemo ? PulseRange(actionCycle, 0.48f, 0.14f, 0.78f, 0.18f) : 0.0f;
    const float clash = titleDemo ? (0.18f + sparkPulse * 0.35f)
                                  : (0.72f + pressure * 0.24f + sparkPulse * 0.20f);
    const float grind = std::sinf(movieTimer_ * 10.5f) * 0.035f;
    const float lane = std::sinf(movieTimer_ * 0.9f) * 0.10f;

    Transform enemy{};
    if (titleDemo) {
        enemy.position = {0.42f + lane * 0.45f, 0.0f,
                          2.45f - enemyAdvance * 0.86f + playerAdvance * 0.28f};
        enemy.rotation =
            MakeQuat(0.0f,
                     kPi - 0.16f + std::sinf(sceneTime_ * 0.9f) * 0.08f -
                         enemyAdvance * 0.18f,
                     enemyAdvance * 0.04f);
    } else {
        enemy.position = {0.34f + lane * 0.35f - pressure * 0.10f, 0.0f,
                          1.92f - pressure * 0.20f + finalHold * 0.56f};
        enemy.rotation = MakeQuat(0.0f,
                                  kPi + std::sinf(sceneTime_ * 1.4f) * 0.05f -
                                      pressure * 0.08f,
                                  pressure * 0.035f);
    }
    enemy.scale = {1.05f + finalHold * 0.20f, 1.05f + finalHold * 0.08f,
                   1.05f + finalHold * 0.20f};

    ModelDrawEffect enemyGlow{};
    enemyGlow.enabled = true;
    enemyGlow.additiveBlend = true;
    enemyGlow.disableCulling = true;
    enemyGlow.color = {1.0f, 0.30f, 0.06f,
                       0.14f + 0.18f * pressure + 0.44f * finalHold};
    enemyGlow.intensity = 0.16f + 0.28f * pressure + 0.62f * finalHold;
    enemyGlow.fresnelPower = 1.1f;
    enemyGlow.time = sceneTime_;
    model->SetDrawEffect(enemyGlow);
    if (sparkPulse > 0.04f || finalHold > 0.02f) {
        Transform enemyAfter = enemy;
        enemyAfter.position.z += 0.04f + pressure * 0.06f;
        enemyAfter.position.x += grind;
        enemyAfter.scale.x *= 1.025f + sparkPulse * 0.035f;
        enemyAfter.scale.z *= 1.025f + sparkPulse * 0.035f;
        ModelDrawEffect afterGlow = enemyGlow;
        afterGlow.color.w *= 0.24f;
        afterGlow.intensity *= 0.44f;
        model->SetDrawEffect(afterGlow);
        model->Draw(enemyModelId_, enemyAfter, movieCamera_);
        model->SetDrawEffect(enemyGlow);
    }
    model->Draw(enemyModelId_, enemy, movieCamera_);
    model->ClearDrawEffect();

    Transform player{};
    if (titleDemo) {
        player.position = {-0.72f - lane * 0.28f + playerAdvance * 0.36f, 0.0f,
                           -1.08f + playerAdvance * 0.92f -
                               enemyAdvance * 0.18f};
        player.rotation =
            MakeQuat(playerAdvance * 0.04f,
                     0.18f + playerAdvance * 0.10f - enemyAdvance * 0.04f,
                     -playerAdvance * 0.06f);
    } else {
        player.position = {-0.86f - lane * 0.30f + pressure * 0.12f, 0.0f,
                           -0.78f + pressure * 0.24f - finalHold * 0.10f};
        player.rotation = MakeQuat(-pressure * 0.035f,
                                   0.16f + pressure * 0.08f,
                                   -pressure * 0.055f);
    }
    player.scale = {1.45f, 1.45f, 1.45f};
    if (sparkPulse > 0.05f) {
        Transform playerAfter = player;
        playerAfter.position.z -= 0.10f + pressure * 0.08f;
        playerAfter.position.x -= grind;
        ModelDrawEffect playerTrail{};
        playerTrail.enabled = true;
        playerTrail.additiveBlend = true;
        playerTrail.disableCulling = true;
        playerTrail.color = {0.38f, 0.64f, 1.0f, 0.12f * sparkPulse};
        playerTrail.intensity = 0.28f * sparkPulse;
        playerTrail.fresnelPower = 1.0f;
        model->SetDrawEffect(playerTrail);
        model->Draw(playerModelId_, playerAfter, movieCamera_);
        model->ClearDrawEffect();
    }
    model->Draw(playerModelId_, player, movieCamera_);

    const float bladeVibration =
        std::sinf(movieTimer_ * 38.0f) * 0.018f * (0.55f + pressure);
    const XMFLOAT3 clashPoint{
        titleDemo ? (0.04f + lane * 0.20f + grind) : (0.03f + grind),
        titleDemo ? (1.10f + sparkPulse * 0.08f)
                  : (1.18f + sparkPulse * 0.08f),
        titleDemo ? (0.35f + enemyAdvance * 0.18f + playerAdvance * 0.12f)
                  : (0.62f + pressure * 0.08f)};

    Transform enemyBlade{};
    enemyBlade.position = {clashPoint.x + 0.22f - pressure * 0.08f,
                           clashPoint.y + bladeVibration,
                           clashPoint.z + (titleDemo ? 0.18f : 0.08f)};
    enemyBlade.rotation =
        titleDemo ? MakeQuat(-0.78f - enemyAdvance * 0.54f,
                             -0.28f + grind,
                             -0.86f + enemyAdvance * 0.62f)
                  : MakeQuat(-1.18f - pressure * 0.12f,
                             -0.28f + grind,
                             -1.12f + pressure * 0.18f);
    enemyBlade.scale = {1.20f, 1.20f, 1.20f};
    ModelDrawEffect enemyBladeGlow{};
    enemyBladeGlow.enabled = true;
    enemyBladeGlow.additiveBlend = true;
    enemyBladeGlow.disableCulling = true;
    enemyBladeGlow.color = {1.0f, 0.24f, 0.08f,
                            0.34f + 0.18f * pressure + 0.28f * sparkPulse};
    enemyBladeGlow.intensity = 0.52f + pressure * 0.38f + sparkPulse * 0.85f;
    enemyBladeGlow.fresnelPower = 0.74f;
    model->SetDrawEffect(enemyBladeGlow);
    model->Draw(swordModelId_, enemyBlade, movieCamera_);
    model->ClearDrawEffect();

    auto drawSword = [&](float side, float rollOffset) {
        Transform sword{};
        sword.position = {
            clashPoint.x - 0.20f + side * 0.13f + pressure * 0.06f,
            clashPoint.y - bladeVibration + side * 0.035f,
            clashPoint.z + (titleDemo ? (-0.22f + side * 0.05f)
                                      : (-0.04f + side * 0.03f))};
        sword.rotation =
            titleDemo ? MakeQuat(-0.66f + side * 0.06f + playerAdvance * 0.62f,
                                 0.18f + side * 0.36f - grind,
                                 rollOffset + side * (0.82f + playerAdvance * 0.74f))
                      : MakeQuat(-0.98f + side * 0.06f + pressure * 0.10f,
                                 0.18f + side * 0.36f - grind,
                                 rollOffset + side * (1.06f + pressure * 0.14f));
        sword.scale = {1.08f, 1.08f, 1.08f};

        ModelDrawEffect blade{};
        blade.enabled = true;
        blade.additiveBlend = true;
        blade.disableCulling = true;
        blade.color = {0.86f, 0.96f, 1.0f,
                       0.34f + 0.20f * pressure + 0.26f * sparkPulse};
        blade.intensity = 0.46f + 0.38f * pressure + 1.10f * sparkPulse;
        blade.fresnelPower = 0.82f;
        model->SetDrawEffect(blade);
        model->Draw(swordModelId_, sword, movieCamera_);
        model->ClearDrawEffect();
    };
    drawSword(-1.0f, -0.24f);
    drawSword(1.0f, 0.24f);

    if (sparkPulse > 0.01f) {
        for (int i = 0; i < 5; ++i) {
            const float trailT = static_cast<float>(i) / 2.0f;
            const float angle = sceneTime_ * 2.8f + static_cast<float>(i) * 1.26f;
            Transform spark{};
            spark.position = {clashPoint.x + std::cosf(angle) * 0.14f * trailT,
                              clashPoint.y + std::sinf(angle * 1.7f) * 0.10f,
                              clashPoint.z + std::sinf(angle) * 0.14f * trailT};
            spark.rotation = MakeQuat(-kPi * 0.5f, angle, kPi * 0.5f);
            spark.scale = {0.12f + sparkPulse * (0.38f + trailT * 0.16f),
                           0.12f + sparkPulse * (0.38f + trailT * 0.16f),
                           1.0f};
            ModelDrawEffect sparkEffect{};
            sparkEffect.enabled = true;
            sparkEffect.additiveBlend = true;
            sparkEffect.disableCulling = true;
            sparkEffect.color = {1.0f, 0.72f, 0.22f,
                                 sparkPulse * (0.34f - trailT * 0.045f)};
            sparkEffect.intensity = 0.90f + sparkPulse * 1.8f;
            sparkEffect.fresnelPower = 0.70f;
            model->SetDrawEffect(sparkEffect);
            model->Draw(movieRingModelId_, spark, movieCamera_);
        }
        model->ClearDrawEffect();
    }

    if (clash > 0.01f) {
        Transform shock{};
        shock.position = {0.0f, 1.12f, 0.92f};
        shock.rotation = MakeQuat(-kPi * 0.5f, sceneTime_ * 1.8f, 0.0f);
        shock.scale = {0.30f + clash * 0.62f + sparkPulse * 0.90f,
                       0.30f + clash * 0.62f + sparkPulse * 0.90f, 1.0f};
        ModelDrawEffect shockEffect{};
        shockEffect.enabled = true;
        shockEffect.additiveBlend = true;
        shockEffect.disableCulling = true;
        shockEffect.color = {1.0f, 0.82f, 0.35f,
                             0.14f + sparkPulse * 0.34f};
        shockEffect.intensity = 0.45f + pressure * 0.36f +
                                sparkPulse * 1.55f + finalHold * 1.4f;
        shockEffect.fresnelPower = 0.62f;
        model->SetDrawEffect(shockEffect);
        model->Draw(movieRingModelId_, shock, movieCamera_);
        model->ClearDrawEffect();
    }

    model->PostDraw();
}

void TitleScene::DrawMovieOverlay(float screenWidth, float screenHeight) {
    const float fadeIn = 1.0f - Smooth01(movieTimer_ / 1.6f);
    const float fadeOut =
        Smooth01((movieTimer_ - (kMovieDuration - 1.2f)) / 1.2f);
    const float flash =
        (std::max)(0.0f, 1.0f - std::fabs(movieTimer_ - kMovieDuration * 0.90f) /
                                      0.52f);
    const float t = Saturate(movieTimer_ / kMovieDuration);
    const float clashBeat = std::fmod(movieTimer_ * 1.85f, 1.0f);
    const float sparkFlash =
        PulseRange(clashBeat, 0.08f, 0.035f, 0.22f, 0.10f);
    const float charge = Smooth01((t - 0.58f) / 0.28f);

    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             MakeColor(0.0f, 0.0f, 0.0f,
                       std::clamp(fadeIn + fadeOut, 0.0f, 1.0f)));
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             MakeColor(0.0f, 0.0f, 0.0f, 0.10f + 0.16f * charge));
    if (sparkFlash > 0.01f) {
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                 MakeColor(1.0f, 0.72f, 0.28f, sparkFlash * 0.18f));
    }
    if (flash > 0.01f) {
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                 MakeColor(1.0f, 0.78f, 0.36f, flash * 0.42f));
    }
}

void TitleScene::DrawTitleBackground(float screenWidth, float screenHeight) {
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             MakeColor(0.015f, 0.018f, 0.024f, 1.0f));
    DrawRect(0.0f, screenHeight * 0.62f, screenWidth, screenHeight * 0.38f,
             MakeColor(0.040f, 0.038f, 0.030f, 1.0f));
    DrawRect(0.0f, screenHeight * 0.62f, screenWidth, 5.0f,
             MakeColor(1.0f, 0.80f, 0.10f, 0.88f));
}

void TitleScene::DrawTitleLogo(float screenWidth, float screenHeight,
                               float alpha, float scaleBias, float yOffset) {
    const float logoScale =
        std::clamp(screenWidth * 0.50f / logoImage_.width, 0.58f, 1.0f) *
        scaleBias;
    const float logoX = (screenWidth - logoImage_.width * logoScale) * 0.5f;
    const float logoY =
        (screenHeight - logoImage_.height * logoScale) * 0.5f + yOffset;
    DrawImage(logoImage_, logoX, logoY, alpha, logoScale);
}

TitleScene::Image TitleScene::LoadTitleImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width = static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
    return image;
}

void TitleScene::DrawRect(float x, float y, float w, float h,
                          const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    sprite.textureId = 0;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void TitleScene::DrawImage(const Image &image, float x, float y, float alpha) {
    DrawImage(image, x, y, alpha, 1.0f);
}

void TitleScene::DrawImage(const Image &image, float x, float y, float alpha,
                           float scale) {
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

bool TitleScene::IsAnyButtonTriggered(const Input &input) const {
    for (int dik = 0; dik < 256; ++dik) {
        if (input.IsKeyTrigger(dik)) {
            return true;
        }
    }

    if (!input.IsGamepadConnected()) {
        return false;
    }

    constexpr WORD kButtons[] = {
        XINPUT_GAMEPAD_DPAD_UP,        XINPUT_GAMEPAD_DPAD_DOWN,
        XINPUT_GAMEPAD_DPAD_LEFT,      XINPUT_GAMEPAD_DPAD_RIGHT,
        XINPUT_GAMEPAD_START,          XINPUT_GAMEPAD_BACK,
        XINPUT_GAMEPAD_LEFT_THUMB,     XINPUT_GAMEPAD_RIGHT_THUMB,
        XINPUT_GAMEPAD_LEFT_SHOULDER,  XINPUT_GAMEPAD_RIGHT_SHOULDER,
        XINPUT_GAMEPAD_A,              XINPUT_GAMEPAD_B,
        XINPUT_GAMEPAD_X,              XINPUT_GAMEPAD_Y,
    };

    for (WORD button : kButtons) {
        if (input.IsGamepadButtonTrigger(button)) {
            return true;
        }
    }

    return input.IsGamepadLeftTriggerTrigger() ||
           input.IsGamepadRightTriggerTrigger();
}

bool TitleScene::IsSkipTriggered(const Input &input) const {
    return input.IsKeyTrigger(DIK_SPACE) || input.IsKeyTrigger(DIK_RETURN) ||
           input.IsKeyTrigger(DIK_NUMPADENTER) ||
           (input.IsGamepadConnected() &&
            (input.IsGamepadButtonTrigger(XINPUT_GAMEPAD_START) ||
             input.IsGamepadButtonTrigger(XINPUT_GAMEPAD_A)));
}
