#include "GameOverScene.h"
#include "DirectXCommon.h"
#include "GameScene.h"
#include "Input.h"
#include "ModelManager.h"
#include "PostProcessSystem.h"
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
constexpr float kDifficultyDropDuration = 0.82f;
constexpr float kDifficultyDropHoldDuration = 1.35f;
constexpr float kDefeatIntroDuration = 2.35f;
constexpr DirectX::XMFLOAT3 kPlayerDefeatPosition{0.0f, 0.0f, 2.35f};
constexpr float kRetryRiseDuration = 1.18f;
constexpr float kTitleFadeDuration = 1.35f;

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

XMFLOAT4 LerpColor(const XMFLOAT4 &a, const XMFLOAT4 &b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
}

XMFLOAT4 GaugeHeatColor(float t, float alpha) {
    t = std::clamp(t, 0.0f, 1.0f);
    const XMFLOAT4 blue{0.015f, 0.075f, 0.50f, alpha};
    const XMFLOAT4 yellow{1.0f, 0.78f, 0.10f, alpha};
    const XMFLOAT4 red{0.62f, 0.018f, 0.010f, alpha};
    if (t < 0.62f) {
        return LerpColor(blue, yellow, t / 0.62f);
    }
    return LerpColor(yellow, red, (t - 0.62f) / 0.38f);
}

uint32_t CreateTriangleTexture(TextureManager *texture, bool gradient) {
    constexpr uint32_t kWidth = 512;
    constexpr uint32_t kHeight = 128;
    constexpr float kAaPixels = 1.8f;
    std::vector<uint8_t> pixels(static_cast<size_t>(kWidth) * kHeight * 4u);
    for (uint32_t py = 0; py < kHeight; ++py) {
        for (uint32_t px = 0; px < kWidth; ++px) {
            const float x = (static_cast<float>(px) + 0.5f) /
                            static_cast<float>(kWidth - 1u);
            const float y = (static_cast<float>(py) + 0.5f) /
                            static_cast<float>(kHeight - 1u);
            const float line = 1.0f - x;
            const float distancePixels = (y - line) * static_cast<float>(kHeight);
            const float alpha =
                std::clamp(distancePixels / kAaPixels + 0.5f, 0.0f, 1.0f);
            const XMFLOAT4 color =
                gradient ? GaugeHeatColor(x, alpha)
                         : XMFLOAT4{1.0f, 1.0f, 1.0f, alpha};
            const size_t index = (static_cast<size_t>(py) * kWidth + px) * 4u;
            pixels[index + 0] =
                static_cast<uint8_t>(std::clamp(color.x, 0.0f, 1.0f) * 255.0f);
            pixels[index + 1] =
                static_cast<uint8_t>(std::clamp(color.y, 0.0f, 1.0f) * 255.0f);
            pixels[index + 2] =
                static_cast<uint8_t>(std::clamp(color.z, 0.0f, 1.0f) * 255.0f);
            pixels[index + 3] =
                static_cast<uint8_t>(std::clamp(color.w, 0.0f, 1.0f) * 255.0f);
        }
    }
    return texture->CreateFromRgbaPixels(kWidth, kHeight, pixels.data());
}
} // namespace

GameOverScene::GameOverScene(float elapsedTime,
                             const SwordInputCalibration &inputCalibration,
                             float combatDifficulty)
    : inputCalibration_(inputCalibration),
      combatDifficulty_(std::clamp(combatDifficulty, 0.0f, 9.0f)),
      elapsedTime_((std::max)(0.0f, elapsedTime)),
      difficultyBeforeDrop_(std::clamp(combatDifficulty, 0.0f, 9.0f)),
      displayedDifficulty_(std::clamp(combatDifficulty, 0.0f, 9.0f)) {}

void GameOverScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    introTimer_ = 0.0f;
    state_ = State::DefeatIntro;
    promptIndex_ = 1;
    menuIndex_ = 0;
    difficultyDropTimer_ = 0.0f;
    retryRiseTimer_ = 0.0f;
    titleFadeTimer_ = 0.0f;
    difficultyBeforeDrop_ = combatDifficulty_;
    displayedDifficulty_ = combatDifficulty_;

    const float aspect = static_cast<float>(ctx_->systems.winApp->GetWidth()) /
                         static_cast<float>(ctx_->systems.winApp->GetHeight());
    camera_.Initialize(aspect);
    camera_.SetClipRange(0.05f, 80.0f);

    if (ctx_->rendering.postProcessSystem != nullptr) {
        ctx_->rendering.postProcessSystem->SetProfile(PostProcessProfile{});
    }
    if (ctx_->rendering.dxCommon != nullptr) {
        ctx_->rendering.dxCommon->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }

    ModelManager *model = ctx_->rendering.model;
    playerModelId_ = model->Load(L"app/resources/models/player/player.glb");
    swordModelId_ = model->Load(L"app/resources/models/player/sword.glb");
    player_.Initialize(playerModelId_, swordModelId_);
    player_.SetInputCalibration(inputCalibration_);
    player_.LockPosition(kPlayerDefeatPosition);
    player_.SetYaw(0.65f);
    player_.SetDefeatPoseRatio(0.0f);

    CreateTextImages();
}

void GameOverScene::Update() {
    const float deltaTime = ctx_->frame.deltaTime;
    sceneTime_ += deltaTime;

    switch (state_) {
    case State::DefeatIntro:
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
            player_.LockPosition(kPlayerDefeatPosition);
            player_.SetYaw(0.65f);
            player_.SetDefeatPoseRatio(1.0f);
            state_ = State::DifficultyPrompt;
        }
        break;
    case State::DifficultyPrompt:
        UpdateDifficultyPrompt();
        break;
    case State::DifficultyDrop:
        UpdateDifficultyDrop(deltaTime);
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
    lowerDifficultyImage_ =
        load(L"app/resources/ui/gameover/lower_difficulty.png");
    yesImage_ = load(L"app/resources/ui/gameover/yes.png");
    noImage_ = load(L"app/resources/ui/gameover/no.png");
    retryImage_ = load(L"app/resources/ui/gameover/retry.png");
    titleImage_ = load(L"app/resources/ui/gameover/title.png");
    gameOverImage_ = load(L"app/resources/ui/gameover/gameover.png");
    triangleMaskImage_.textureId =
        CreateTriangleTexture(ctx_->rendering.texture, false);
    triangleMaskImage_.width = 512.0f;
    triangleMaskImage_.height = 128.0f;
    triangleGradientImage_.textureId =
        CreateTriangleTexture(ctx_->rendering.texture, true);
    triangleGradientImage_.width = 512.0f;
    triangleGradientImage_.height = 128.0f;
}

void GameOverScene::UpdateDifficultyPrompt() {
    Input *input = ctx_->systems.input;
    if (input->IsKeyTrigger(DIK_A) || input->IsKeyTrigger(DIK_LEFT)) {
        promptIndex_ = 0;
    }
    if (input->IsKeyTrigger(DIK_D) || input->IsKeyTrigger(DIK_RIGHT)) {
        promptIndex_ = 1;
    }

    const bool confirm =
        input->IsKeyTrigger(DIK_SPACE) || input->IsKeyTrigger(DIK_RETURN) ||
        (input->IsGamepadConnected() &&
         input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_A));
    if (!confirm) {
        return;
    }

    if (promptIndex_ == 0) {
        difficultyBeforeDrop_ = combatDifficulty_;
        combatDifficulty_ = std::clamp(combatDifficulty_ - 1.0f, 0.0f, 9.0f);
        difficultyDropTimer_ = 0.0f;
        state_ = State::DifficultyDrop;
    } else {
        state_ = State::Menu;
        menuIndex_ = 0;
    }
}

