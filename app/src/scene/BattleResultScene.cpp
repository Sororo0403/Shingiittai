#include "BattleResultScene.h"
#include "DirectXCommon.h"
#include "GameScene.h"
#include "Input.h"
#include "Material.h"
#include "Model.h"
#include "ModelManager.h"
#include "PostProcessSystem.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TitleScene.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

using namespace DirectX;

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kHandSwingStartSpeed = 0.78f;
constexpr float kHandSwingResetSpeed = 0.32f;
constexpr int kRequiredHandSwings = 3;
constexpr float kHandIdleMenuSeconds = 5.0f;
constexpr float kReturnTitleFadeDuration = 0.42f;
constexpr size_t kRankingControlCount = 2;
constexpr size_t kMaxRankingEntries = 5;
constexpr size_t kRankingDisplayEntries = 3;
const std::filesystem::path kRankingFilePath =
    std::filesystem::path(L"save") / L"battle_result_rankings.txt";

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float SmoothStep01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool IsHandControl(InputControlType controlType) {
    return controlType == InputControlType::Hand;
}

size_t RankingIndex(InputControlType controlType) {
    switch (controlType) {
    case InputControlType::Hand:
        return 1;
    case InputControlType::KeyboardMouse:
    default:
        return 0;
    }
}

const char *RankingKey(InputControlType controlType) {
    switch (controlType) {
    case InputControlType::Hand:
        return "hand";
    case InputControlType::KeyboardMouse:
    default:
        return "keyboard_mouse";
    }
}

int RankingIndexFromKey(const std::string &key) {
    if (key == "keyboard_mouse") {
        return 0;
    }
    if (key == "hand") {
        return 1;
    }
    return -1;
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

uint32_t Hash2D(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return h ^ (h >> 16u);
}

uint32_t CreateResultProceduralTexture(TextureManager *texture, uint32_t width,
                                       uint32_t height,
                                       const XMFLOAT3 &baseColor,
                                       const XMFLOAT3 &accentColor,
                                       uint32_t seed, float grainStrength) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4u);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t h = Hash2D(x / 3u, y / 3u, seed);
            const float noise = static_cast<float>(h & 255u) / 255.0f;
            const float broad =
                static_cast<float>(Hash2D(x / 13u, y / 13u, seed + 17u) &
                                   255u) /
                255.0f;
            const float mid =
                static_cast<float>(Hash2D(x / 7u, y / 7u, seed + 23u) &
                                   255u) /
                255.0f;
            const float remaining = (std::max)(1.0f - grainStrength, 0.0f);
            const float t = std::clamp(noise * grainStrength +
                                           broad * remaining * 0.58f +
                                           mid * remaining * 0.42f,
                                       0.0f, 1.0f);
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

uint32_t CreateResultRustedMetalTexture(TextureManager *texture) {
    return CreateResultProceduralTexture(texture, 512u, 512u,
                                         {0.23f, 0.22f, 0.20f},
                                         {0.70f, 0.30f, 0.12f}, 0x914Au,
                                         0.62f);
}

