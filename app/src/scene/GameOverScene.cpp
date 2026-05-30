#include "GameOverScene.h"
#include "AppSceneServices.h"
#include "DirectXCommon.h"
#include "GameScene.h"
#include "Input.h"
#include "ModelManager.h"
#include "PostEffectManager.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "TitleScene.h"
#include "WinApp.h"
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace DirectX;

namespace {
constexpr float kDefeatIntroDuration = 2.35f;
constexpr float kDefeatIntroBlackFadeDuration = 0.18f;
constexpr DirectX::XMFLOAT3 kPlayerDefeatPosition{0.0f, 0.0f, 2.35f};
constexpr DirectX::XMFLOAT3 kDefeatSpotlightPosition{
    kPlayerDefeatPosition.x - 0.25f,
    kPlayerDefeatPosition.y + 3.20f,
    kPlayerDefeatPosition.z - 0.55f,
};
constexpr DirectX::XMFLOAT3 kDefeatSpotlightTarget{
    kPlayerDefeatPosition.x,
    kPlayerDefeatPosition.y + 0.10f,
    kPlayerDefeatPosition.z,
};
constexpr float kRetryRiseAnimDuration = 0.78f;
constexpr float kRetryRunStart = 0.84f;
constexpr float kRetryRunEndZ = 11.6f;
constexpr float kRetryBlackFadeDuration = 0.88f;
constexpr float kRetryBlackHoldDuration = 0.5f;
constexpr float kRetrySkipFadeDuration = 0.18f;
constexpr float kRetryRiseDuration =
    kRetryRunStart + kRetryBlackFadeDuration + kRetryBlackHoldDuration;
constexpr int kGameOverLetterCount = 8;
constexpr float kGameOverLetterStart = 0.54f;
constexpr float kGameOverLetterInterval = 0.13f;
constexpr float kGameOverLetterFadeDuration = 0.34f;
constexpr float kGameOverLetterOozeDuration = 0.48f;
constexpr float kGameOverTextHoldDuration = 3.0f;
constexpr int kGameOverParticleColumns = 40;
constexpr int kGameOverParticleRows = 12;
constexpr float kGameOverCrumbleStartDelay = 0.74f;
constexpr float kGameOverCrumbleDuration = 2.10f;
constexpr float kGameOverBlackFadeDuration = 0.78f;
constexpr float kGameOverBlackHoldDuration = 3.0f;
constexpr float kGameOverTextCompleteTime =
    kGameOverLetterStart +
    kGameOverLetterInterval * static_cast<float>(kGameOverLetterCount - 1) +
    kGameOverLetterFadeDuration;
constexpr float kGameOverBlackFadeStartTime =
    kGameOverTextCompleteTime + kGameOverTextHoldDuration;
constexpr float kTitleFadeDuration =
    kGameOverBlackFadeStartTime + kGameOverBlackFadeDuration +
    kGameOverBlackHoldDuration;
constexpr float kTitleFadeSkipDuration =
    kGameOverBlackFadeStartTime + kGameOverBlackFadeDuration;

bool IsAdvancePressed(Input *input) {
    return input != nullptr &&
           (input->IsKeyTrigger(DIK_SPACE) || input->IsKeyTrigger(DIK_RETURN) ||
            (input->IsGamepadConnected() &&
             input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_A)));
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

float SmoothStep01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float EaseInQuad(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t;
}

XMFLOAT4 MakeQuat(float pitch, float yaw, float roll) {
    XMFLOAT4 result{};
    XMStoreFloat4(&result, XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
    return result;
}

uint32_t CreateDustTexture(TextureManager *texture) {
    constexpr uint32_t kSize = 64;
    std::vector<uint8_t> pixels(static_cast<size_t>(kSize) * kSize * 4u);
    for (uint32_t y = 0; y < kSize; ++y) {
        for (uint32_t x = 0; x < kSize; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) /
                                static_cast<float>(kSize) * 2.0f -
                            1.0f;
            const float v = (static_cast<float>(y) + 0.5f) /
                                static_cast<float>(kSize) * 2.0f -
                            1.0f;
            const float r = std::sqrtf(u * u + v * v);
            const float core = 1.0f - SmoothStep01((r - 0.05f) / 0.24f);
            const float halo = 1.0f - SmoothStep01((r - 0.18f) / 0.58f);
            const float alpha = std::clamp(core * 0.82f + halo * 0.32f, 0.0f, 1.0f);
            const size_t index = (static_cast<size_t>(y) * kSize + x) * 4u;
            pixels[index + 0] = 255u;
            pixels[index + 1] = 244u;
            pixels[index + 2] = 210u;
            pixels[index + 3] = static_cast<uint8_t>(alpha * 255.0f);
        }
    }
    return texture->CreateFromRgbaPixels(kSize, kSize, pixels.data());
}

} // namespace