void GameOverScene::UpdateDifficultyDrop(float deltaTime) {
    difficultyDropTimer_ =
        (std::min)(difficultyDropTimer_ + deltaTime,
                   kDifficultyDropDuration + kDifficultyDropHoldDuration);
    const float t = SmoothStep01(difficultyDropTimer_ / kDifficultyDropDuration);
    displayedDifficulty_ =
        difficultyBeforeDrop_ + (combatDifficulty_ - difficultyBeforeDrop_) * t;
    if (difficultyDropTimer_ >= kDifficultyDropDuration + kDifficultyDropHoldDuration) {
        displayedDifficulty_ = combatDifficulty_;
        state_ = State::Menu;
        menuIndex_ = 0;
    }
}

void GameOverScene::UpdateMenu() {
    Input *input = ctx_->systems.input;
    if (input->IsKeyTrigger(DIK_A) || input->IsKeyTrigger(DIK_LEFT)) {
        menuIndex_ = 0;
    }
    if (input->IsKeyTrigger(DIK_D) || input->IsKeyTrigger(DIK_RIGHT)) {
        menuIndex_ = 1;
    }

    const bool confirm =
        input->IsKeyTrigger(DIK_SPACE) || input->IsKeyTrigger(DIK_RETURN) ||
        (input->IsGamepadConnected() &&
         input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_A));
    if (!confirm) {
        return;
    }

    if (menuIndex_ == 0) {
        retryRiseTimer_ = 0.0f;
        state_ = State::RetryRise;
    } else {
        titleFadeTimer_ = 0.0f;
        state_ = State::TitleFade;
    }
}

void GameOverScene::UpdateRetryRise(float deltaTime) {
    retryRiseTimer_ = (std::min)(retryRiseTimer_ + deltaTime, kRetryRiseDuration);
    const float t = SmoothStep01(retryRiseTimer_ / kRetryRiseDuration);
    player_.SetDefeatPoseRatio(1.0f - t);
    if (retryRiseTimer_ >= kRetryRiseDuration) {
        sceneManager_->ChangeScene(
            std::make_unique<GameScene>(inputCalibration_, combatDifficulty_));
    }
}

