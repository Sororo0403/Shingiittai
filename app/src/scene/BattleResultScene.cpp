#include "BattleResultScene.h"
#include "AppSceneServices.h"
#include "AssetManager.h"
#include "DirectXCommon.h"
#include "GameScene.h"
#include "Input.h"
#include "Material.h"
#include "Model.h"
#include "ModelManager.h"
#include "ParticleEmitterSettings.h"
#include "PostEffectManager.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
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
#include <vector>

using namespace DirectX;

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kHandSwingStartSpeed = 0.78f;
constexpr float kHandSwingResetSpeed = 0.32f;
constexpr int kRequiredHandSwings = 3;
constexpr float kHandIdleMenuSeconds = 5.0f;
constexpr float kReturnTitleFadeDuration = 0.42f;
constexpr size_t kRankingDrawCount = 5;
constexpr float kCelebrationParticleInterval = 0.18f;
constexpr float kTimeRevealDuration = 2.55f;
constexpr float kScoreRevealDuration = 1.55f;
constexpr float kRevealHoldDuration = 0.46f;

XMFLOAT4 Color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

std::filesystem::path RankingPath() {
    return AssetManager::GetAssetRoot() / L"save" / L"ranking.tsv";
}

float SmoothStep01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool IsHandControl(InputControlType controlType) {
    return controlType == InputControlType::Hand;
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
                static_cast<float>(Hash2D(x / 7u, y / 7u, seed + 23u) & 255u) /
                255.0f;
            const float remaining = (std::max)(1.0f - grainStrength, 0.0f);
            const float t =
                std::clamp(noise * grainStrength + broad * remaining * 0.58f +
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
                                         {0.70f, 0.30f, 0.12f}, 0x914Au, 0.62f);
}

uint32_t CreateResultArenaStoneTexture(TextureManager *texture) {
    return CreateResultProceduralTexture(texture, 768u, 768u,
                                         {0.06f, 0.075f, 0.09f},
                                         {0.24f, 0.25f, 0.22f}, 0x51C3u, 0.50f);
}

Material MakeResultArenaMaterial(const XMFLOAT4 &color, bool useTexture,
                                 float reflection, float roughness) {
    Material material{};
    material.color = color;
    material.enableTexture = useTexture ? 1 : 0;
    material.reflectionStrength = reflection;
    material.reflectionFresnelStrength = reflection * 0.40f;
    material.reflectionRoughness = roughness;
    return material;
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
bool IsResultConfirmInput(const Input &input) {
    return input.IsKeyTrigger(DIK_SPACE) || input.IsKeyTrigger(DIK_RETURN) ||
           (input.IsGamepadConnected() &&
            input.IsGamepadButtonTrigger(XINPUT_GAMEPAD_A));
}

bool IsResultLeftInput(const Input &input) {
    return input.IsKeyTrigger(DIK_A) || input.IsKeyTrigger(DIK_LEFT);
}

bool IsResultRightInput(const Input &input) {
    return input.IsKeyTrigger(DIK_D) || input.IsKeyTrigger(DIK_RIGHT);
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
    ResetResultState();

    if (IsHandControl(inputCalibration_.controlType)) {
        handController_.SetCalibration(inputCalibration_);
    }
    if (resultKind_ == ResultKind::Clear) {
        InitializeWorld();
    }
    if (ctx_->rendering.postEffectManager != nullptr) {
        ctx_->rendering.postEffectManager->SetBaseProfile(PostProcessProfile{});
    }
    LoadResultImages();
    ConfigureResultGlyphMetrics();
    if (resultKind_ == ResultKind::Clear) {
        RegisterClearRanking();
    }
}

void BattleResultScene::ResetResultState() {
    sceneTime_ = 0.0f;
    clearRevealTimer_ = 0.0f;
    clearRevealPhase_ = ClearRevealPhase::Time;
    celebrationParticleTimer_ = 0.0f;
    handIdleTimer_ = 0.0f;
    returnTitleFadeTimer_ = 0.0f;
    handSwingCount_ = 0;
    handSwingArmed_ = true;
    returnTitleConfirmVisible_ = false;
    returnTitleFadeActive_ = false;
    clearActionButtonIndex_ = 1;
    returnTitleConfirmIndex_ = 1;
    celebrationParticlesReady_ = false;
}

void BattleResultScene::LoadResultImages() {
    missionCompleteLabel_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/mission_complete.png");
    clearTimeLabel_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/clear_time.png");
    scoreTitleLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/score_title.png");
    currentRecordLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/current_record.png");
    scoreFormulaLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/score_formula.png");
    rankingTitleLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_title.png");
    rankingRankHeaderLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_rank.png");
    rankingScoreHeaderLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_score.png");
    rankingTimeHeaderLabel_ =
        LoadTextureImage(L"app/resources/ui/result/text/ranking_time.png");
    rankingDifficultyHeaderLabel_ = LoadTextureImage(
        L"app/resources/ui/result/text/ranking_difficulty.png");
    controlsKbmClearImage_ = LoadTextureImage(
        L"app/resources/ui/result/mplus/controls_kbm_clear.png");
    controlsKbmGameOverImage_ = LoadTextureImage(
        L"app/resources/ui/result/mplus/controls_kbm_gameover.png");
    controlsHandImage_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/controls_hand.png");
    missionFailedLabel_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/mission_failed.png");
    retryButtonLabel_ =
        LoadTextureImage(L"app/resources/ui/gameover/retry.png");
    titleButtonLabel_ =
        LoadTextureImage(L"app/resources/ui/gameover/title.png");
    returnTitleConfirmMessageImage_ = LoadTextureImage(
        L"app/resources/ui/result/text/return_title_confirm_message.png");
    returnTitleConfirmYesImage_ =
        LoadTextureImage(L"app/resources/ui/title/exit_confirm_yes.png");
    returnTitleConfirmNoImage_ =
        LoadTextureImage(L"app/resources/ui/title/exit_confirm_no.png");
    for (int i = 0; i < 10; ++i) {
        digitImages_[static_cast<size_t>(i)] =
            LoadTextureImage(L"app/resources/ui/result/mplus/glyphs/char_" +
                             std::to_wstring(i) + L".png");
    }
    colonImage_ = LoadTextureImage(
        L"app/resources/ui/result/mplus/glyphs/char_colon.png");
    dotImage_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/glyphs/char_dot.png");
    dashImage_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/glyphs/char_dash.png");
    secondImage_ =
        LoadTextureImage(L"app/resources/ui/result/mplus/glyphs/char_s.png");
}

void BattleResultScene::ConfigureResultGlyphMetrics() {
    const float digitInkLeft[10] = {14.0f, 16.0f, 15.0f, 16.0f, 12.0f,
                                    16.0f, 14.0f, 16.0f, 14.0f, 14.0f};
    const float digitInkRight[10] = {50.0f, 41.0f, 48.0f, 48.0f, 50.0f,
                                     49.0f, 50.0f, 49.0f, 50.0f, 50.0f};
    for (int i = 0; i < 10; ++i) {
        Image &digit = digitImages_[static_cast<size_t>(i)];
        digit.inkLeft = digitInkLeft[i];
        digit.inkRight = digitInkRight[i];
        digit.inkTop =
            (i == 1 || i == 3 || i == 4 || i == 5 || i == 7) ? 21.0f : 20.0f;
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
}

void BattleResultScene::Update() {
    sceneTime_ += ctx_->frame.deltaTime;
    UpdateClearReveal(ctx_->frame.deltaTime);
    UpdateCelebrationParticles(ctx_->frame.deltaTime);
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

    const bool isClear = resultKind_ == ResultKind::Clear;
    if (isClear) {
        UpdateClearActionButtons(*input);
        return;
    }

    const bool retryKey = input->IsKeyTrigger(DIK_SPACE);
    const bool titleKey = input->IsKeyTrigger(DIK_TAB);

    if (retryKey || (input->IsGamepadConnected() &&
                     input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_A))) {
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
        sceneManager_->ChangeScene(
            std::make_unique<GameScene>(inputCalibration_, combatDifficulty_));
        return;
    }

    const bool title = titleKey || input->IsKeyTrigger(DIK_ESCAPE) ||
                       (input->IsGamepadConnected() &&
                        input->IsGamepadButtonTrigger(XINPUT_GAMEPAD_B));
    if (title) {
        BeginReturnTitleConfirm();
        return;
    }
}

void BattleResultScene::UpdateClearActionButtons(Input &input) {
    if (clearRevealPhase_ != ClearRevealPhase::Ranking) {
        const bool skip = IsResultConfirmInput(input);
        if (!skip) {
            return;
        }

        if (clearRevealPhase_ == ClearRevealPhase::Time) {
            if (clearRevealTimer_ < kTimeRevealDuration) {
                clearRevealTimer_ = kTimeRevealDuration;
            } else {
                clearRevealPhase_ = ClearRevealPhase::Score;
                clearRevealTimer_ = 0.0f;
            }
            AppSceneServices::PlayMenuSe(*ctx_,
                                         AppSceneServices::MenuSe::Selected);
            return;
        }

        if (clearRevealTimer_ < kScoreRevealDuration) {
            clearRevealTimer_ = kScoreRevealDuration;
        } else {
            clearRevealPhase_ = ClearRevealPhase::Ranking;
            clearRevealTimer_ = 0.0f;
        }
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
        return;
    }

    if (IsResultLeftInput(input)) {
        if (clearActionButtonIndex_ != 0) {
            clearActionButtonIndex_ = 0;
            AppSceneServices::PlayMenuSe(*ctx_,
                                         AppSceneServices::MenuSe::Select);
        }
    }
    if (IsResultRightInput(input)) {
        if (clearActionButtonIndex_ != 1) {
            clearActionButtonIndex_ = 1;
            AppSceneServices::PlayMenuSe(*ctx_,
                                         AppSceneServices::MenuSe::Select);
        }
    }

    const bool confirm = IsResultConfirmInput(input);
    if (!confirm) {
        return;
    }

    if (clearActionButtonIndex_ == 0) {
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
        sceneManager_->ChangeScene(
            std::make_unique<GameScene>(inputCalibration_, combatDifficulty_));
    } else {
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
        returnTitleFadeActive_ = true;
        returnTitleFadeTimer_ = 0.0f;
    }
}

void BattleResultScene::BeginReturnTitleConfirm() {
    returnTitleConfirmVisible_ = true;
    returnTitleConfirmIndex_ = 1;
    AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Cancel);
}