GameOverScene::GameOverScene(float elapsedTime,
                             const SwordInputCalibration &inputCalibration,
                             float combatDifficulty)
    : inputCalibration_(inputCalibration),
      combatDifficulty_(std::clamp(combatDifficulty, 0.0f, 9.0f)),
      elapsedTime_((std::max)(0.0f, elapsedTime)) {}

void GameOverScene::ResetDefeatCounts() {}

void GameOverScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    introTimer_ = 0.0f;
    state_ = State::DefeatIntro;
    menuIndex_ = 0;
    retryRiseTimer_ = 0.0f;
    retrySkipFadeTimer_ = 0.0f;
    titleFadeTimer_ = 0.0f;
    retrySkipFadeActive_ = false;
    titleFadeSkipRequested_ = false;

    const float aspect = static_cast<float>(ctx_->systems.winApp->GetWidth()) /
                         static_cast<float>(ctx_->systems.winApp->GetHeight());
    camera_.Initialize(aspect);
    camera_.SetClipRange(0.05f, 80.0f);

    if (ctx_->rendering.postEffectManager != nullptr) {
        ctx_->rendering.postEffectManager->SetBaseProfile(PostProcessProfile{});
    }
    if (ctx_->rendering.dxCommon != nullptr) {
        ctx_->rendering.dxCommon->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }

    ModelManager *model = ctx_->rendering.model;
    playerModelId_ = model->Load(L"app/resources/models/player/player.gltf");
    swordModelId_ = model->Load(L"app/resources/models/player/sword.glb");
    player_.Initialize(playerModelId_, swordModelId_);
    player_.SetInputCalibration(inputCalibration_);
    player_.LockPosition(kPlayerDefeatPosition);
    player_.SetYaw(0.65f);
    player_.SetDefeatPoseRatio(0.0f);

    Material floorMaterial{};
    floorMaterial.color = {0.070f, 0.064f, 0.058f, 1.0f};
    floorMaterial.enableTexture = 0;
    floorMaterial.roughness = 0.88f;
    floorMaterial.metallic = 0.0f;
    floorMaterial.reflectionStrength = 0.015f;
    floorMaterial.reflectionFresnelStrength = 0.0f;
    floorMaterial.cullMode = static_cast<int32_t>(MaterialCullMode::None);
    gameOverFloorModelId_ = model->CreatePlane(0, floorMaterial);

    spotlightDustTextureId_ = CreateDustTexture(ctx_->rendering.texture);
    Material poolMaterial{};
    poolMaterial.color = {2.7f, 2.05f, 1.08f, 0.26f};
    poolMaterial.enableTexture = 1;
    poolMaterial.baseColorTextureId = spotlightDustTextureId_;
    poolMaterial.reflectionStrength = 0.0f;
    poolMaterial.reflectionFresnelStrength = 0.0f;
    poolMaterial.blendMode = static_cast<int32_t>(BlendMode::Transparent);
    poolMaterial.cullMode = static_cast<int32_t>(MaterialCullMode::None);
    poolMaterial.depthWrite = 0;
    poolMaterial.roughness = 1.0f;
    spotlightPoolModelId_ = model->CreatePlane(spotlightDustTextureId_, poolMaterial);

    CreateTextImages();
}

void GameOverScene::Update() {
    const float deltaTime = ctx_->frame.deltaTime;
    sceneTime_ += deltaTime;

    switch (state_) {
    case State::DefeatIntro:
        if (IsAdvancePressed(ctx_->systems.input)) {
            FinishDefeatIntro();
            break;
        }
        introTimer_ = (std::min)(introTimer_ + deltaTime, kDefeatIntroDuration);
        {
            const float fall = EaseInQuad(introTimer_ / 0.36f);
            const float hit =
                (std::max)(0.0f, 1.0f - std::fabs(introTimer_ - 0.38f) / 0.08f);
            const float bounce =
                (std::max)(0.0f, 1.0f - std::fabs(introTimer_ - 0.50f) / 0.14f);
            const float y = kPlayerDefeatPosition.y + (1.0f - fall) * 6.4f -
                            hit * 0.34f + bounce * 0.18f;
            player_.LockPosition({kPlayerDefeatPosition.x, y,
                                  kPlayerDefeatPosition.z});
        }
        player_.SetYaw(0.65f);
        player_.SetDefeatPoseRatio(SmoothStep01((introTimer_ - 0.34f) / 0.42f));
        if (introTimer_ >= kDefeatIntroDuration) {
            FinishDefeatIntro();
        }
        break;
    case State::Menu:
        UpdateMenu();
        break;
    case State::RetryRise:
        UpdateRetryRise(deltaTime);
        break;
    case State::TitleFade:
        UpdateTitleFade(deltaTime);
        break;
    }
}

