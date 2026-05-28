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
#include <array>
#include <cmath>
#include <memory>
#include <vector>

using namespace DirectX;

namespace {
constexpr float kDifficultyDropDuration = 0.82f;
constexpr float kDifficultyDropHoldDuration = 1.35f;
constexpr float kDefeatIntroDuration = 2.35f;
constexpr float kDefeatIntroBlackFadeDuration = 0.18f;
constexpr float kPromptOpenDuration = 0.28f;
constexpr float kPromptCloseDuration = 0.22f;
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
constexpr size_t kSpotlightDustCount = 150;
constexpr float kRetryRiseDuration = 1.18f;
constexpr int kGameOverLetterCount = 8;
constexpr float kGameOverLetterStart = 0.54f;
constexpr float kGameOverLetterInterval = 0.13f;
constexpr float kGameOverLetterFadeDuration = 0.18f;
constexpr float kTitleFadeDuration =
    kGameOverLetterStart +
    kGameOverLetterInterval * static_cast<float>(kGameOverLetterCount - 1) +
    kGameOverLetterFadeDuration + 0.62f;

std::array<int, 10> g_defeatsByDifficulty{};

int DifficultyBucket(float difficulty) {
    return std::clamp(static_cast<int>(std::lround(difficulty)), 0, 9);
}

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

XMFLOAT4 RotationFromZAxisTo(const XMFLOAT3 &direction) {
    XMVECTOR to = XMVector3Normalize(XMLoadFloat3(&direction));
    XMVECTOR from = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    float dot = XMVectorGetX(XMVector3Dot(from, to));
    dot = std::clamp(dot, -1.0f, 1.0f);

    if (dot > 0.9995f) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    if (dot < -0.9995f) {
        XMFLOAT4 rotation{};
        XMStoreFloat4(&rotation,
                      XMQuaternionRotationAxis(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f),
                                               XM_PI));
        return rotation;
    }

    XMVECTOR axis = XMVector3Cross(from, to);
    XMVECTOR rotation = XMVectorSet(XMVectorGetX(axis), XMVectorGetY(axis),
                                   XMVectorGetZ(axis), 1.0f + dot);
    XMFLOAT4 result{};
    XMStoreFloat4(&result, XMQuaternionNormalize(rotation));
    return result;
}