void BattleResultScene::UpdateReturnTitleConfirm(Input &input) {
    if (input.IsKeyTrigger(DIK_A) || input.IsKeyTrigger(DIK_LEFT)) {
        if (returnTitleConfirmIndex_ != 0) {
            returnTitleConfirmIndex_ = 0;
            AppSceneServices::PlayMenuSe(*ctx_,
                                         AppSceneServices::MenuSe::Select);
        }
    }
    if (input.IsKeyTrigger(DIK_D) || input.IsKeyTrigger(DIK_RIGHT)) {
        if (returnTitleConfirmIndex_ != 1) {
            returnTitleConfirmIndex_ = 1;
            AppSceneServices::PlayMenuSe(*ctx_,
                                         AppSceneServices::MenuSe::Select);
        }
    }

    if (input.IsKeyTrigger(DIK_ESCAPE)) {
        returnTitleConfirmVisible_ = false;
        returnTitleConfirmIndex_ = 1;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Cancel);
        return;
    }

    const bool confirm =
        input.IsKeyTrigger(DIK_RETURN) || input.IsKeyTrigger(DIK_SPACE);
    if (!confirm) {
        return;
    }

    if (returnTitleConfirmIndex_ == 0) {
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Selected);
        returnTitleFadeActive_ = true;
        returnTitleFadeTimer_ = 0.0f;
    } else {
        returnTitleConfirmVisible_ = false;
        returnTitleConfirmIndex_ = 1;
        AppSceneServices::PlayMenuSe(*ctx_, AppSceneServices::MenuSe::Cancel);
    }
}

void BattleResultScene::Draw() {
    const float w = static_cast<float>(ctx_->systems.winApp->GetWidth());
    const float h = static_cast<float>(ctx_->systems.winApp->GetHeight());

    if (resultKind_ == ResultKind::Clear) {
        UpdateResultCamera(w, h);
        DrawWorld(w, h);
    }

    ctx_->rendering.sprite->PreDraw();
    DrawResultOverlay(w, h);
    ctx_->rendering.sprite->PostDraw();
}