void GameOverScene::Draw() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    UpdateCamera(w, h);
    DrawWorld();

    ctx_->rendering.sprite->PreDraw();
    DrawOverlay(w, h);
    ctx_->rendering.sprite->PostDraw();
}

void GameOverScene::DrawTransparent() {}

void GameOverScene::CreateTextImages() {
    auto load = [&](const wchar_t *path) {
        Image image{};
        image.textureId = ctx_->rendering.texture->Load(path);
        image.width =
            static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
        image.height = static_cast<float>(
            ctx_->rendering.texture->GetHeight(image.textureId));
        return image;
    };

    defeatCleanImage_ = load(L"app/resources/ui/gameover/defeat_clean.png");
    defeatImage_ = load(L"app/resources/ui/gameover/defeat.png");
    retryImage_ = load(L"app/resources/ui/gameover/retry.png");
    titleImage_ = load(L"app/resources/ui/gameover/title.png");
    const wchar_t *gameOverLetterPaths[kGameOverLetterCount] = {
        L"app/resources/ui/gameover/letters/gameover_0_g.png",
        L"app/resources/ui/gameover/letters/gameover_1_a.png",
        L"app/resources/ui/gameover/letters/gameover_2_m.png",
        L"app/resources/ui/gameover/letters/gameover_3_e.png",
        L"app/resources/ui/gameover/letters/gameover_4_o.png",
        L"app/resources/ui/gameover/letters/gameover_5_v.png",
        L"app/resources/ui/gameover/letters/gameover_6_e.png",
        L"app/resources/ui/gameover/letters/gameover_7_r.png",
    };
    for (int i = 0; i < kGameOverLetterCount; ++i) {
        gameOverLetterImages_[static_cast<size_t>(i)] =
            load(gameOverLetterPaths[i]);
    }
}

void GameOverScene::FinishDefeatIntro() {
    introTimer_ = kDefeatIntroDuration;
    player_.LockPosition(kPlayerDefeatPosition);
    player_.SetYaw(0.65f);
    player_.SetDefeatPoseRatio(1.0f);
    state_ = State::Menu;
    menuIndex_ = 0;
}

void GameOverScene::UpdateMenu() {
    Input *input = ctx_->systems.input;
    if (input->IsKeyTrigger(DIK_A) || input->IsKeyTrigger(DIK_LEFT)) {
        if (menuIndex_ != 0) {
            menuIndex_ = 0;
            AppSceneServices::PlayMenuSe(*ctx_,
                                         AppSceneServices::MenuSe::Select);
        }
    }
    if (input->IsKeyTrigger(DIK_D) || input->IsKeyTrigger(DIK_RIGHT)) {
        if (menuIndex_ != 1) {
            menuIndex_ = 1;
            AppSceneServices::PlayMenuSe(*ctx_,
                                         AppSceneServices::MenuSe::Select);
        }
    }

    const bool confirm = IsAdvancePressed(input);
    if (!confirm) {
        return;
    }

    if (menuIndex_ == 0) {
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
        retryRiseTimer_ = 0.0f;
        state_ = State::RetryRise;
    } else {
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
        titleFadeTimer_ = 0.0f;
        titleFadeSkipRequested_ = false;
        state_ = State::TitleFade;
    }
}