void ApplyResultEnemyMaterial(ModelManager *modelManager, uint32_t modelId,
                              uint32_t rustTextureId) {
    if (modelManager == nullptr) {
        return;
    }

    Model *model = modelManager->GetModel(modelId);
    if (model == nullptr) {
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

void ResetModelToBindPose(ModelManager *modelManager, uint32_t modelId) {
    if (modelManager == nullptr) {
        return;
    }

    Model *model = modelManager->GetModel(modelId);
    if (model == nullptr) {
        return;
    }

    model->currentAnimation.clear();
    model->animationTime = 0.0f;
    model->isLoop = true;
    model->isPlaying = false;
    model->animationFinished = false;
    modelManager->UpdateAnimation(modelId, 0.0f);
}
} // namespace

BattleResultScene::BattleResultScene(
    ResultKind resultKind, float clearTime,
    const SwordInputCalibration &inputCalibration, float combatDifficulty)
    : resultKind_(resultKind), inputCalibration_(inputCalibration),
      combatDifficulty_(combatDifficulty),
      clearTime_((std::max)(0.0f, clearTime)) {}

void BattleResultScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);
    sceneTime_ = 0.0f;
    handIdleTimer_ = 0.0f;
    returnTitleFadeTimer_ = 0.0f;
    handSwingCount_ = 0;
    handSwingArmed_ = true;
    returnTitleConfirmVisible_ = false;
    returnTitleFadeActive_ = false;
    returnTitleConfirmIndex_ = 1;

    if (IsHandControl(inputCalibration_.controlType)) {
        handController_.SetCalibration(inputCalibration_);
    }

    InitializeWorld();
    if (ctx_->rendering.postProcessSystem != nullptr) {
        ctx_->rendering.postProcessSystem->SetProfile(PostProcessProfile{});
    }

    clearTitle_ = LoadTextureImage(L"app/resources/ui/result/clear_title.png");
    gameClearTitle_ =
        LoadTextureImage(L"app/resources/ui/result/text/game_clear_title.png");
    gameOverTitle_ =
        LoadTextureImage(L"app/resources/ui/result/game_over_title.png");
    clearTimeLabel_ =
        LoadTextureImage(L"app/resources/ui/result/clear_time.png");
    noClearTimeLabel_ =
        LoadTextureImage(L"app/resources/ui/result/no_clear_time.png");
    retryLabel_ = LoadTextureImage(L"app/resources/ui/result/retry.png");
    menuLabel_ = LoadTextureImage(L"app/resources/ui/result/menu.png");
    returnTitleConfirmMessageImage_ = LoadTextureImage(
        L"app/resources/ui/result/text/return_title_confirm_message.png");
    returnTitleConfirmYesImage_ =
        LoadTextureImage(L"app/resources/ui/title/exit_confirm_yes.png");
    returnTitleConfirmNoImage_ =
        LoadTextureImage(L"app/resources/ui/title/exit_confirm_no.png");
    rankingTitle_ =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_title.png");
    currentRecordLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/current_record.png");
    rankingControlLabels_[0] =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_kbm.png");
    rankingControlLabels_[1] =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_hand.png");
    for (int i = 0; i < 10; ++i) {
        digitImages_[static_cast<size_t>(i)] =
            LoadTextureImage(L"app/resources/ui/result/glyphs/char_" +
                             std::to_wstring(i) + L".png");
    }
    colonImage_ = LoadTextureImage(L"app/resources/ui/result/glyphs/char_colon.png");
    dotImage_ = LoadTextureImage(L"app/resources/ui/result/glyphs/char_dot.png");
    dashImage_ = LoadTextureImage(L"app/resources/ui/result/glyphs/char_dash.png");
    secondImage_ = LoadTextureImage(L"app/resources/ui/result/glyphs/char_s.png");

    LoadRankings();
    if (resultKind_ == ResultKind::Clear) {
        RegisterClearRanking();
    }
}