void BattleResultScene::DrawTransparent() {}

BattleResultScene::Image
BattleResultScene::LoadTextureImage(const std::wstring &path) {
    Image image{};
    image.textureId = ctx_->rendering.texture->Load(path);
    image.width =
        static_cast<float>(ctx_->rendering.texture->GetWidth(image.textureId));
    image.height =
        static_cast<float>(ctx_->rendering.texture->GetHeight(image.textureId));
    return image;
}

void BattleResultScene::RegisterClearRanking() {
    currentScore_ = ComputeScore(clearTime_, combatDifficulty_);
    currentRank_ = -1;
    LoadRanking();

    rankingEntries_.push_back(
        {currentScore_, combatDifficulty_, clearTime_, true});
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
}

int BattleResultScene::ComputeScore(float clearTime, float difficulty) const {
    const float p = std::clamp(difficulty / 9.0f, 0.0f, 1.0f);
    const float smooth = p * p * (3.0f - 2.0f * p);
    const float effectiveDifficulty =
        std::clamp(difficulty + 0.55f + 0.45f * smooth, 0.0f, 9.0f);
    const float difficultyBonus =
        1.0f + effectiveDifficulty * 0.22f +
        effectiveDifficulty * effectiveDifficulty * 0.035f;
    const float safeClearTime = (std::max)(clearTime, 0.0f);
    const float timeBonus = 180.0f / (safeClearTime + 30.0f);
    return (std::max)(1, static_cast<int>(std::round(1000.0f * difficultyBonus *
                                                     timeBonus)));
}

void BattleResultScene::LoadRanking() {
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

        std::istringstream iss(line);
        RankingEntry entry{};
        if (iss >> entry.score >> entry.difficulty >> entry.clearTime) {
            entry.difficulty = std::clamp(entry.difficulty, 0.0f, 9.0f);
            entry.clearTime = (std::max)(0.0f, entry.clearTime);
            entry.score = ComputeScore(entry.clearTime, entry.difficulty);
            rankingEntries_.push_back(entry);
        }
    }
}

void BattleResultScene::SaveRanking() const {
    const std::filesystem::path path = RankingPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return;
    }

    file << "# score difficulty clear_time\n";
    for (const RankingEntry &entry : rankingEntries_) {
        file << entry.score << '\t' << std::fixed << std::setprecision(1)
             << entry.difficulty << '\t' << std::setprecision(2)
             << entry.clearTime << '\n';
    }
}

void BattleResultScene::UpdateClearReveal(float deltaTime) {
    if (resultKind_ != ResultKind::Clear) {
        return;
    }

    clearRevealTimer_ += deltaTime;
    switch (clearRevealPhase_) {
    case ClearRevealPhase::Time:
        if (clearRevealTimer_ >= kTimeRevealDuration + kRevealHoldDuration) {
            clearRevealPhase_ = ClearRevealPhase::Score;
            clearRevealTimer_ = 0.0f;
        }
        break;
    case ClearRevealPhase::Score:
        if (clearRevealTimer_ >= kScoreRevealDuration + kRevealHoldDuration) {
            clearRevealPhase_ = ClearRevealPhase::Ranking;
            clearRevealTimer_ = 0.0f;
        }
        break;
    case ClearRevealPhase::Ranking:
        break;
    }
}

void BattleResultScene::InitializeWorld() {
    const float aspect = static_cast<float>(ctx_->systems.winApp->GetWidth()) /
                         static_cast<float>(ctx_->systems.winApp->GetHeight());
    camera_.Initialize(aspect);
    camera_.SetClipRange(0.05f, 160.0f);

    ModelManager *model = ctx_->rendering.model;
    playerModelId_ = model->Load(L"app/resources/models/player/player.gltf");
    swordModelId_ = model->Load(L"app/resources/models/player/sword.glb");
    enemyModelId_ = model->Load(L"app/resources/models/boss/boss.gltf");
    celebrationParticleTextureId_ = ctx_->rendering.texture->Load(
        L"app/resources/effects/particles/smoke.png");

    if (ctx_->rendering.texture != nullptr) {
        const uint32_t enemyRustTextureId =
            CreateResultRustedMetalTexture(ctx_->rendering.texture);
        ApplyResultEnemyMaterial(model, enemyModelId_, enemyRustTextureId);

        const uint32_t arenaTextureId =
            CreateResultArenaStoneTexture(ctx_->rendering.texture);
        resultArenaFloorModelId_ = model->CreatePlane(
            arenaTextureId,
            MakeResultArenaMaterial({0.070f, 0.085f, 0.105f, 1.0f}, true,
                                    0.025f, 0.84f));
        resultArenaSpokeModelId_ = model->CreatePlane(
            arenaTextureId, MakeResultArenaMaterial({0.28f, 0.21f, 0.12f, 1.0f},
                                                    false, 0.06f, 0.58f));
        resultArenaCenterDiskModelId_ = model->CreateRing(
            arenaTextureId,
            MakeResultArenaMaterial({0.72f, 0.58f, 0.30f, 1.0f}, false, 0.12f,
                                    0.30f),
            96, 1.95f, 0.0f);
        resultArenaInnerRingModelId_ = model->CreateRing(
            arenaTextureId,
            MakeResultArenaMaterial({0.64f, 0.60f, 0.44f, 1.0f}, false, 0.12f,
                                    0.32f),
            96, 4.9f, 4.35f);
        resultArenaOuterRingModelId_ = model->CreateRing(
            arenaTextureId,
            MakeResultArenaMaterial({0.68f, 0.28f, 0.18f, 1.0f}, false, 0.10f,
                                    0.38f),
            128, 12.3f, 11.6f);
        resultArenaColumnModelId_ = model->CreateCylinder(
            arenaTextureId,
            MakeResultArenaMaterial({0.22f, 0.26f, 0.30f, 1.0f}, true, 0.045f,
                                    0.68f),
            24, 0.26f, 0.38f, 5.4f);
        resultArenaLightModelId_ = model->CreatePlane(
            0, MakeResultArenaMaterial({0.72f, 0.90f, 0.96f, 0.58f}, false,
                                       0.0f, 0.40f));
        resultArenaTowerModelId_ = model->CreateCylinder(
            arenaTextureId,
            MakeResultArenaMaterial({0.18f, 0.24f, 0.29f, 1.0f}, true, 0.025f,
                                    0.76f),
            4, 0.72f, 0.72f, 1.0f);
    }

    ResetModelToBindPose(model, playerModelId_);
    ResetModelToBindPose(model, enemyModelId_);

    if (ctx_->rendering.dxCommon != nullptr && ctx_->rendering.srv != nullptr &&
        ctx_->rendering.texture != nullptr &&
        celebrationParticleTextureId_ != 0) {
        celebrationParticles_.Initialize(
            ctx_->rendering.dxCommon, ctx_->rendering.srv,
            ctx_->rendering.texture, celebrationParticleTextureId_, 1024);
        celebrationParticlesReady_ = true;
    }
}