void GameOverScene::UpdateTitleFade(float deltaTime) {
    titleFadeTimer_ = (std::min)(titleFadeTimer_ + deltaTime, kTitleFadeDuration);
    if (titleFadeTimer_ >= kTitleFadeDuration) {
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

    SceneLighting lighting{};
    lighting.keyLightDirection = {-0.22f, -0.88f, 0.40f};
    lighting.keyLightColor = {1.34f, 1.34f, 1.30f, 1.0f};
    lighting.fillLightDirection = {0.60f, -0.30f, -0.55f};
    lighting.fillLightColor = {0.30f, 0.30f, 0.36f, 0.48f};
    lighting.ambientColor = {0.20f, 0.20f, 0.22f, 1.0f};
    lighting.lightingParams = {46.0f, 0.18f, 1.20f, 0.0f};
    model->SetSceneLighting(lighting);

    SceneFog fog{};
    fog.params = {0.0f, 0.0f, 1.0f, 0.0f};
    model->SetSceneFog(fog);

    if (ctx_->rendering.dxCommon != nullptr) {
        ctx_->rendering.dxCommon->SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }

    model->PrepareSkinning({playerModelId_});
    model->PreDraw();
    player_.Draw(model, camera_, true, true, 0.92f);
    model->PostDraw();
}

void GameOverScene::DrawOverlay(float screenWidth, float screenHeight) {
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f, 0.08f));
    DrawDefeatTitle(screenWidth, screenHeight);

    if (state_ == State::DefeatIntro) {
        return;
    }
    if (state_ == State::DifficultyPrompt || state_ == State::DifficultyDrop) {
        DrawDifficultyPrompt(screenWidth, screenHeight);
    }
    if (state_ == State::Menu || state_ == State::RetryRise) {
        DrawMenu(screenWidth, screenHeight);
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

void GameOverScene::DrawDifficultyPrompt(float screenWidth, float screenHeight) {
    const float panelW = screenWidth - std::clamp(screenWidth * 0.10f, 96.0f, 150.0f);
    const float panelH = screenHeight - std::clamp(screenHeight * 0.16f, 96.0f, 150.0f);
    const float panelX = (screenWidth - panelW) * 0.5f;
    const float panelY = (screenHeight - panelH) * 0.5f;

    DrawRect(panelX + 14.0f, panelY + 16.0f, panelW, panelH,
             Color(0.0f, 0.0f, 0.0f, 0.44f));
    DrawRect(panelX, panelY, panelW, panelH, Color(0.018f, 0.020f, 0.024f, 0.96f));
    DrawFrame(panelX, panelY, panelW, panelH, 3.0f,
              Color(0.90f, 0.70f, 0.32f, 0.78f));

    const float messageScale =
        std::min(1.22f, (panelW * 0.64f) /
                           (std::max)(lowerDifficultyImage_.width, 1.0f));
    const float messageW = lowerDifficultyImage_.width * messageScale;
    DrawImage(lowerDifficultyImage_, panelX + (panelW - messageW) * 0.5f,
              panelY + panelH * 0.17f, messageScale);

    DrawDifficultyGauge(panelX + panelW * 0.13f, panelY + panelH * 0.40f,
                        panelW * 0.74f, std::clamp(panelH * 0.19f, 110.0f, 158.0f),
                        displayedDifficulty_);

    const float buttonW = std::clamp(panelW * 0.20f, 170.0f, 240.0f);
    const float buttonH = std::clamp(panelH * 0.12f, 70.0f, 92.0f);
    const float gap = panelW * 0.10f;
    const float totalW = buttonW * 2.0f + gap;
    const float buttonY = panelY + panelH * 0.72f;
    const float firstX = panelX + (panelW - totalW) * 0.5f;
    const Image *labels[2] = {&yesImage_, &noImage_};
    for (int i = 0; i < 2; ++i) {
        const float x = firstX + static_cast<float>(i) * (buttonW + gap);
        const bool selected = i == promptIndex_;
        DrawRect(x, buttonY, buttonW, buttonH,
                 selected ? Color(0.18f, 0.13f, 0.055f, 0.98f)
                          : Color(0.040f, 0.046f, 0.058f, 0.92f));
        DrawFrame(x, buttonY, buttonW, buttonH, 2.0f,
                  selected ? Color(1.0f, 0.78f, 0.34f, 0.96f)
                           : Color(0.62f, 0.66f, 0.72f, 0.38f));
        const Image &label = *labels[i];
        const float scale =
            (std::min)({1.0f, (buttonW * 0.70f) / (std::max)(label.width, 1.0f),
                        (buttonH * 0.62f) / (std::max)(label.height, 1.0f)});
        DrawImage(label, x + (buttonW - label.width * scale) * 0.5f,
                  buttonY + (buttonH - label.height * scale) * 0.5f, scale,
                  selected ? 1.0f : 0.82f);
    }
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

void GameOverScene::DrawDifficultyGauge(float x, float y, float w, float h,
                                        float difficulty, float form) {
    form = std::clamp(form, 0.0f, 1.0f);
    const float t = std::clamp(difficulty, 0.0f, 9.0f) / 9.0f;
    const float formedW = w * form;
    if (formedW <= 0.0f) {
        return;
    }
    const float leftTrim = 3.0f;
    const XMFLOAT4 gold = Color(0.86f, 0.61f - 0.22f * t, 0.21f, 0.94f);
    const XMFLOAT4 brightGold = Color(1.0f, 0.86f - 0.32f * t, 0.38f, 0.92f);

    const float shadowW = formedW + 36.0f * form;
    const float shadowTrim = (std::min)(leftTrim, shadowW);
    const float shadowUvLeft = shadowW > 0.0f ? (shadowTrim / shadowW) * form : 0.0f;
    DrawTextureRect(triangleMaskImage_.textureId, x - 18.0f + shadowTrim,
                    y - 16.0f, shadowW - shadowTrim, h + 32.0f,
                    Color(0.0f, 0.0f, 0.0f, 0.46f * form),
                    form - shadowUvLeft, SpriteBlendMode::PremultipliedMask,
                    shadowUvLeft);

    const float outerW = formedW + 16.0f * form;
    const float outerTrim = (std::min)(leftTrim, outerW);
    const float outerUvLeft = outerW > 0.0f ? (outerTrim / outerW) * form : 0.0f;
    DrawTextureRect(triangleMaskImage_.textureId, x - 8.0f + outerTrim,
                    y - 8.0f, outerW - outerTrim, h + 16.0f,
                    Color(gold.x, gold.y, gold.z, gold.w * form),
                    form - outerUvLeft, SpriteBlendMode::PremultipliedMask,
                    outerUvLeft);

    const float backW = (w - 32.0f) * form;
    const float backTrim = (std::min)(leftTrim, backW);
    const float backUvLeft = backW > 0.0f ? (backTrim / backW) * form : 0.0f;
    DrawTextureRect(triangleMaskImage_.textureId, x + 12.0f + backTrim,
                    y + 14.0f, backW - backTrim, h - 30.0f,
                    Color(0.010f, 0.012f, 0.020f, 0.94f * form),
                    form - backUvLeft, SpriteBlendMode::PremultipliedMask,
                    backUvLeft);

    const float innerW = w - 44.0f;
    const float innerH = h - 40.0f;
    const float innerX = x + 18.0f;
    const float innerY = y + 20.0f;
    if (t > 0.001f) {
        const float fillW = innerW * t;
        const float fillTrim = (std::min)(leftTrim, fillW);
        const float fillUvLeft = fillW > 0.0f ? (fillTrim / fillW) * t : 0.0f;
        DrawTextureRect(triangleGradientImage_.textureId, innerX + fillTrim,
                        innerY, fillW - fillTrim, innerH,
                        Color(1.0f, 1.0f, 1.0f, 0.98f * form),
                        t - fillUvLeft, SpriteBlendMode::Alpha, fillUvLeft);
    }

    for (int i = 1; i < 10; ++i) {
        const float markerT = static_cast<float>(i) / 9.0f;
        const float markerX = innerX + innerW * markerT;
        const float markerH = (std::max)(6.0f, innerH * markerT);
        const float markerY = innerY + innerH - markerH;
        const bool selected =
            std::abs(std::lround(std::clamp(difficulty, 0.0f, 9.0f)) - i) == 0;
        const XMFLOAT4 markerColor =
            selected ? Color(brightGold.x, brightGold.y, brightGold.z,
                             brightGold.w * form)
                     : Color(0.70f, 0.76f, 0.88f, 0.20f * form);
        DrawRect(markerX - 2.0f, markerY, 4.0f, markerH, markerColor);
        DrawRect(markerX - 5.0f, markerY - 5.0f, 10.0f, 4.0f,
                 selected ? markerColor : Color(0.52f, 0.45f, 0.32f, 0.42f * form));
    }
}

void GameOverScene::DrawTitleFade(float screenWidth, float screenHeight) {
    const float t = SmoothStep01(titleFadeTimer_ / kTitleFadeDuration);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f, t));
    if (t < 0.38f) {
        return;
    }
    const float textT = SmoothStep01((t - 0.38f) / 0.42f);
    const float scale =
        std::clamp(screenWidth * 0.50f / (std::max)(gameOverImage_.width, 1.0f),
                   0.44f, 0.86f);
    const float textW = gameOverImage_.width * scale;
    const float textH = gameOverImage_.height * scale;
    DrawImage(gameOverImage_, (screenWidth - textW) * 0.5f,
              (screenHeight - textH) * 0.5f, scale, textT);
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