void BattleResultScene::Update() {
    sceneTime_ += ctx_->frame.deltaTime;
    Input *input = ctx_->systems.input;

    if (returnTitleFadeActive_) {
        returnTitleFadeTimer_ =
            (std::min)(returnTitleFadeTimer_ + ctx_->frame.deltaTime,
                       kReturnTitleFadeDuration);
        if (returnTitleFadeTimer_ >= kReturnTitleFadeDuration) {
            sceneManager_->ChangeScene(std::make_unique<TitleScene>());
        }
        return;
    }

    if (returnTitleConfirmVisible_) {
        UpdateReturnTitleConfirm(*input);
        return;
    }

    if (IsHandControl(inputCalibration_.controlType)) {
        UpdateHandResultInput(ctx_->frame.deltaTime);
        return;
    }

    if (resultKind_ == ResultKind::Clear && input->IsKeyTrigger(DIK_SPACE)) {
        returnTitleConfirmVisible_ = true;
        returnTitleConfirmIndex_ = 1;
        return;
    }

    const bool retry =
        input->IsKeyTrigger(DIK_RETURN) ||
        (resultKind_ != ResultKind::Clear && input->IsKeyTrigger(DIK_SPACE)) ||
        input->IsMouseTrigger(0) ||
        (input->IsGamepadConnected() &&
         input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_A));
    if (retry) {
        sceneManager_->ChangeScene(
            std::make_unique<GameScene>(inputCalibration_, combatDifficulty_));
        return;
    }

    const bool menu =
        input->IsKeyTrigger(DIK_TAB) ||
        (input->IsGamepadConnected() &&
         input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_B));
    if (menu) {
        sceneManager_->ChangeScene(std::make_unique<TitleScene>());
    }
}

void BattleResultScene::UpdateHandResultInput(float deltaTime) {
    handController_.Update(deltaTime);

    const float speed =
        (std::max)(handController_.GetMotionSpeed(0),
                   handController_.GetMotionSpeed(1));
    if (handSwingArmed_ && speed >= kHandSwingStartSpeed) {
        ++handSwingCount_;
        handSwingArmed_ = false;
    }
    if (speed <= kHandSwingResetSpeed) {
        handSwingArmed_ = true;
        handIdleTimer_ += deltaTime;
    } else {
        handIdleTimer_ = 0.0f;
    }

    if (handSwingCount_ >= kRequiredHandSwings) {
        sceneManager_->ChangeScene(
            std::make_unique<GameScene>(inputCalibration_, combatDifficulty_));
        return;
    }

    if (handIdleTimer_ >= kHandIdleMenuSeconds) {
        sceneManager_->ChangeScene(std::make_unique<TitleScene>());
    }
}

void BattleResultScene::UpdateReturnTitleConfirm(Input &input) {
    if (input.IsKeyTrigger(DIK_A) || input.IsKeyTrigger(DIK_LEFT)) {
        returnTitleConfirmIndex_ = 0;
    }
    if (input.IsKeyTrigger(DIK_D) || input.IsKeyTrigger(DIK_RIGHT)) {
        returnTitleConfirmIndex_ = 1;
    }

    if (input.IsKeyTrigger(DIK_ESCAPE)) {
        returnTitleConfirmVisible_ = false;
        returnTitleConfirmIndex_ = 1;
        return;
    }

    const bool confirm =
        input.IsKeyTrigger(DIK_RETURN) || input.IsKeyTrigger(DIK_SPACE);
    if (!confirm) {
        return;
    }

    if (returnTitleConfirmIndex_ == 0) {
        returnTitleFadeActive_ = true;
        returnTitleFadeTimer_ = 0.0f;
    } else {
        returnTitleConfirmVisible_ = false;
        returnTitleConfirmIndex_ = 1;
    }
}

void BattleResultScene::Draw() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    UpdateResultCamera(w, h);
    DrawWorld(w, h);

    ctx_->rendering.sprite->PreDraw();
    DrawResultOverlay(w, h);
    ctx_->rendering.sprite->PostDraw();
}

void BattleResultScene::DrawTransparent() {}

BattleResultScene::Image
BattleResultScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width = static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
    return image;
}

void BattleResultScene::InitializeWorld() {
    const float aspect = static_cast<float>(ctx_->systems.winApp->GetWidth()) /
                         static_cast<float>(ctx_->systems.winApp->GetHeight());
    camera_.Initialize(aspect);
    camera_.SetClipRange(0.05f, 160.0f);

    ModelManager *model = ctx_->rendering.model;
    playerModelId_ = model->Load(L"app/resources/models/player/player.glb");
    swordModelId_ = model->Load(L"app/resources/models/player/sword.glb");
    enemyModelId_ = model->Load(L"app/resources/models/boss/boss.gltf");

    if (ctx_->rendering.texture != nullptr) {
        const uint32_t enemyRustTextureId =
            CreateResultRustedMetalTexture(ctx_->rendering.texture);
        ApplyResultEnemyMaterial(model, enemyModelId_, enemyRustTextureId);
    }

    ResetModelToBindPose(model, playerModelId_);
    ResetModelToBindPose(model, enemyModelId_);
}