void BattleResultScene::UpdateCelebrationParticles(float deltaTime) {
    if (resultKind_ != ResultKind::Clear || !celebrationParticlesReady_) {
        return;
    }

    celebrationParticleTimer_ -= deltaTime;
    while (celebrationParticleTimer_ <= 0.0f) {
        celebrationParticleTimer_ += kCelebrationParticleInterval;

        const float t = sceneTime_ * 1.9f;
        const float side = std::sinf(t) >= 0.0f ? 1.0f : -1.0f;
        const XMFLOAT3 origin{side * (2.0f + 0.9f * std::sinf(t * 0.71f)),
                              3.1f + 0.35f * std::sinf(t * 0.37f),
                              -0.5f + 0.7f * std::cosf(t * 0.53f)};

        ParticleEmitterSettings settings{};
        settings.position = origin;
        settings.emissionType = ParticleEmissionType::Burst;
        settings.spawnShape = ParticleSpawnShape::Box;
        settings.burstCount = 42;
        settings.maxParticles = 42;
        settings.spawnOffsetScale = {0.70f, 0.20f, 0.38f};
        settings.tintColor = side > 0.0f ? XMFLOAT4{1.0f, 0.72f, 0.24f, 0.92f}
                                         : XMFLOAT4{0.70f, 0.92f, 1.0f, 0.84f};
        settings.direction = {-side * 0.28f, 1.0f, 0.06f};
        settings.velocityBias = {-side * 0.52f, 0.70f, 0.0f};
        settings.directionalVelocity = 0.85f;
        settings.radialVelocity = 0.95f;
        settings.baseLifeTime = 1.55f;
        settings.lifeTimeRandom = 0.45f;
        settings.startScale = 0.075f;
        settings.endScale = 0.018f;
        settings.scaleRandom = 0.035f;
        settings.stretch = 1.25f;
        settings.acceleration = {0.0f, -0.36f, 0.0f};
        settings.turbulence = 0.42f;
        settings.damping = 0.96f;
        settings.fadeInTime = 0.04f;
        settings.fadeOutTime = 0.50f;
        celebrationParticles_.EmitOnce(settings);
    }

    celebrationParticles_.Update(deltaTime);
}

void BattleResultScene::UpdateResultCamera(float screenWidth,
                                           float screenHeight) {
    camera_.SetAspect(screenWidth / (std::max)(screenHeight, 1.0f));
    if (resultKind_ == ResultKind::GameOver) {
        const float orbit = sceneTime_ * 0.26f;
        const float radius = 13.0f;
        const XMFLOAT3 target = {0.0f, 1.34f, 0.0f};
        const XMFLOAT3 eye = {std::sinf(orbit) * radius, 3.75f,
                              std::cosf(orbit) * radius};
        camera_.SetPerspectiveFovDeg(38.0f);
        camera_.SetPosition(eye);
        camera_.SetRotation(CameraRotationLookAt(eye, target));
        return;
    }

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
    if (resultKind_ == ResultKind::Clear && celebrationParticlesReady_) {
        celebrationParticles_.DispatchPendingUpdate();
    }
    model->PreDraw();
    DrawResultStage();
    DrawResultModels();
    model->PostDraw();
    if (resultKind_ == ResultKind::Clear && celebrationParticlesReady_) {
        celebrationParticles_.Draw(camera_);
    }
}

void BattleResultScene::DrawResultStage() {
    ModelManager *model = ctx_->rendering.model;
    if (model == nullptr || resultArenaFloorModelId_ == 0) {
        return;
    }

    const float pulse = 0.5f + 0.5f * std::sinf(sceneTime_ * 2.2f);
    Transform floor{};
    floor.position = {0.0f, -0.04f, 0.0f};
    floor.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    floor.scale = {180.0f, 180.0f, 1.0f};
    model->Draw(resultArenaFloorModelId_, floor, camera_);

    DrawResultFloorPattern(model);
    DrawResultArchitecture(model);
    DrawResultGlowGrid(model, pulse);
}

void BattleResultScene::DrawResultFloorPattern(ModelManager *model) {
    constexpr float kPatternSpacing = 3.55f;
    constexpr float kLaneSpacing = 4.25f;
    ModelDrawEffect fieldEffect{};
    fieldEffect.enabled = true;
    fieldEffect.additiveBlend = false;
    fieldEffect.disableCulling = true;
    fieldEffect.color = {0.085f, 0.075f, 0.060f, 0.115f};
    fieldEffect.intensity = 0.004f;
    fieldEffect.fresnelPower = 0.7f;
    fieldEffect.noiseAmount = 0.0f;
    fieldEffect.baseDim = 0.0f;
    fieldEffect.time = sceneTime_;
    model->SetDrawEffect(fieldEffect);

    std::vector<Transform> fieldTiles;
    fieldTiles.reserve(520u);
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

    Transform centerTile{};
    centerTile.position = {0.0f, 0.012f, 0.0f};
    centerTile.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    centerTile.scale = {4.8f, 4.8f, 1.0f};
    fieldTiles.push_back(centerTile);

    for (int i = -13; i <= 13; ++i) {
        Transform laneX{};
        laneX.position = {0.0f, 0.016f, static_cast<float>(i) * kLaneSpacing};
        laneX.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        laneX.scale = {168.0f, i == 0 ? 0.080f : 0.034f, 1.0f};
        fieldTiles.push_back(laneX);

        Transform laneZ{};
        laneZ.position = {static_cast<float>(i) * kLaneSpacing, 0.017f, 0.0f};
        laneZ.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        laneZ.scale = {i == 0 ? 0.080f : 0.034f, 168.0f, 1.0f};
        fieldTiles.push_back(laneZ);
    }
    model->DrawInstanced(resultArenaSpokeModelId_, fieldTiles.data(),
                         static_cast<uint32_t>(fieldTiles.size()), camera_);
    model->ClearDrawEffect();
}