void GameOverScene::UpdateRetryRise(float deltaTime) {
    if (retrySkipFadeActive_) {
        retrySkipFadeTimer_ =
            (std::min)(retrySkipFadeTimer_ + deltaTime, kRetrySkipFadeDuration);
        if (retrySkipFadeTimer_ >= kRetrySkipFadeDuration) {
            sceneManager_->ChangeScene(
                std::make_unique<GameScene>(inputCalibration_, combatDifficulty_));
        }
        return;
    }
    if (IsAdvancePressed(ctx_->systems.input)) {
        retrySkipFadeActive_ = true;
        retrySkipFadeTimer_ = 0.0f;
        return;
    }

    retryRiseTimer_ = (std::min)(retryRiseTimer_ + deltaTime, kRetryRiseDuration);
    const float rawT =
        std::clamp(retryRiseTimer_ / kRetryRiseAnimDuration, 0.0f, 1.0f);
    constexpr float kSqueezeEnd = 0.16f;
    constexpr float kPopEnd = 0.68f;
    constexpr float kTrembleEnd = 0.24f;
    const float squeezeT = std::clamp(rawT / kSqueezeEnd, 0.0f, 1.0f);
    const float popT =
        std::clamp((rawT - kSqueezeEnd) / (kPopEnd - kSqueezeEnd), 0.0f, 1.0f);
    const float riseT = rawT < kSqueezeEnd
                            ? 0.0f
                            : std::clamp(1.0f - std::powf(1.0f - popT, 4.2f),
                                         0.0f, 1.0f);
    const float defeatPose = 1.0f - riseT;
    const float defeatPoseEased = SmoothStep01(defeatPose);
    constexpr float kDefeatVisualGroundOffset = 0.48f;
    const float squeezeDip = std::sinf(squeezeT * XM_PI) * 0.10f;
    const float tremble =
        rawT < kTrembleEnd ? (1.0f - rawT / kTrembleEnd) : 0.0f;
    const float shakeX = std::sinf(retryRiseTimer_ * 118.0f) * 0.038f * tremble;
    const float shakeZ = std::cosf(retryRiseTimer_ * 96.0f) * 0.024f * tremble;
    const float shakeYaw = std::sinf(retryRiseTimer_ * 110.0f) * 0.092f * tremble;
    const float turnT =
        SmoothStep01((retryRiseTimer_ - kRetryRiseAnimDuration * 0.58f) /
                     (kRetryRiseAnimDuration * 0.34f));
    const float runT = SmoothStep01((retryRiseTimer_ - kRetryRunStart) /
                                    kRetryBlackFadeDuration);
    const float runZ =
        kPlayerDefeatPosition.z +
        (kRetryRunEndZ - kPlayerDefeatPosition.z) * runT;
    player_.LockPosition({
        kPlayerDefeatPosition.x + shakeX,
        kPlayerDefeatPosition.y -
            kDefeatVisualGroundOffset * (1.0f - defeatPoseEased) - squeezeDip,
        runZ + shakeZ,
    });
    player_.SetYaw((0.65f + shakeYaw) * (1.0f - turnT));
    player_.SetDefeatPoseRatio(defeatPose);
    if (retryRiseTimer_ >= kRetryRiseDuration) {
        sceneManager_->ChangeScene(
            std::make_unique<GameScene>(inputCalibration_, combatDifficulty_));
    }
}

void GameOverScene::UpdateTitleFade(float deltaTime) {
    if (IsAdvancePressed(ctx_->systems.input)) {
        titleFadeSkipRequested_ = true;
        if (titleFadeTimer_ < kGameOverBlackFadeStartTime) {
            titleFadeTimer_ = kGameOverBlackFadeStartTime;
        }
    }
    const float targetDuration =
        titleFadeSkipRequested_ ? kTitleFadeSkipDuration : kTitleFadeDuration;
    titleFadeTimer_ = (std::min)(titleFadeTimer_ + deltaTime, targetDuration);
    if (titleFadeTimer_ >= targetDuration) {
        ResetDefeatCounts();
        sceneManager_->ChangeScene(std::make_unique<TitleScene>());
    }
}

void GameOverScene::UpdateCamera(float screenWidth, float screenHeight) {
    camera_.SetAspect(screenWidth / (std::max)(screenHeight, 1.0f));
    camera_.SetPerspectiveFovDeg(42.0f);
    const XMFLOAT3 target{0.0f, 0.48f, 2.28f};
    const XMFLOAT3 eye{0.0f, 2.05f, -6.9f};
    camera_.SetPosition(eye);
    camera_.SetRotation(CameraRotationLookAt(eye, target));
}