void BattleResultScene::UpdateResultCamera(float screenWidth,
                                           float screenHeight) {
    camera_.SetAspect(screenWidth / (std::max)(screenHeight, 1.0f));
    const float orbit = sceneTime_ * 0.34f;
    const float radius = 6.2f;
    const XMFLOAT3 target = {0.0f, 1.05f, 0.0f};
    const XMFLOAT3 eye = {std::sinf(orbit) * radius, 2.25f,
                          std::cosf(orbit) * radius};
    camera_.SetPerspectiveFovDeg(45.0f);
    camera_.SetPosition(eye);
    camera_.SetRotation(CameraRotationLookAt(eye, target));
}

void BattleResultScene::DrawWorld(float screenWidth, float screenHeight) {
    (void)screenWidth;
    (void)screenHeight;

    ModelManager *model = ctx_->rendering.model;
    if (model == nullptr) {
        return;
    }

    SceneLighting lighting{};
    lighting.keyLightDirection = {-0.42f, -0.72f, 0.46f};
    lighting.keyLightColor = {1.12f, 1.08f, 1.02f, 1.0f};
    lighting.fillLightDirection = {0.64f, -0.28f, -0.58f};
    lighting.fillLightColor = {0.28f, 0.34f, 0.42f, 0.46f};
    lighting.ambientColor = {0.30f, 0.30f, 0.32f, 1.0f};
    lighting.lightingParams = {48.0f, 0.30f, 1.20f, 0.18f};
    model->SetSceneLighting(lighting);

    SceneFog fog{};
    fog.params = {0.0f, 0.0f, 1.0f, 0.0f};
    model->SetSceneFog(fog);

    ctx_->rendering.dxCommon->SetClearColor(0.06f, 0.065f, 0.075f, 1.0f);

    model->PrepareSkinning({playerModelId_, enemyModelId_});
    model->PreDraw();
    DrawResultModels();
    model->PostDraw();
}

void BattleResultScene::DrawResultModels() {
    ModelManager *model = ctx_->rendering.model;

    if (resultKind_ == ResultKind::Clear) {
        Transform player{};
        player.position = {-0.75f, 0.0f, 0.0f};
        player.rotation = MakeQuat(0.0f, 0.18f, 0.0f);
        player.scale = {1.45f, 1.45f, 1.45f};
        model->Draw(playerModelId_, player, camera_);

        Transform sword{};
        sword.position = {0.95f, 0.42f, 0.0f};
        sword.rotation = MakeQuat(0.0f, 0.0f, 0.0f);
        sword.scale = {3.0f, 3.0f, 3.0f};
        model->Draw(swordModelId_, sword, camera_);
        return;
    }

    Transform sword{};
    sword.position = {-0.95f, 0.42f, 0.0f};
    sword.rotation = MakeQuat(0.0f, 0.0f, 0.0f);
    sword.scale = {3.0f, 3.0f, 3.0f};
    model->Draw(swordModelId_, sword, camera_);

    Transform enemy{};
    enemy.position = {0.72f, 0.0f, 0.0f};
    enemy.rotation = MakeQuat(0.0f, -0.22f, 0.0f);
    enemy.scale = {1.0f, 1.0f, 1.0f};
    model->Draw(enemyModelId_, enemy, camera_);
}