void BattleResultScene::DrawResultArchitecture(ModelManager *model) {
    Transform center{};
    center.position = {0.0f, 0.006f, 0.0f};
    center.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    center.scale = {1.0f, 1.0f, 1.0f};
    model->Draw(resultArenaCenterDiskModelId_, center, camera_);

    Transform innerRing{};
    innerRing.position = {0.0f, 0.032f, 0.0f};
    innerRing.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    innerRing.scale = {1.0f, 1.0f, 1.0f};
    model->Draw(resultArenaInnerRingModelId_, innerRing, camera_);

    Transform outerRing{};
    outerRing.position = {0.0f, 0.040f, 0.0f};
    outerRing.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
    outerRing.scale = {1.0f, 1.0f, 1.0f};
    model->Draw(resultArenaOuterRingModelId_, outerRing, camera_);

    std::vector<Transform> columns;
    columns.reserve(8u);
    for (int i = 0; i < 8; ++i) {
        const float a = static_cast<float>(i) / 8.0f * kPi * 2.0f;
        Transform column{};
        column.position = {std::cosf(a) * 11.7f, 2.35f, std::sinf(a) * 11.7f};
        column.rotation = MakeQuat(0.0f, -a, 0.0f);
        column.scale = {1.0f, 0.82f, 1.0f};
        columns.push_back(column);
    }
    model->DrawInstanced(resultArenaColumnModelId_, columns.data(),
                         static_cast<uint32_t>(columns.size()), camera_);

    std::vector<Transform> towers;
    towers.reserve(36u);
    for (int side = 0; side < 2; ++side) {
        const float sign = side == 0 ? -1.0f : 1.0f;
        for (int i = -8; i <= 8; ++i) {
            Transform tower{};
            tower.position = {
                static_cast<float>(i) * 4.0f, -0.62f,
                sign * (38.0f + std::fabs(static_cast<float>(i)) * 0.62f)};
            tower.rotation = MakeQuat(0.0f, 0.0f, 0.0f);
            tower.scale = {
                0.58f + static_cast<float>((i + 8) % 3) * 0.16f,
                2.6f + static_cast<float>((i * i + side) % 7) * 0.38f, 0.72f};
            towers.push_back(tower);
        }
    }
    model->DrawInstanced(resultArenaTowerModelId_, towers.data(),
                         static_cast<uint32_t>(towers.size()), camera_);
}

void BattleResultScene::DrawResultGlowGrid(ModelManager *model, float pulse) {
    constexpr float kPatternSpacing = 3.55f;
    ModelDrawEffect lineEffect{};
    lineEffect.enabled = true;
    lineEffect.additiveBlend = true;
    lineEffect.disableCulling = true;
    lineEffect.color = {0.22f, 0.10f, 0.045f, 0.026f};
    lineEffect.intensity = 0.0015f + 0.0015f * pulse;
    lineEffect.fresnelPower = 0.75f;
    lineEffect.noiseAmount = 0.0f;
    lineEffect.time = sceneTime_;
    model->SetDrawEffect(lineEffect);

    std::vector<Transform> glowLines;
    glowLines.reserve(62u);
    for (int i = -15; i <= 15; ++i) {
        Transform line{};
        line.position = {0.0f, 0.034f, static_cast<float>(i) * kPatternSpacing};
        line.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        line.scale = {160.0f, 0.016f, 1.0f};
        glowLines.push_back(line);

        Transform cross{};
        cross.position = {static_cast<float>(i) * kPatternSpacing, 0.035f,
                          0.0f};
        cross.rotation = MakeQuat(-kPi * 0.5f, 0.0f, 0.0f);
        cross.scale = {0.016f, 160.0f, 1.0f};
        glowLines.push_back(cross);
    }
    model->DrawInstanced(resultArenaLightModelId_, glowLines.data(),
                         static_cast<uint32_t>(glowLines.size()), camera_);
    model->ClearDrawEffect();
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

    Transform enemy{};
    enemy.position = {0.0f, 0.0f, 0.0f};
    enemy.rotation = MakeQuat(0.0f, -0.06f, 0.0f);
    enemy.scale = {1.23f, 1.23f, 1.23f};
    model->Draw(enemyModelId_, enemy, camera_);
}

void BattleResultScene::DrawResultOverlay(float screenWidth,
                                          float screenHeight) {
    if (resultKind_ == ResultKind::Clear) {
        const float pulse = 0.5f + 0.5f * std::sin(sceneTime_ * 2.0f);
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                 Color(0.006f, 0.018f + pulse * 0.008f, 0.022f, 0.34f));
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight * 0.18f,
                 Color(0.0f, 0.0f, 0.0f, 0.16f));
        DrawRect(0.0f, screenHeight * 0.82f, screenWidth, screenHeight * 0.18f,
                 Color(0.0f, 0.0f, 0.0f, 0.34f));
        DrawClear(screenWidth, screenHeight);
    } else {
        DrawRect(0.0f, 0.0f, screenWidth, screenHeight,
                 Color(0.010f, 0.012f, 0.016f, 1.0f));
        DrawRect(0.0f, screenHeight * 0.72f, screenWidth, screenHeight * 0.28f,
                 Color(0.0f, 0.0f, 0.0f, 0.22f));
        DrawGameOver(screenWidth, screenHeight);
        DrawHandInputStatus(screenWidth, screenHeight);
    }
    DrawControlsHint(screenWidth, screenHeight);
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
    const float titleScale = std::clamp(
        screenWidth * 0.64f / (std::max)(missionCompleteLabel_.width, 1.0f),
        0.58f, 1.0f);
    const float titleW = missionCompleteLabel_.width * titleScale;
    const float titleY = screenHeight * 0.046f;
    DrawImage(missionCompleteLabel_, (screenWidth - titleW) * 0.5f, titleY,
              titleScale, 0.98f);

    switch (clearRevealPhase_) {
    case ClearRevealPhase::Time:
        DrawClearTimeScreen(screenWidth, screenHeight);
        break;
    case ClearRevealPhase::Score:
        DrawScoreScreen(screenWidth, screenHeight);
        break;
    case ClearRevealPhase::Ranking:
        DrawRankingScreen(screenWidth, screenHeight);
        DrawClearActionButtons(screenWidth, screenHeight);
        break;
    }
}