void GameOverScene::DrawWorld() {
    ModelManager *model = ctx_->rendering.model;
    if (model == nullptr) {
        return;
    }

    const bool retrySpotlightOff =
        state_ == State::RetryRise &&
        (retrySkipFadeActive_ || retryRiseTimer_ >= kRetryRunStart);

    SceneLighting lighting{};
    lighting.keyLightDirection = {-0.08f, -1.0f, 0.18f};
    lighting.keyLightColor = {0.34f, 0.30f, 0.24f, 1.0f};
    lighting.fillLightDirection = {0.62f, -0.22f, -0.62f};
    lighting.fillLightColor = {0.08f, 0.12f, 0.20f, 0.34f};
    lighting.ambientColor = {0.035f, 0.038f, 0.045f, 1.0f};
    lighting.pointLights[0].colorIntensity = {1.0f, 0.84f, 0.54f, 0.0f};
    lighting.pointLights[1].colorIntensity = {0.16f, 0.26f, 0.58f, 0.0f};
    lighting.spotLight.positionRange = {kDefeatSpotlightPosition.x,
                                        kDefeatSpotlightPosition.y,
                                        kDefeatSpotlightPosition.z, 8.20f};
    lighting.spotLight.direction = {0.083f, -0.979f, 0.183f, 0.0f};
    lighting.spotLight.colorIntensity = {
        1.0f, 0.86f, 0.58f, retrySpotlightOff ? 0.0f : 11.50f};
    lighting.spotLight.angleParams = {0.976f, 0.620f, 1.85f, 1.0f};
    lighting.lightingParams = {64.0f, 0.26f, 2.60f, 0.03f};
    model->SetSceneLighting(lighting);

    SceneFog fog{};
    fog.params = {0.0f, 0.0f, 1.0f, 0.0f};
    model->SetSceneFog(fog);

    if (ctx_->rendering.dxCommon != nullptr) {
        ctx_->rendering.dxCommon->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }

    model->PrepareSkinning({playerModelId_});
    model->PreDraw();
    if (gameOverFloorModelId_ != 0) {
        Transform floor{};
        floor.position = {0.0f, -0.46f, 2.35f};
        floor.rotation = MakeQuat(-XM_PIDIV2, 0.0f, 0.0f);
        floor.scale = {32.0f, 32.0f, 1.0f};
        model->Draw(gameOverFloorModelId_, floor, camera_);
    }
    if (!retrySpotlightOff && spotlightPoolModelId_ != 0) {
        Transform pool{};
        pool.position = {kDefeatSpotlightTarget.x, kPlayerDefeatPosition.y - 0.45f,
                         kDefeatSpotlightTarget.z};
        pool.rotation = MakeQuat(-XM_PIDIV2, 0.0f, 0.0f);
        pool.scale = {2.65f, 2.65f, 1.0f};
        model->Draw(spotlightPoolModelId_, pool, camera_);
    }
    player_.Draw(model, camera_, true, true, 0.92f);
    model->PostDraw();
}

void GameOverScene::DrawOverlay(float screenWidth, float screenHeight) {
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f, 0.08f));
    DrawDefeatTitle(screenWidth, screenHeight);

    if (state_ == State::DefeatIntro) {
        const float fade =
            1.0f - SmoothStep01(introTimer_ / kDefeatIntroBlackFadeDuration);
        if (fade > 0.001f) {
            DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                     Color(0.0f, 0.0f, 0.0f, fade));
        }
        return;
    }
    if (state_ == State::Menu || state_ == State::RetryRise) {
        DrawMenu(screenWidth, screenHeight);
    }
    if (state_ == State::RetryRise) {
        const float retryFade =
            SmoothStep01((retryRiseTimer_ - kRetryRunStart) /
                         kRetryBlackFadeDuration);
        const float skipFade =
            retrySkipFadeActive_
                ? SmoothStep01(retrySkipFadeTimer_ / kRetrySkipFadeDuration)
                : 0.0f;
        const float fade = (std::max)(retryFade, skipFade);
        if (fade > 0.001f) {
            DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                     Color(0.0f, 0.0f, 0.0f, fade));
        }
    }
    if (state_ == State::TitleFade) {
        DrawTitleFade(screenWidth, screenHeight);
    }
}