void BattleResultScene::DrawResultOverlay(float screenWidth,
                                          float screenHeight) {
    const float pulse = 0.5f + 0.5f * std::sin(sceneTime_ * 2.0f);
    const XMFLOAT4 wash =
        resultKind_ == ResultKind::Clear
            ? Color(0.01f, 0.025f + pulse * 0.010f, 0.030f, 0.54f)
            : Color(0.055f + pulse * 0.010f, 0.012f, 0.014f, 0.62f);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight, wash);
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight * 0.18f,
             Color(0.0f, 0.0f, 0.0f, 0.22f));
    DrawRect(0.0f, screenHeight * 0.82f, screenWidth, screenHeight * 0.18f,
             Color(0.0f, 0.0f, 0.0f, 0.42f));

    if (resultKind_ == ResultKind::Clear) {
        DrawClear(screenWidth, screenHeight);
    } else {
        DrawGameOver(screenWidth, screenHeight);
    }
    DrawRankingPanel(screenWidth, screenHeight);
    DrawHandInputStatus(screenWidth, screenHeight);
    if (returnTitleConfirmVisible_) {
        DrawReturnTitleConfirmWindow(screenWidth, screenHeight);
    }
    if (returnTitleFadeActive_) {
        const float alpha =
            SmoothStep01(returnTitleFadeTimer_ / kReturnTitleFadeDuration);
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                 Color(0.0f, 0.0f, 0.0f, alpha));
    }
}

void BattleResultScene::DrawClear(float screenWidth, float screenHeight) {
    const Image &title =
        gameClearTitle_.textureId != 0 ? gameClearTitle_ : clearTitle_;
    const float titleScale =
        std::clamp(screenWidth * 0.34f / (std::max)(title.width, 1.0f), 0.54f,
                   1.0f);
    DrawImage(title, screenWidth * 0.050f, screenHeight * 0.026f, titleScale);

    const float labelScale =
        std::clamp(screenWidth * 0.16f / clearTimeLabel_.width, 0.48f, 0.72f);
    DrawImage(clearTimeLabel_, screenWidth * 0.076f, screenHeight * 0.876f,
              labelScale);
    const float recordScale = std::clamp(
        screenWidth * 0.10f / (std::max)(currentRecordLabel_.width, 1.0f),
        0.42f, 0.62f);
    DrawImage(currentRecordLabel_, screenWidth * 0.078f,
              screenHeight * 0.824f, recordScale, 0.92f);
    DrawTextLine(FormatTime(clearTime_), screenWidth * 0.285f,
                 screenHeight * 0.886f, 0.82f);

    DrawImage(retryLabel_, screenWidth * 0.63f, screenHeight * 0.894f, 0.66f);
    DrawImage(menuLabel_, screenWidth * 0.79f, screenHeight * 0.894f, 0.66f);
}

void BattleResultScene::DrawGameOver(float screenWidth, float screenHeight) {
    const float titleScale =
        std::clamp(screenWidth * 0.34f / gameOverTitle_.width, 0.62f, 1.05f);
    DrawImage(gameOverTitle_, screenWidth * 0.048f, screenHeight * 0.032f,
              titleScale);
    DrawImage(noClearTimeLabel_, screenWidth * 0.070f, screenHeight * 0.872f,
              0.70f);
    DrawImage(retryLabel_, screenWidth * 0.63f, screenHeight * 0.894f, 0.66f);
    DrawImage(menuLabel_, screenWidth * 0.79f, screenHeight * 0.894f, 0.66f);
}