void BattleResultScene::DrawClearTimeScreen(float screenWidth,
                                            float screenHeight) {
    const float panelW = std::clamp(screenWidth * 0.68f, 760.0f, 1040.0f);
    const float panelH = std::clamp(screenHeight * 0.30f, 260.0f, 340.0f);
    const float panelX = (screenWidth - panelW) * 0.5f;
    const float panelY = screenHeight * 0.345f;

    DrawRect(panelX + 14.0f, panelY + 16.0f, panelW, panelH,
             Color(0.0f, 0.0f, 0.0f, 0.30f));
    DrawRect(panelX, panelY, panelW, panelH,
             Color(0.018f, 0.022f, 0.026f, 0.78f));
    DrawFrame(panelX, panelY, panelW, panelH, 2.0f,
              Color(0.95f, 0.72f, 0.28f, 0.72f));

    const float labelScale =
        std::clamp((panelW * 0.24f) / (std::max)(clearTimeLabel_.width, 1.0f),
                   0.52f, 0.86f);
    const float labelW = clearTimeLabel_.width * labelScale;
    DrawImage(clearTimeLabel_, panelX + (panelW - labelW) * 0.5f,
              panelY + panelH * 0.16f, labelScale, 0.88f);

    const std::string time = FormatAnimatedTime();
    const float timeScale = std::clamp(
        (panelW * 0.62f) / MeasureTextLine(time, 1.0f), 1.24f, 1.92f);
    DrawTextLine(time, panelX + panelW * 0.5f, panelY + panelH * 0.46f,
                 timeScale, 1.0f);
}

void BattleResultScene::DrawScoreScreen(float screenWidth, float screenHeight) {
    const float panelW = std::clamp(screenWidth * 0.68f, 760.0f, 1040.0f);
    const float panelH = std::clamp(screenHeight * 0.34f, 292.0f, 380.0f);
    const float panelX = (screenWidth - panelW) * 0.5f;
    const float panelY = screenHeight * 0.325f;

    DrawRect(panelX + 14.0f, panelY + 16.0f, panelW, panelH,
             Color(0.0f, 0.0f, 0.0f, 0.30f));
    DrawRect(panelX, panelY, panelW, panelH,
             Color(0.018f, 0.022f, 0.026f, 0.80f));
    DrawFrame(panelX, panelY, panelW, panelH, 2.0f,
              Color(0.95f, 0.72f, 0.28f, 0.72f));

    const Image &label = scoreTitleLabel_.textureId != 0 ? scoreTitleLabel_
                                                         : currentRecordLabel_;
    const float labelScale = std::clamp(
        (panelW * 0.20f) / (std::max)(label.width, 1.0f), 0.50f, 0.88f);
    const float labelW = label.width * labelScale;
    DrawImage(label, panelX + (panelW - labelW) * 0.5f, panelY + panelH * 0.12f,
              labelScale, 0.90f);

    const std::string score = FormatAnimatedScore();
    const float scoreScale = std::clamp(
        (panelW * 0.46f) / MeasureTextLine(score, 1.0f), 1.28f, 1.92f);
    DrawTextLine(score, panelX + panelW * 0.5f, panelY + panelH * 0.42f,
                 scoreScale, 1.0f);

    const std::string time = FormatTime(clearTime_);
    const float timeScale = std::clamp(
        (panelW * 0.22f) / MeasureTextLine(time, 1.0f), 0.38f, 0.58f);
    DrawTextLine(time, panelX + panelW * 0.5f, panelY + panelH * 0.78f,
                 timeScale, 0.74f);
}

void BattleResultScene::DrawRankingScreen(float screenWidth,
                                          float screenHeight) {
    const float panelW = std::clamp(screenWidth * 0.74f, 880.0f, 1180.0f);
    const float panelH = std::clamp(screenHeight * 0.42f, 340.0f, 460.0f);
    const float panelX = (screenWidth - panelW) * 0.5f;
    const float panelY = screenHeight * 0.305f;
    DrawRanking(panelX, panelY, panelW, panelH, true);
}

void BattleResultScene::DrawGameOver(float screenWidth, float screenHeight) {
    const float titleScale = std::clamp(
        screenWidth * 0.52f / (std::max)(missionFailedLabel_.width, 1.0f),
        0.46f, 0.82f);
    const float titleW = missionFailedLabel_.width * titleScale;
    const float titleY =
        screenHeight * 0.38f - missionFailedLabel_.height * titleScale * 0.5f;
    DrawImage(missionFailedLabel_, (screenWidth - titleW) * 0.5f, titleY,
              titleScale, 0.96f);
}