void GameOverScene::DrawDefeatTitle(float screenWidth, float screenHeight) {
    const Image &image = defeatCleanImage_;
    const float introAlpha =
        state_ == State::DefeatIntro ? SmoothStep01(introTimer_ / 0.28f) : 1.0f;
    const float baseScale =
        std::clamp(screenWidth * 0.54f / (std::max)(image.width, 1.0f), 0.42f,
                   0.86f);
    const float introSettle =
        state_ == State::DefeatIntro ? SmoothStep01((introTimer_ - 0.22f) / 0.30f)
                                     : 1.0f;
    const float impact =
        state_ == State::DefeatIntro
            ? (std::max)(0.0f,
                         1.0f - std::fabs(introTimer_ - 0.34f) / 0.07f)
            : 0.0f;
    const float defeatScale = baseScale * (1.22f - 0.22f * introSettle +
                                           impact * 0.08f);
    const float defeatW = image.width * defeatScale;
    const float x = (screenWidth - defeatW) * 0.5f;
    const float targetY = screenHeight * 0.085f;
    const float titleFall =
        state_ == State::DefeatIntro ? EaseInQuad(introTimer_ / 0.34f) : 1.0f;
    const float titleSettle =
        state_ == State::DefeatIntro
            ? (std::max)(0.0f,
                         1.0f - std::fabs(introTimer_ - 0.36f) / 0.08f)
            : 0.0f;
    const float titleBounce =
        state_ == State::DefeatIntro
            ? (std::max)(0.0f,
                         1.0f - std::fabs(introTimer_ - 0.48f) / 0.12f)
            : 0.0f;
    const float y = targetY - (1.0f - titleFall) * (screenHeight * 0.50f) +
                    titleSettle * 30.0f - titleBounce * 10.0f;
    DrawImage(image, x, y, defeatScale, introAlpha);
}

void GameOverScene::DrawMenu(float screenWidth, float screenHeight) {
    const float buttonW = std::clamp(screenWidth * 0.18f, 210.0f, 300.0f);
    const float buttonH = 72.0f;
    const float gap = screenWidth * 0.045f;
    const float totalW = buttonW * 2.0f + gap;
    const float y = screenHeight * 0.78f;
    const float firstX = (screenWidth - totalW) * 0.5f;
    const Image *labels[2] = {&retryImage_, &titleImage_};

    for (int i = 0; i < 2; ++i) {
        const float x = firstX + static_cast<float>(i) * (buttonW + gap);
        const bool selected = i == menuIndex_;
        DrawRect(x, y, buttonW, buttonH,
                 selected ? Color(0.18f, 0.13f, 0.055f, 0.98f)
                          : Color(0.030f, 0.034f, 0.040f, 0.92f));
        DrawFrame(x, y, buttonW, buttonH, 2.0f,
                  selected ? Color(1.0f, 0.78f, 0.34f, 0.96f)
                           : Color(0.62f, 0.66f, 0.72f, 0.38f));
        const Image &label = *labels[i];
        const float scale =
            (std::min)({1.0f, (buttonW * 0.72f) / (std::max)(label.width, 1.0f),
                        (buttonH * 0.60f) / (std::max)(label.height, 1.0f)});
        DrawImage(label, x + (buttonW - label.width * scale) * 0.5f,
                  y + (buttonH - label.height * scale) * 0.5f, scale,
                  selected ? 1.0f : 0.82f);
    }
}