void BattleResultScene::DrawRankingPanel(float screenWidth,
                                         float screenHeight) {
    const float panelW = std::clamp(screenWidth * 0.34f, 390.0f, 560.0f);
    const float panelH = std::clamp(screenHeight * 0.58f, 420.0f, 540.0f);
    const float panelX = screenWidth - panelW - screenWidth * 0.050f;
    const float panelY = screenHeight * 0.185f;
    const float edge = resultKind_ == ResultKind::Clear ? 0.86f : 0.62f;

    DrawRect(panelX + 10.0f, panelY + 12.0f, panelW, panelH,
             Color(0.0f, 0.0f, 0.0f, 0.34f));
    DrawRect(panelX, panelY, panelW, panelH,
             Color(0.018f, 0.021f, 0.027f, 0.86f));
    DrawFrame(panelX, panelY, panelW, panelH, 2.0f,
              resultKind_ == ResultKind::Clear
                  ? Color(0.95f, 0.72f, 0.28f, edge)
                  : Color(0.76f, 0.16f, 0.14f, edge));

    const float titleScale = std::clamp(
        panelW * 0.38f / (std::max)(rankingTitle_.width, 1.0f), 0.56f, 0.82f);
    DrawImage(rankingTitle_, panelX + 24.0f, panelY + 20.0f, titleScale,
              0.96f);

    const float sectionTop = panelY + 82.0f;
    const float sectionH =
        (panelH - 108.0f) / static_cast<float>(kRankingControlCount);
    const size_t currentIndex = RankingIndex(inputCalibration_.controlType);
    for (size_t control = 0; control < kRankingControlCount; ++control) {
        const float y = sectionTop + sectionH * static_cast<float>(control);
        const bool current = control == currentIndex;
        const XMFLOAT4 sectionColor =
            current ? Color(0.16f, 0.115f, 0.040f, 0.68f)
                    : Color(0.04f, 0.046f, 0.056f, 0.52f);
        DrawRect(panelX + 18.0f, y, panelW - 36.0f, sectionH - 10.0f,
                 sectionColor);
        DrawRect(panelX + 18.0f, y, 3.0f, sectionH - 10.0f,
                 current ? Color(1.0f, 0.78f, 0.30f, 0.90f)
                         : Color(0.55f, 0.62f, 0.70f, 0.24f));

        const Image &label = rankingControlLabels_[control];
        const float labelScale =
            std::clamp((panelW * 0.32f) / (std::max)(label.width, 1.0f),
                       0.42f, 0.66f);
        DrawImage(label, panelX + 34.0f, y + 12.0f, labelScale,
                  current ? 1.0f : 0.78f);

        const std::vector<float> &board = rankings_[control];
        for (size_t rank = 0; rank < kRankingDisplayEntries; ++rank) {
            const float rowX = panelX + panelW * 0.48f;
            const float rowY = y + 10.0f + static_cast<float>(rank) * 30.0f;
            std::string line = std::to_string(rank + 1) + " ";
            line += rank < board.size() ? FormatTime(board[rank])
                                        : "--:--.--s";
            DrawTextLineLeft(line, rowX, rowY, 0.46f,
                             current ? 0.98f : 0.74f);
        }
    }
}

void BattleResultScene::DrawHandInputStatus(float screenWidth,
                                            float screenHeight) {
    if (!IsHandControl(inputCalibration_.controlType)) {
        return;
    }

    const float unit = 52.0f;
    const float gap = 18.0f;
    const float startX =
        (screenWidth - unit * 3.0f - gap * 2.0f) * 0.5f;
    const float swingY = screenHeight * 0.805f;
    for (int i = 0; i < kRequiredHandSwings; ++i) {
        DrawRect(startX + static_cast<float>(i) * (unit + gap), swingY, unit,
                 10.0f,
                 i < handSwingCount_ ? Color(0.12f, 0.78f, 0.36f, 1.0f)
                                     : Color(0.20f, 0.24f, 0.28f, 1.0f));
    }

    const float barWidth = screenWidth * 0.34f;
    const float barX = (screenWidth - barWidth) * 0.5f;
    const float barY = screenHeight * 0.845f;
    const float progress =
        (std::min)(handIdleTimer_ / kHandIdleMenuSeconds, 1.0f);
    DrawRect(barX, barY, barWidth, 8.0f, Color(0.16f, 0.18f, 0.20f, 0.90f));
    DrawRect(barX, barY, barWidth * progress, 8.0f,
             Color(0.95f, 0.72f, 0.18f, 0.95f));
}