void BattleResultScene::DrawRanking(float x, float y, float w, float h,
                                    bool fullDetail) {
    DrawRect(x + 12.0f, y + 14.0f, w, h, Color(0.0f, 0.0f, 0.0f, 0.42f));
    DrawRect(x, y, w, h, Color(0.010f, 0.013f, 0.018f, 0.91f));
    DrawRect(x + w * 0.035f, y + h * 0.265f, w * 0.93f, h * 0.650f,
             Color(0.0f, 0.0f, 0.0f, 0.24f));
    DrawFrame(x, y, w, h, 2.0f, Color(0.95f, 0.72f, 0.28f, 0.78f));

    const float titleScale = std::clamp(
        (w * 0.54f) / (std::max)(rankingTitleLabel_.width, 1.0f), 0.42f, 0.72f);
    const float titleW = rankingTitleLabel_.width * titleScale;
    DrawImage(rankingTitleLabel_, x + (w - titleW) * 0.5f, y + h * 0.10f,
              titleScale, 0.92f);

    const float rankX = x + w * 0.08f;
    const float scoreRightX = x + w * (fullDetail ? 0.36f : 0.54f);
    const float timeRightX = x + w * (fullDetail ? 0.66f : 0.92f);
    const float difficultyRightX = x + w * 0.90f;
    const float rankColumnW = w * 0.10f;
    const float scoreColumnW = w * (fullDetail ? 0.20f : 0.26f);
    const float timeColumnW = w * (fullDetail ? 0.25f : 0.33f);
    const float difficultyColumnW = w * 0.12f;
    const float headerY = y + h * 0.265f;
    const float rowStartY = y + h * 0.38f;
    const float rowGap = h * 0.105f;
    const float headerScale = fullDetail ? 0.50f : 0.42f;
    DrawImage(rankingRankHeaderLabel_, rankX, headerY, headerScale, 0.72f);
    DrawImage(rankingScoreHeaderLabel_,
              scoreRightX - rankingScoreHeaderLabel_.width * headerScale,
              headerY, headerScale, 0.72f);
    DrawImage(rankingTimeHeaderLabel_,
              timeRightX - rankingTimeHeaderLabel_.width * headerScale, headerY,
              headerScale, 0.72f);
    if (fullDetail) {
        DrawImage(rankingDifficultyHeaderLabel_,
                  difficultyRightX -
                      rankingDifficultyHeaderLabel_.width * headerScale,
                  headerY, headerScale, 0.72f);
    }
    DrawRect(x + w * 0.07f, y + h * 0.335f, w * 0.86f, 1.0f,
             Color(0.95f, 0.72f, 0.28f, 0.30f));
    const float tableTop = y + h * 0.255f;
    const float tableBottom = rowStartY + rowGap * 4.72f;
    const float separatorAlpha = 0.22f;
    DrawRect(x + w * 0.205f, tableTop, 1.0f, tableBottom - tableTop,
             Color(0.95f, 0.72f, 0.28f, separatorAlpha));
    DrawRect(x + w * (fullDetail ? 0.405f : 0.620f), tableTop, 1.0f,
             tableBottom - tableTop,
             Color(0.95f, 0.72f, 0.28f, separatorAlpha));
    if (fullDetail) {
        DrawRect(x + w * 0.705f, tableTop, 1.0f, tableBottom - tableTop,
                 Color(0.95f, 0.72f, 0.28f, separatorAlpha));
    }
    const size_t rows = (std::min)(rankingEntries_.size(), kRankingDrawCount);
    for (size_t i = 0; i < rows; ++i) {
        const float rowY = rowStartY + static_cast<float>(i) * rowGap;
        DrawRankingRow(i, x, w, rowY, rowGap, rankX, scoreRightX, timeRightX,
                       difficultyRightX, rankColumnW, scoreColumnW, timeColumnW,
                       difficultyColumnW, fullDetail);
    }
}

void BattleResultScene::DrawRankingRow(
    size_t index, float x, float w, float rowY, float rowGap, float rankX,
    float scoreRightX, float timeRightX, float difficultyRightX,
    float rankColumnW, float scoreColumnW, float timeColumnW,
    float difficultyColumnW, bool fullDetail) {
    const RankingEntry &entry = rankingEntries_[index];
    const bool highlight = entry.isCurrent;
    const float rowFrameY = rowY - rowGap * 0.12f;
    const float rowFrameH = rowGap * 0.82f;
    DrawRect(x + w * 0.07f, rowFrameY, w * 0.86f, rowFrameH,
             Color(1.0f, 1.0f, 1.0f, index % 2 == 0 ? 0.030f : 0.015f));
    if (highlight) {
        DrawRect(x + w * 0.07f, rowFrameY, w * 0.86f, rowFrameH,
                 Color(0.95f, 0.72f, 0.28f, 0.20f));
        DrawFrame(x + w * 0.07f, rowFrameY, w * 0.86f, rowFrameH, 1.0f,
                  Color(0.95f, 0.72f, 0.28f, 0.46f));
    }
    std::ostringstream rank;
    rank << (index + 1) << ":";
    const std::string score = FormatScore(entry.score);
    const std::string time = FormatTime(entry.clearTime);
    const std::string difficulty = FormatDifficulty(entry.difficulty);
    const float alpha = highlight ? 1.0f : 0.86f;
    const float rankScale =
        std::clamp(rankColumnW / MeasureTextLine(rank.str(), 1.0f),
                   fullDetail ? 0.42f : 0.34f, fullDetail ? 0.58f : 0.44f);
    const float scoreScale =
        std::clamp(scoreColumnW / MeasureTextLine(score, 1.0f),
                   fullDetail ? 0.42f : 0.30f, fullDetail ? 0.58f : 0.44f);
    const float timeScale =
        std::clamp(timeColumnW / MeasureTextLine(time, 1.0f),
                   fullDetail ? 0.42f : 0.30f, fullDetail ? 0.58f : 0.44f);
    const float difficultyScale = std::clamp(
        difficultyColumnW / MeasureTextLine(difficulty, 1.0f), 0.42f, 0.58f);
    const auto baseline = [this, rowFrameY, rowFrameH](float scale) {
        return rowFrameY + rowFrameH * 0.5f +
               MeasureTextInkCenterOffset("00:00.00s", scale);
    };
    DrawTextLineLeftBaseline(rank.str(), rankX, baseline(rankScale), rankScale,
                             alpha);
    DrawTextLineLeftBaseline(score,
                             scoreRightX - MeasureTextLine(score, scoreScale),
                             baseline(scoreScale), scoreScale, alpha);
    DrawTextLineLeftBaseline(
        time, timeRightX - MeasureTextLine(time, timeScale),
        baseline(timeScale), timeScale, highlight ? 1.0f : 0.90f);
    if (fullDetail) {
        DrawTextLineLeftBaseline(
            difficulty,
            difficultyRightX - MeasureTextLine(difficulty, difficultyScale),
            baseline(difficultyScale), difficultyScale,
            highlight ? 1.0f : 0.90f);
    }
}

void BattleResultScene::DrawControlsHint(float screenWidth,
                                         float screenHeight) {
    if (resultKind_ == ResultKind::Clear) {
        return;
    }

    const Image *hint = resultKind_ == ResultKind::Clear
                            ? &controlsKbmClearImage_
                            : &controlsKbmGameOverImage_;
    if (resultKind_ != ResultKind::Clear &&
        IsHandControl(inputCalibration_.controlType)) {
        hint = &controlsHandImage_;
    }

    constexpr float kControlsPadding = 32.0f;
    const float bottomTransparentPixels =
        hint == &controlsHandImage_ ? 26.0f : 18.0f;
    const float controlsScale =
        std::min(0.80f, (screenWidth * 0.31f) / (std::max)(hint->width, 1.0f));
    DrawImage(*hint, kControlsPadding,
              screenHeight -
                  (hint->height - bottomTransparentPixels) * controlsScale -
                  kControlsPadding,
              controlsScale, 0.70f);
}