void GameOverScene::DrawTitleFade(float screenWidth, float screenHeight) {
    const float t = SmoothStep01(titleFadeTimer_ / 0.70f);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f, t));
    if (titleFadeTimer_ < kGameOverLetterStart - 0.06f) {
        return;
    }

    float totalLetterWidth = 0.0f;
    float maxLetterHeight = 0.0f;
    for (const Image &letter : gameOverLetterImages_) {
        totalLetterWidth += letter.width;
        maxLetterHeight = (std::max)(maxLetterHeight, letter.height);
    }
    if (totalLetterWidth <= 0.0f || maxLetterHeight <= 0.0f) {
        return;
    }

    const float scale =
        std::clamp(screenWidth * 0.50f / totalLetterWidth, 0.44f, 0.86f);
    const float textW = totalLetterWidth * scale;
    const float textH = maxLetterHeight * scale;
    const float x = (screenWidth - textW) * 0.5f;
    const float y = (screenHeight - textH) * 0.5f;
    const float blackFade =
        SmoothStep01((titleFadeTimer_ - kGameOverBlackFadeStartTime) /
                     kGameOverBlackFadeDuration);
    const float crumbleStart =
        kGameOverTextCompleteTime + kGameOverCrumbleStartDelay;
    float cursorX = x;
    for (int i = 0; i < kGameOverLetterCount; ++i) {
        const Image &letter = gameOverLetterImages_[static_cast<size_t>(i)];
        const float letterStart =
            kGameOverLetterStart +
            kGameOverLetterInterval * static_cast<float>(i);
        const float appear =
            SmoothStep01((titleFadeTimer_ - letterStart) /
                         kGameOverLetterFadeDuration);
        if (appear > 0.001f) {
            const float ooze =
                1.0f - SmoothStep01((titleFadeTimer_ - letterStart) /
                                    kGameOverLetterOozeDuration);
            const float wave =
                std::sinf(titleFadeTimer_ * 7.4f + static_cast<float>(i) * 1.83f);
            const float slide = ooze * (5.0f + 1.8f * wave);
            const float wobbleX = ooze * wave * 1.4f;
            const float letterAlpha = appear;
            const float lx = cursorX + wobbleX;
            const float ly = y + slide;
            const float letterW = letter.width * scale;
            const float letterH = letter.height * scale;
            const float centerX = lx + letterW * 0.5f;
            const float centerY = ly + letterH * 0.5f;
            const float textDissolve =
                SmoothStep01((titleFadeTimer_ - crumbleStart) /
                             (kGameOverCrumbleDuration * 0.46f));

            for (int layer = 4; layer >= 1; --layer) {
                const float layerF = static_cast<float>(layer);
                const float bloomAlpha =
                    letterAlpha * ooze * (1.0f - textDissolve) *
                    (0.12f + 0.035f * layerF);
                if (bloomAlpha <= 0.001f) {
                    continue;
                }
                const float bloomScale = scale * (1.0f + ooze * 0.16f +
                                                  layerF * 0.015f);
                const float bloomW = letter.width * bloomScale;
                const float bloomH = letter.height * bloomScale;
                const float offsetX =
                    std::sinf(titleFadeTimer_ * 9.2f + layerF * 2.1f +
                              static_cast<float>(i)) *
                    ooze * layerF * 1.1f;
                const float offsetY =
                    std::cosf(titleFadeTimer_ * 8.4f + layerF * 1.7f +
                              static_cast<float>(i) * 0.6f) *
                    ooze * layerF * 0.9f;
                DrawImageTint(letter, centerX - bloomW * 0.5f + offsetX,
                              centerY - bloomH * 0.5f + offsetY, bloomScale,
                              Color(0.56f, 0.56f, 0.55f, bloomAlpha));
            }

            const float bodyScale = scale * (0.96f + 0.04f * appear);
            const float bodyW = letter.width * bodyScale;
            const float bodyH = letter.height * bodyScale;
            const float bodyX = centerX - bodyW * 0.5f;
            const float bodyY = centerY - bodyH * 0.5f;
            if (textDissolve < 0.995f) {
                const float wholeAlpha = letterAlpha * (1.0f - textDissolve);
                DrawImageTint(letter, bodyX - 2.0f, bodyY + 2.0f, bodyScale,
                              Color(0.30f, 0.30f, 0.30f,
                                    wholeAlpha * (0.26f + ooze * 0.16f)));
                DrawImageTint(letter, bodyX, bodyY, bodyScale,
                              Color(0.72f, 0.72f, 0.70f, wholeAlpha));
            }
            const float cellW =
                bodyW / static_cast<float>(kGameOverParticleColumns);
            const float cellH =
                bodyH / static_cast<float>(kGameOverParticleRows);
            for (int row = 0; row < kGameOverParticleRows; ++row) {
                for (int col = 0; col < kGameOverParticleColumns; ++col) {
                    const float colF = static_cast<float>(col);
                    const float rowF = static_cast<float>(row);
                    const float seed =
                        static_cast<float>(i) * 13.0f + colF * 3.17f +
                        rowF * 7.31f;
                    const float randomA = std::sinf(seed) * 0.5f + 0.5f;
                    const float randomB = std::cosf(seed * 1.63f) * 0.5f + 0.5f;
                    const float startOffset = randomA * 0.28f + randomB * 0.10f;
                    const float local =
                        SmoothStep01((titleFadeTimer_ - crumbleStart -
                                      startOffset) /
                                     kGameOverCrumbleDuration);
                    if (local <= 0.002f) {
                        continue;
                    }
                    const float gust = local * local;
                    const float spray = SmoothStep01((local - 0.08f) / 0.62f);
                    const float windX =
                        screenWidth * (0.16f + randomA * 0.18f) +
                        colF * 3.2f;
                    const float windY =
                        -screenHeight * (0.05f + randomB * 0.14f) +
                        (rowF - 4.5f) * 4.2f;
                    const float flutterX =
                        std::sinf(titleFadeTimer_ * (8.0f + randomA * 4.0f) +
                                  seed) *
                        spray * 20.0f;
                    const float flutterY =
                        std::cosf(titleFadeTimer_ * (7.0f + randomB * 5.0f) +
                                  seed * 0.71f) *
                        spray * 14.0f;
                    const float fade =
                        letterAlpha * SmoothStep01(local / 0.18f) *
                        (1.0f - local * 0.96f);
                    if (fade <= 0.003f) {
                        continue;
                    }

                    const float grainBase =
                        std::clamp(scale * (2.6f + randomA * 2.4f), 1.0f, 3.6f);
                    const float particleSize =
                        grainBase * (1.0f - local * (0.22f + randomB * 0.24f));
                    const float baseX = bodyX + cellW * colF;
                    const float baseY = bodyY + cellH * rowF;
                    const float particleX =
                        baseX + cellW * randomA + gust * windX + flutterX -
                        particleSize * 0.5f;
                    const float particleY =
                        baseY + cellH * randomB + gust * windY + flutterY -
                        particleSize * 0.5f;
                    const float uvLeft =
                        colF / static_cast<float>(kGameOverParticleColumns);
                    const float uvTop =
                        rowF / static_cast<float>(kGameOverParticleRows);
                    const float uvWidth =
                        1.0f / static_cast<float>(kGameOverParticleColumns);
                    const float uvHeight =
                        1.0f / static_cast<float>(kGameOverParticleRows);
                    const float particleAlpha = fade * (0.58f + spray * 0.32f);
                    DrawImageSlice(letter, particleX, particleY, particleSize,
                                   particleSize,
                                   Color(0.72f, 0.72f, 0.70f, particleAlpha),
                                   uvLeft, uvTop, uvWidth, uvHeight);
                }
            }
        }
        cursorX += letter.width * scale;
    }

    if (blackFade > 0.001f) {
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                 Color(0.0f, 0.0f, 0.0f, blackFade));
    }
}