void BattleResultScene::DrawReturnTitleConfirmWindow(float screenWidth,
                                                     float screenHeight) {
    DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
             Color(0.0f, 0.0f, 0.0f, 0.54f));

    const float panelW = std::clamp(screenWidth * 0.50f, 520.0f, 760.0f);
    const float panelH = std::clamp(screenHeight * 0.30f, 240.0f, 320.0f);
    const float panelX = (screenWidth - panelW) * 0.5f;
    const float panelY = (screenHeight - panelH) * 0.5f;
    const float edge = 3.0f;

    DrawRect(panelX + 10.0f, panelY + 12.0f, panelW, panelH,
             Color(0.0f, 0.0f, 0.0f, 0.36f));
    DrawRect(panelX, panelY, panelW, panelH,
             Color(0.018f, 0.020f, 0.026f, 0.96f));
    DrawFrame(panelX, panelY, panelW, panelH, edge,
              Color(0.92f, 0.68f, 0.28f, 0.74f));

    const float messageScale =
        (std::min)(1.0f, (panelW * 0.78f) /
                             (std::max)(returnTitleConfirmMessageImage_.width,
                                        1.0f));
    const float messageW = returnTitleConfirmMessageImage_.width * messageScale;
    const float messageH = returnTitleConfirmMessageImage_.height * messageScale;
    DrawImage(returnTitleConfirmMessageImage_,
              panelX + (panelW - messageW) * 0.5f,
              panelY + panelH * 0.26f - messageH * 0.5f, messageScale);

    const float buttonW = std::clamp(panelW * 0.24f, 130.0f, 176.0f);
    const float buttonH = std::clamp(panelH * 0.23f, 58.0f, 76.0f);
    const float buttonGap = panelW * 0.08f;
    const float totalButtonW = buttonW * 2.0f + buttonGap;
    const float buttonY = panelY + panelH * 0.61f;
    const float firstButtonX = panelX + (panelW - totalButtonW) * 0.5f;
    const Image *labels[2] = {&returnTitleConfirmYesImage_,
                              &returnTitleConfirmNoImage_};

    for (int i = 0; i < 2; ++i) {
        const float x =
            firstButtonX + static_cast<float>(i) * (buttonW + buttonGap);
        const bool selected = i == returnTitleConfirmIndex_;
        const XMFLOAT4 body =
            selected ? Color(0.18f, 0.13f, 0.055f, 0.98f)
                     : Color(0.040f, 0.046f, 0.058f, 0.92f);
        const XMFLOAT4 line =
            selected ? Color(1.0f, 0.78f, 0.34f, 0.96f)
                     : Color(0.62f, 0.66f, 0.72f, 0.38f);

        DrawRect(x, buttonY, buttonW, buttonH, body);
        DrawFrame(x, buttonY, buttonW, buttonH, 2.0f, line);

        const Image &label = *labels[i];
        const float labelScale =
            (std::min)({1.0f,
                        (buttonH * 0.68f) / (std::max)(label.height, 1.0f),
                        (buttonW * 0.86f) / (std::max)(label.width, 1.0f)});
        const float labelW = label.width * labelScale;
        const float labelH = label.height * labelScale;
        DrawImage(label, x + (buttonW - labelW) * 0.5f,
                  buttonY + (buttonH - labelH) * 0.5f, labelScale,
                  selected ? 1.0f : 0.82f);
    }
}