void BattleResultScene::DrawClearActionButtons(float screenWidth,
                                               float screenHeight) {
    const float buttonW = std::clamp(screenWidth * 0.14f, 180.0f, 252.0f);
    const float buttonH = std::clamp(screenHeight * 0.072f, 62.0f, 84.0f);
    const float buttonGap = std::clamp(screenWidth * 0.028f, 34.0f, 56.0f);
    const float totalW = buttonW * 2.0f + buttonGap;
    const float firstX = (screenWidth - totalW) * 0.5f;
    const float y = screenHeight * 0.828f;
    const Image *labels[2] = {&retryButtonLabel_, &titleButtonLabel_};

    for (int i = 0; i < 2; ++i) {
        const float x = firstX + static_cast<float>(i) * (buttonW + buttonGap);
        const bool selected = i == clearActionButtonIndex_;
        const XMFLOAT4 body = selected ? Color(0.18f, 0.13f, 0.055f, 0.98f)
                                       : Color(0.040f, 0.046f, 0.058f, 0.90f);
        const XMFLOAT4 line = selected ? Color(1.0f, 0.78f, 0.34f, 0.98f)
                                       : Color(0.62f, 0.66f, 0.72f, 0.40f);

        DrawRect(x + 6.0f, y + 8.0f, buttonW, buttonH,
                 Color(0.0f, 0.0f, 0.0f, selected ? 0.32f : 0.22f));
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
                  selected ? 1.0f : 0.80f);
    }
}

void BattleResultScene::DrawHandInputStatus(float screenWidth,
                                            float screenHeight) {
    if (!IsHandControl(inputCalibration_.controlType)) {
        return;
    }

    const float unit = 52.0f;
    const float gap = 18.0f;
    const float startX = (screenWidth - unit * 3.0f - gap * 2.0f) * 0.5f;
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
        (std::min)(1.0f,
                   (panelW * 0.78f) /
                       (std::max)(returnTitleConfirmMessageImage_.width, 1.0f));
    const float messageW = returnTitleConfirmMessageImage_.width * messageScale;
    const float messageH =
        returnTitleConfirmMessageImage_.height * messageScale;
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
        const XMFLOAT4 body = selected ? Color(0.18f, 0.13f, 0.055f, 0.98f)
                                       : Color(0.040f, 0.046f, 0.058f, 0.92f);
        const XMFLOAT4 line = selected ? Color(1.0f, 0.78f, 0.34f, 0.96f)
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
            x += GetCharAdvance(c) * scale;
            continue;
        }
        const Image *image = FindCharImage(c);
        if (image == nullptr) {
            continue;
        }
        const float inkLeft =
            image->inkRight > image->inkLeft ? image->inkLeft : 0.0f;
        DrawImage(*image, x - inkLeft * scale, y, scale, alpha);
        x += GetCharAdvance(c) * scale;
    }
}

void BattleResultScene::DrawTextLineLeftBaseline(const std::string &text,
                                                 float x, float baselineY,
                                                 float scale, float alpha) {
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
        DrawImage(*image, x - inkLeft * scale, baselineY - inkBottom * scale,
                  scale, alpha);
        x += GetCharAdvance(c) * scale;
    }
}

float BattleResultScene::GetCharAdvance(char c) const {
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

float BattleResultScene::MeasureTextLine(const std::string &text,
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
            const float inkWidth = image->inkRight > image->inkLeft
                                       ? image->inkRight - image->inkLeft
                                       : image->width;
            width = cursorX + inkWidth;
            cursorX += GetCharAdvance(c);
        }
    }
    return (std::max)(0.0f, width * scale);
}

float BattleResultScene::MeasureTextInkCenterOffset(const std::string &text,
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
    oss << std::setfill('0') << std::setw(2) << minutes << ':' << std::setw(2)
        << sec << '.' << std::setw(2) << centi << 's';
    return oss.str();
}

std::string BattleResultScene::FormatAnimatedTime() const {
    std::string text = FormatTime(clearTime_);
    int digitCount = 0;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            ++digitCount;
        }
    }

    const float progress =
        std::clamp(clearRevealTimer_ / kTimeRevealDuration, 0.0f, 1.0f);
    const int lockedDigits =
        (std::min)(digitCount,
                   static_cast<int>(progress * static_cast<float>(digitCount) +
                                    0.001f));
    const uint32_t frame = static_cast<uint32_t>(sceneTime_ * 42.0f);

    int digitsFromRight = 0;
    for (int i = static_cast<int>(text.size()) - 1; i >= 0; --i) {
        char &c = text[static_cast<size_t>(i)];
        if (c < '0' || c > '9') {
            continue;
        }
        const bool locked = digitsFromRight < lockedDigits;
        if (!locked) {
            c = static_cast<char>(
                '0' + (Hash2D(static_cast<uint32_t>(i), frame, 0xC1EAu) % 10u));
        }
        ++digitsFromRight;
    }
    return text;
}

std::string BattleResultScene::FormatScore(int score) const {
    return std::to_string((std::max)(0, score));
}

std::string BattleResultScene::FormatAnimatedScore() const {
    std::string text = FormatScore(currentScore_);
    const int digitCount = static_cast<int>(text.size());
    const float progress =
        std::clamp(clearRevealTimer_ / kScoreRevealDuration, 0.0f, 1.0f);
    const int lockedDigits =
        (std::min)(digitCount,
                   static_cast<int>(progress * static_cast<float>(digitCount) +
                                    0.001f));
    const uint32_t frame = static_cast<uint32_t>(sceneTime_ * 34.0f);

    for (int i = digitCount - 1; i >= 0; --i) {
        const int digitsFromRight = digitCount - 1 - i;
        if (digitsFromRight >= lockedDigits) {
            text[static_cast<size_t>(i)] = static_cast<char>(
                '0' +
                (Hash2D(static_cast<uint32_t>(i), frame, 0x5C0A3u) % 10u));
        }
    }
    return text;
}

std::string BattleResultScene::FormatDifficulty(float difficulty) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1)
        << std::clamp(difficulty, 0.0f, 9.0f);
    return oss.str();
}