XMFLOAT4 MakeQuat(float pitch, float yaw, float roll) {
    XMFLOAT4 result{};
    XMStoreFloat4(&result, XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
    return result;
}

float Hash01(uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return static_cast<float>(value & 0x00ffffffu) /
           static_cast<float>(0x00ffffffu);
}

XMFLOAT3 NormalizeVec3(const XMFLOAT3 &value, const XMFLOAT3 &fallback) {
    const float len =
        std::sqrtf(value.x * value.x + value.y * value.y + value.z * value.z);
    if (len <= 0.0001f) {
        return fallback;
    }
    return {value.x / len, value.y / len, value.z / len};
}

XMFLOAT3 CrossVec3(const XMFLOAT3 &a, const XMFLOAT3 &b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
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

void GameOverScene::ResetDefeatCounts() { g_defeatsByDifficulty.fill(0); }

void GameOverScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    introTimer_ = 0.0f;
    state_ = State::DefeatIntro;
    const int difficultyBucket = DifficultyBucket(combatDifficulty_);
    offerDifficultyDrop_ = ++g_defeatsByDifficulty[static_cast<size_t>(
                               difficultyBucket)] >= 3;
    promptIndex_ = 1;
    menuIndex_ = 0;
    promptWindowTimer_ = 0.0f;
    promptCloseTimer_ = 0.0f;
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

    Material dustMaterial{};
    dustMaterial.color = {2.8f, 2.18f, 1.12f, 0.32f};
    dustMaterial.enableTexture = 1;
    dustMaterial.baseColorTextureId = spotlightDustTextureId_;
    dustMaterial.reflectionStrength = 0.0f;
    dustMaterial.reflectionFresnelStrength = 0.0f;
    dustMaterial.blendMode = static_cast<int32_t>(BlendMode::Transparent);
    dustMaterial.cullMode = static_cast<int32_t>(MaterialCullMode::None);
    dustMaterial.depthWrite = 0;
    dustMaterial.roughness = 1.0f;
    dustMaterial.metallic = 0.0f;
    spotlightDustModelId_ =
        model->CreatePlane(spotlightDustTextureId_, dustMaterial);
    InitializeSpotlightDust();

    CreateTextImages();
}

void GameOverScene::Update() {
    const float deltaTime = ctx_->frame.deltaTime;
    sceneTime_ += deltaTime;

    switch (state_) {
    case State::DefeatIntro:
        if (IsAdvancePressed(ctx_->systems.input)) {
            FinishDefeatIntro(true);
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
    case State::DifficultyPrompt:
        UpdateDifficultyPrompt(deltaTime);
        break;
    case State::DifficultyDrop:
        UpdateDifficultyDrop(deltaTime);
        break;
    case State::DifficultyPromptClose:
        UpdateDifficultyPromptClose(deltaTime);
        break;
    case State::Menu:
        UpdateMenu();
        break;
    case State::RetryRise:
        if (IsAdvancePressed(ctx_->systems.input)) {
            sceneManager_->ChangeScene(
                std::make_unique<GameScene>(inputCalibration_, combatDifficulty_));
            return;
        }
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
    triangleMaskImage_.textureId =
        CreateTriangleTexture(ctx_->rendering.texture, false);
    triangleMaskImage_.width = 512.0f;
    triangleMaskImage_.height = 128.0f;
    triangleGradientImage_.textureId =
        CreateTriangleTexture(ctx_->rendering.texture, true);
    triangleGradientImage_.width = 512.0f;
    triangleGradientImage_.height = 128.0f;
}

void GameOverScene::FinishDefeatIntro(bool skipPromptOpen) {
    introTimer_ = kDefeatIntroDuration;
    player_.LockPosition(kPlayerDefeatPosition);
    player_.SetYaw(0.65f);
    player_.SetDefeatPoseRatio(1.0f);
    promptWindowTimer_ = skipPromptOpen ? kPromptOpenDuration : 0.0f;
    if (offerDifficultyDrop_) {
        state_ = State::DifficultyPrompt;
    } else {
        state_ = State::Menu;
        menuIndex_ = 0;
    }
}

void GameOverScene::UpdateDifficultyPrompt(float deltaTime) {
    Input *input = ctx_->systems.input;
    const bool confirm = IsAdvancePressed(input);
    if (promptWindowTimer_ < kPromptOpenDuration) {
        if (confirm) {
            promptWindowTimer_ = kPromptOpenDuration;
        } else {
            promptWindowTimer_ =
                (std::min)(promptWindowTimer_ + deltaTime, kPromptOpenDuration);
        }
        return;
    }

    if (input->IsKeyTrigger(DIK_A) || input->IsKeyTrigger(DIK_LEFT)) {
        promptIndex_ = 0;
    }
    if (input->IsKeyTrigger(DIK_D) || input->IsKeyTrigger(DIK_RIGHT)) {
        promptIndex_ = 1;
    }

    if (!confirm) {
        return;
    }

    if (promptIndex_ == 0) {
        difficultyBeforeDrop_ = combatDifficulty_;
        combatDifficulty_ = std::clamp(combatDifficulty_ - 1.0f, 0.0f, 9.0f);
        difficultyDropTimer_ = 0.0f;
        state_ = State::DifficultyDrop;
    } else {
        promptCloseTimer_ = 0.0f;
        state_ = State::DifficultyPromptClose;
    }
}

void GameOverScene::UpdateDifficultyDrop(float deltaTime) {
    if (IsAdvancePressed(ctx_->systems.input)) {
        displayedDifficulty_ = combatDifficulty_;
        promptCloseTimer_ = kPromptCloseDuration;
        state_ = State::DifficultyPromptClose;
        return;
    }
    difficultyDropTimer_ =
        (std::min)(difficultyDropTimer_ + deltaTime,
                   kDifficultyDropDuration + kDifficultyDropHoldDuration);
    const float t = SmoothStep01(difficultyDropTimer_ / kDifficultyDropDuration);
    displayedDifficulty_ =
        difficultyBeforeDrop_ + (combatDifficulty_ - difficultyBeforeDrop_) * t;
    if (difficultyDropTimer_ >= kDifficultyDropDuration + kDifficultyDropHoldDuration) {
        displayedDifficulty_ = combatDifficulty_;
        promptCloseTimer_ = 0.0f;
        state_ = State::DifficultyPromptClose;
    }
}

void GameOverScene::UpdateDifficultyPromptClose(float deltaTime) {
    if (IsAdvancePressed(ctx_->systems.input)) {
        state_ = State::Menu;
        menuIndex_ = 0;
        return;
    }

    promptCloseTimer_ =
        (std::min)(promptCloseTimer_ + deltaTime, kPromptCloseDuration);
    if (promptCloseTimer_ >= kPromptCloseDuration) {
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

    const bool confirm = IsAdvancePressed(input);
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
    const float rawT = std::clamp(retryRiseTimer_ / kRetryRiseDuration, 0.0f, 1.0f);
    constexpr float kTrembleEnd = 0.28f;
    constexpr float kSnapEnd = 0.58f;
    const float snapT = std::clamp((rawT - kTrembleEnd) / (kSnapEnd - kTrembleEnd),
                                   0.0f, 1.0f);
    const float riseT =
        std::clamp(1.0f - std::powf(1.0f - snapT, 4.2f), 0.0f, 1.0f);
    const float defeatPose = 1.0f - riseT;
    const float defeatPoseEased = SmoothStep01(defeatPose);
    constexpr float kDefeatVisualGroundOffset = 0.48f;
    const float tremble =
        rawT < kTrembleEnd ? (1.0f - rawT / kTrembleEnd) : 0.0f;
    const float shakeX = std::sinf(retryRiseTimer_ * 92.0f) * 0.030f * tremble;
    const float shakeZ = std::cosf(retryRiseTimer_ * 76.0f) * 0.018f * tremble;
    const float shakeYaw = std::sinf(retryRiseTimer_ * 84.0f) * 0.070f * tremble;
    player_.LockPosition({kPlayerDefeatPosition.x + shakeX,
                          kPlayerDefeatPosition.y -
                              kDefeatVisualGroundOffset * (1.0f - defeatPoseEased),
                          kPlayerDefeatPosition.z + shakeZ});
    player_.SetYaw(0.65f + shakeYaw);
    player_.SetDefeatPoseRatio(defeatPose);
    if (retryRiseTimer_ >= kRetryRiseDuration) {
        sceneManager_->ChangeScene(
            std::make_unique<GameScene>(inputCalibration_, combatDifficulty_));
    }
}

void GameOverScene::UpdateTitleFade(float deltaTime) {
    titleFadeTimer_ = (std::min)(titleFadeTimer_ + deltaTime, kTitleFadeDuration);
    if (titleFadeTimer_ >= kTitleFadeDuration) {
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
    lighting.spotLight.colorIntensity = {1.0f, 0.86f, 0.58f, 11.50f};
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
        floor.scale = {8.6f, 8.6f, 1.0f};
        model->Draw(gameOverFloorModelId_, floor, camera_);
    }
    if (spotlightPoolModelId_ != 0) {
        Transform pool{};
        pool.position = {kDefeatSpotlightTarget.x, kPlayerDefeatPosition.y - 0.45f,
                         kDefeatSpotlightTarget.z};
        pool.rotation = MakeQuat(-XM_PIDIV2, 0.0f, 0.0f);
        pool.scale = {2.65f, 2.65f, 1.0f};
        model->Draw(spotlightPoolModelId_, pool, camera_);
    }
    player_.Draw(model, camera_, true, true, 0.92f);
    DrawSpotlightDust();
    model->PostDraw();
}

void GameOverScene::InitializeSpotlightDust() {
    spotlightDust_.clear();
    spotlightDust_.reserve(kSpotlightDustCount);

    for (size_t i = 0; i < kSpotlightDustCount; ++i) {
        const uint32_t seed = static_cast<uint32_t>(i) * 977u + 0x6d2bu;
        SpotlightDust dust{};
        dust.path = 0.08f + Hash01(seed + 1u) * 0.84f;
        dust.radius = std::sqrtf(Hash01(seed + 2u));
        dust.angle = Hash01(seed + 3u) * XM_2PI;
        dust.phase = Hash01(seed + 4u) * XM_2PI;
        dust.driftSpeed = 0.010f + Hash01(seed + 5u) * 0.024f;
        dust.size = 0.026f + Hash01(seed + 6u) * 0.068f;
        dust.alpha = 0.30f + Hash01(seed + 7u) * 0.55f;
        spotlightDust_.push_back(dust);
    }
}

void GameOverScene::DrawSpotlightDust() {
    ModelManager *model = ctx_->rendering.model;
    if (model == nullptr || spotlightDustModelId_ == 0 ||
        spotlightDust_.empty()) {
        return;
    }

    Model *dustModel = model->GetModel(spotlightDustModelId_);
    if (dustModel == nullptr || dustModel->subMeshes.empty()) {
        return;
    }

    const uint32_t materialId = dustModel->subMeshes.front().materialId;
    Material dustMaterial = model->GetMaterial(materialId);

    const XMFLOAT3 lightToTarget{
        kDefeatSpotlightTarget.x - kDefeatSpotlightPosition.x,
        kDefeatSpotlightTarget.y - kDefeatSpotlightPosition.y,
        kDefeatSpotlightTarget.z - kDefeatSpotlightPosition.z,
    };
    const float beamLength =
        std::sqrtf(lightToTarget.x * lightToTarget.x +
                   lightToTarget.y * lightToTarget.y +
                   lightToTarget.z * lightToTarget.z);
    if (beamLength <= 0.001f) {
        return;
    }

    const XMFLOAT3 beamDir = NormalizeVec3(lightToTarget, {0.0f, -1.0f, 0.0f});
    XMFLOAT3 beamRight =
        NormalizeVec3(CrossVec3({0.0f, 1.0f, 0.0f}, beamDir),
                      {1.0f, 0.0f, 0.0f});
    XMFLOAT3 beamUp = NormalizeVec3(CrossVec3(beamDir, beamRight),
                                    {0.0f, 0.0f, 1.0f});
    const XMFLOAT3 cameraPosition = camera_.GetPosition();

    for (const SpotlightDust &dust : spotlightDust_) {
        float path = dust.path + sceneTime_ * dust.driftSpeed;
        path -= std::floor(path);
        path = 0.07f + path * 0.86f;

        const float coneRadius = 0.025f + path * 0.66f;
        const float angle = dust.angle + std::sinf(sceneTime_ * 0.29f + dust.phase) * 0.34f;
        const float radial = coneRadius * dust.radius;
        XMFLOAT3 position{
            kDefeatSpotlightPosition.x + beamDir.x * beamLength * path +
                beamRight.x * std::cosf(angle) * radial +
                beamUp.x * std::sinf(angle) * radial,
            kDefeatSpotlightPosition.y + beamDir.y * beamLength * path +
                beamRight.y * std::cosf(angle) * radial +
                beamUp.y * std::sinf(angle) * radial,
            kDefeatSpotlightPosition.z + beamDir.z * beamLength * path +
                beamRight.z * std::cosf(angle) * radial +
                beamUp.z * std::sinf(angle) * radial,
        };
        position.x += std::sinf(sceneTime_ * 0.41f + dust.phase * 1.7f) * 0.020f;
        position.y += std::sinf(sceneTime_ * 0.35f + dust.phase) * 0.026f;
        position.z += std::cosf(sceneTime_ * 0.38f + dust.phase * 1.3f) * 0.020f;

        const float radialFade = 1.0f - SmoothStep01(dust.radius * 0.74f);
        const float endFade = SmoothStep01((path - 0.08f) / 0.18f) *
                              (1.0f - SmoothStep01((path - 0.76f) / 0.18f));
        const float twinkle = 0.72f + 0.28f * std::sinf(sceneTime_ * 1.7f + dust.phase);
        const float alpha = dust.alpha * radialFade * endFade * twinkle;
        if (alpha <= 0.012f) {
            continue;
        }

        const XMFLOAT3 toCamera{
            cameraPosition.x - position.x,
            cameraPosition.y - position.y,
            cameraPosition.z - position.z,
        };

        dustMaterial.color = {3.8f, 3.0f, 1.45f, alpha};
        model->SetMaterial(materialId, dustMaterial);

        Transform dustTransform{};
        dustTransform.position = position;
        dustTransform.rotation = RotationFromZAxisTo(toCamera);
        const float size = dust.size * (0.72f + path * 0.52f);
        dustTransform.scale = {size, size, 1.0f};
        model->Draw(spotlightDustModelId_, dustTransform, camera_);
    }
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
    if (state_ == State::DifficultyPrompt || state_ == State::DifficultyDrop ||
        state_ == State::DifficultyPromptClose) {
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
    float windowForm = 1.0f;
    if (state_ == State::DifficultyPrompt) {
        windowForm = SmoothStep01(promptWindowTimer_ / kPromptOpenDuration);
    } else if (state_ == State::DifficultyPromptClose) {
        windowForm = 1.0f - SmoothStep01(promptCloseTimer_ / kPromptCloseDuration);
    }
    const float contentAlpha = SmoothStep01((windowForm - 0.35f) / 0.45f);
    const float windowScale = 0.08f + 0.92f * windowForm;

    const float basePanelW =
        screenWidth - std::clamp(screenWidth * 0.10f, 96.0f, 150.0f);
    const float basePanelH =
        screenHeight - std::clamp(screenHeight * 0.16f, 96.0f, 150.0f);
    const float panelW = basePanelW * windowScale;
    const float panelH = basePanelH * windowScale;
    const float panelX = (screenWidth - panelW) * 0.5f;
    const float panelY = (screenHeight - panelH) * 0.5f;

    DrawRect(panelX + 14.0f, panelY + 16.0f, panelW, panelH,
             Color(0.0f, 0.0f, 0.0f, 0.44f * windowForm));
    DrawRect(panelX, panelY, panelW, panelH,
             Color(0.018f, 0.020f, 0.024f, 0.96f * windowForm));
    DrawFrame(panelX, panelY, panelW, panelH, 3.0f,
              Color(0.90f, 0.70f, 0.32f, 0.78f * windowForm));

    const float messageScale =
        std::min(1.22f, (panelW * 0.64f) /
                           (std::max)(lowerDifficultyImage_.width, 1.0f));
    const float messageW = lowerDifficultyImage_.width * messageScale;
    DrawImage(lowerDifficultyImage_, panelX + (panelW - messageW) * 0.5f,
              panelY + panelH * 0.17f, messageScale, contentAlpha);

    DrawDifficultyGauge(panelX + panelW * 0.13f, panelY + panelH * 0.40f,
                        panelW * 0.74f, std::clamp(panelH * 0.19f, 110.0f, 158.0f),
                        displayedDifficulty_, contentAlpha);

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
                 selected ? Color(0.18f, 0.13f, 0.055f, 0.98f * contentAlpha)
                          : Color(0.040f, 0.046f, 0.058f,
                                  0.92f * contentAlpha));
        DrawFrame(x, buttonY, buttonW, buttonH, 2.0f,
                  selected ? Color(1.0f, 0.78f, 0.34f, 0.96f * contentAlpha)
                           : Color(0.62f, 0.66f, 0.72f, 0.38f * contentAlpha));
        const Image &label = *labels[i];
        const float scale =
            (std::min)({1.0f, (buttonW * 0.70f) / (std::max)(label.width, 1.0f),
                        (buttonH * 0.62f) / (std::max)(label.height, 1.0f)});
        DrawImage(label, x + (buttonW - label.width * scale) * 0.5f,
                  buttonY + (buttonH - label.height * scale) * 0.5f, scale,
                  (selected ? 1.0f : 0.82f) * contentAlpha);
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
        const float fillT = (std::min)(t, form);
        const float fillW = innerW * fillT;
        const float fillTrim = (std::min)(leftTrim, fillW);
        const float fillUvLeft =
            fillW > 0.0f ? (fillTrim / fillW) * fillT : 0.0f;
        DrawTextureRect(triangleGradientImage_.textureId, innerX + fillTrim,
                        innerY, fillW - fillTrim, innerH,
                        Color(1.0f, 1.0f, 1.0f, 0.98f * form),
                        fillT - fillUvLeft, SpriteBlendMode::Alpha,
                        fillUvLeft);
    }

    for (int i = 0; i < 10; ++i) {
        const float markerT = static_cast<float>(i) / 9.0f;
        if (markerT > form + 0.015f) {
            continue;
        }
        const float markerX = innerX + innerW * markerT;
        const float markerH = (std::max)(6.0f, innerH * markerT);
        const float markerY = innerY + innerH - markerH;
        const bool selected =
            std::abs(std::lround(std::clamp(difficulty, 0.0f, 9.0f)) - i) == 0;
        const float markerAlpha = std::clamp((form - markerT) * 8.0f, 0.0f, 1.0f);
        const XMFLOAT4 markerColor =
            selected ? Color(brightGold.x, brightGold.y, brightGold.z,
                             brightGold.w * markerAlpha)
                     : Color(0.70f, 0.76f, 0.88f, 0.20f * markerAlpha);
        if (i != 0) {
            DrawRect(markerX - 2.0f, markerY, 4.0f, markerH, markerColor);
            DrawRect(markerX - 5.0f, markerY - 5.0f, 10.0f, 4.0f,
                     selected ? markerColor
                              : Color(0.52f, 0.45f, 0.32f,
                                      0.42f * markerAlpha));
        }
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
    float cursorX = x;
    for (int i = 0; i < kGameOverLetterCount; ++i) {
        const Image &letter = gameOverLetterImages_[static_cast<size_t>(i)];
        const float appear =
            SmoothStep01((titleFadeTimer_ - kGameOverLetterStart -
                          kGameOverLetterInterval * static_cast<float>(i)) /
                         kGameOverLetterFadeDuration);
        if (appear > 0.001f) {
            const float lift = (1.0f - appear) * 16.0f;
            DrawImage(letter, cursorX, y + lift, scale, appear);
        }
        cursorX += letter.width * scale;
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