void GameOverScene::DrawImage(const Image &image, float x, float y, float scale,
                              float alpha) {
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

void GameOverScene::DrawImageTint(const Image &image, float x, float y,
                                  float scale, const XMFLOAT4 &color) {
    if (image.textureId == 0 || image.width <= 0.0f || image.height <= 0.0f ||
        color.w <= 0.0f) {
        return;
    }
    Sprite sprite{};
    sprite.textureId = image.textureId;
    sprite.position = {x, y};
    sprite.size = {image.width * scale, image.height * scale};
    sprite.color = color;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void GameOverScene::DrawImageSlice(const Image &image, float x, float y,
                                   float w, float h, const XMFLOAT4 &color,
                                   float uvLeft, float uvTop, float uvWidth,
                                   float uvHeight) {
    if (image.textureId == 0 || w <= 0.0f || h <= 0.0f || color.w <= 0.0f) {
        return;
    }
    Sprite sprite{};
    sprite.textureId = image.textureId;
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.uvLeftTop = {std::clamp(uvLeft, 0.0f, 1.0f),
                        std::clamp(uvTop, 0.0f, 1.0f)};
    sprite.uvSize = {std::clamp(uvWidth, 0.0f, 1.0f),
                     std::clamp(uvHeight, 0.0f, 1.0f)};
    sprite.color = color;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void GameOverScene::DrawTextureRect(uint32_t textureId, float x, float y,
                                    float w, float h, const XMFLOAT4 &color,
                                    float uvWidth, SpriteBlendMode blendMode,
                                    float uvLeft) {
    if (textureId == 0 || w <= 0.0f || h <= 0.0f) {
        return;
    }
    Sprite sprite{};
    sprite.textureId = textureId;
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.uvLeftTop = {std::clamp(uvLeft, 0.0f, 1.0f), 0.0f};
    sprite.uvSize = {std::clamp(uvWidth, 0.0f, 1.0f), 1.0f};
    sprite.color = color;
    sprite.blendMode = blendMode;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void GameOverScene::DrawRect(float x, float y, float w, float h,
                             const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.textureId = 0;
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void GameOverScene::DrawFrame(float x, float y, float w, float h,
                              float thickness, const XMFLOAT4 &color) {
    DrawRect(x, y, w, thickness, color);
    DrawRect(x, y + h - thickness, w, thickness, color);
    DrawRect(x, y, thickness, h, color);
    DrawRect(x + w - thickness, y, thickness, h, color);
}

XMFLOAT4 GameOverScene::Color(float r, float g, float b, float a) const {
    return {r, g, b, a};
}