void BattleResultScene::DrawImage(const Image &image, float x, float y,
                                  float scale, float alpha) {
    if (image.width <= 0.0f || image.height <= 0.0f) {
        return;
    }

    Sprite sprite{};
    sprite.textureId = image.textureId;
    sprite.position = {x, y};
    sprite.size = {image.width * scale, image.height * scale};
    sprite.color = {1.0f, 1.0f, 1.0f, alpha};
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void BattleResultScene::DrawRect(float x, float y, float w, float h,
                                 const XMFLOAT4 &color) {
    Sprite sprite{};
    sprite.textureId = 0;
    sprite.position = {x, y};
    sprite.size = {w, h};
    sprite.color = color;
    ctx_->rendering.sprite->DrawSprite(sprite);
}

void BattleResultScene::DrawFrame(float x, float y, float w, float h,
                                  float thickness, const XMFLOAT4 &color) {
    DrawRect(x, y, w, thickness, color);
    DrawRect(x, y + h - thickness, w, thickness, color);
    DrawRect(x, y, thickness, h, color);
    DrawRect(x + w - thickness, y, thickness, h, color);
}

void BattleResultScene::DrawTextLine(const std::string &text, float centerX,
                                     float y, float scale, float alpha) {
    float x = centerX - MeasureTextLine(text, scale) * 0.5f;
    DrawTextLineLeft(text, x, y, scale, alpha);
}

void BattleResultScene::DrawTextLineLeft(const std::string &text, float x,
                                         float y, float scale, float alpha) {
    for (char c : text) {
        if (c == ' ') {
            x += 18.0f * scale;
            continue;
        }
        const Image *image = FindCharImage(c);
        if (image == nullptr) {
            continue;
        }
        DrawImage(*image, x, y, scale, alpha);
        x += image->width * scale - 4.0f * scale;
    }
}

float BattleResultScene::MeasureTextLine(const std::string &text,
                                         float scale) const {
    float width = 0.0f;
    for (char c : text) {
        if (c == ' ') {
            width += 18.0f * scale;
            continue;
        }
        const Image *image = FindCharImage(c);
        if (image != nullptr) {
            width += image->width * scale - 4.0f * scale;
        }
    }
    return (std::max)(0.0f, width);
}

const BattleResultScene::Image *BattleResultScene::FindCharImage(char c) const {
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

std::string BattleResultScene::FormatTime(float seconds) const {
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

void BattleResultScene::LoadRankings() {
    for (std::vector<float> &board : rankings_) {
        board.clear();
    }

    std::ifstream file(kRankingFilePath);
    if (!file) {
        return;
    }

    std::string key;
    float seconds = 0.0f;
    while (file >> key >> seconds) {
        const int index = RankingIndexFromKey(key);
        if (index < 0 || !std::isfinite(seconds) || seconds < 0.0f) {
            continue;
        }
        rankings_[static_cast<size_t>(index)].push_back(seconds);
    }

    for (std::vector<float> &board : rankings_) {
        std::sort(board.begin(), board.end());
        if (board.size() > kMaxRankingEntries) {
            board.resize(kMaxRankingEntries);
        }
    }
}

void BattleResultScene::SaveRankings() const {
    const std::filesystem::path directory = kRankingFilePath.parent_path();
    if (!directory.empty()) {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
    }

    std::ofstream file(kRankingFilePath, std::ios::trunc);
    if (!file) {
        return;
    }

    file << std::fixed << std::setprecision(2);
    const std::array<InputControlType, kRankingControlCount> controlTypes = {
        InputControlType::KeyboardMouse, InputControlType::Hand};
    for (size_t index = 0; index < controlTypes.size(); ++index) {
        for (float seconds : rankings_[index]) {
            file << RankingKey(controlTypes[index]) << ' ' << seconds << '\n';
        }
    }
}

void BattleResultScene::RegisterClearRanking() {
    std::vector<float> &board =
        rankings_[RankingIndex(inputCalibration_.controlType)];
    board.push_back(clearTime_);
    std::sort(board.begin(), board.end());
    if (board.size() > kMaxRankingEntries) {
        board.resize(kMaxRankingEntries);
    }
    SaveRankings();
}
