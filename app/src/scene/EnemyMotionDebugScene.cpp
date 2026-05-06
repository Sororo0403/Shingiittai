#include "EnemyMotionDebugScene.h"
#include "DebugDraw.h"
#include "DirectXCommon.h"
#include "EnemyTuningPresetIO.h"
#include "GameScene.h"
#include "Input.h"
#include "Model.h"
#include "ModelManager.h"
#include "SceneManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#ifndef IMGUI_DISABLED
#include "imgui.h"
#endif

using namespace DirectX;

static const std::string kBossAnimIdle = "Action";
static const std::string kBossAnimMove = "Action.001";
static const std::string kBossAnimSweep =
    "\xE6\xA8\xAA\xE8\x96\x99\xE3\x81\x8E\xE6\x89\x95\xE3\x81\x84";
static const std::string kBossAnimWave =
    "\xE6\xB3\xA2\xE7\x8A\xB6\xE6\x94\xBB\xE6\x92\x83";
static const std::string kBossAnimSmash =
    "\xE7\xB8\xA6\xE6\x8C\xAF\xE3\x82\x8A\xE4\xB8\x8B\xE3\x82\x8D\xE3\x81\x97";

static bool HasAnimation(const Model *model, const std::string &animationName) {
    if (model == nullptr) {
        return false;
    }
    return model->animations.find(animationName) != model->animations.end();
}

static std::string PickEnemyIntroAnimation(const Model *model, const Enemy &enemy,
                                           bool &outLoop) {
    outLoop = false;
    if (model == nullptr || model->animations.empty()) {
        return {};
    }

    switch (enemy.GetIntroPhase()) {
    case IntroPhase::SecondSlash:
        if (HasAnimation(model, kBossAnimSmash)) {
            return kBossAnimSmash;
        }
        if (HasAnimation(model, kBossAnimSweep)) {
            return kBossAnimSweep;
        }
        break;
    case IntroPhase::SpinSlash:
    case IntroPhase::Settle:
        if (HasAnimation(model, kBossAnimSweep)) {
            return kBossAnimSweep;
        }
        if (HasAnimation(model, kBossAnimSmash)) {
            return kBossAnimSmash;
        }
        break;
    }

    outLoop = true;
    if (HasAnimation(model, kBossAnimIdle)) {
        return kBossAnimIdle;
    }
    return model->currentAnimation.empty() ? model->animations.begin()->first
                                           : model->currentAnimation;
}

static std::string PickEnemyAnimation(const Model *model, const Enemy &enemy,
                                      bool &outLoop) {
    outLoop = true;
    if (model == nullptr || model->animations.empty()) {
        return {};
    }

    switch (enemy.GetActionKind()) {
    case ActionKind::Smash:
        outLoop = false;
        if (HasAnimation(model, kBossAnimSmash)) {
            return kBossAnimSmash;
        }
        break;
    case ActionKind::Sweep:
        outLoop = false;
        if (HasAnimation(model, kBossAnimSweep)) {
            return kBossAnimSweep;
        }
        break;
    case ActionKind::Shot:
    case ActionKind::Wave:
        outLoop = false;
        if (HasAnimation(model, kBossAnimWave)) {
            return kBossAnimWave;
        }
        break;
    case ActionKind::Warp:
    case ActionKind::Stalk:
    case ActionKind::None:
    default:
        if (HasAnimation(model, kBossAnimIdle)) {
            return kBossAnimIdle;
        }
        break;
    }

    if (HasAnimation(model, kBossAnimIdle)) {
        return kBossAnimIdle;
    }
    if (HasAnimation(model, kBossAnimMove)) {
        return kBossAnimMove;
    }
    return model->currentAnimation.empty() ? model->animations.begin()->first
                                           : model->currentAnimation;
}

void EnemyMotionDebugScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

    const float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                         static_cast<float>(ctx_->winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.SetMode(CameraMode::LookAt);
    camera_.SetPosition({0.0f, 2.0f, 1.0f});
    camera_.LookAt({0.0f, 1.25f, 10.0f});
    camera_.SetPerspectiveFovDeg(60.0f);
    camera_.UpdateMatrices();

#ifdef _DEBUG
    debugCamera_.Initialize(aspect);
    debugCamera_.SetMode(CameraMode::Free);
    debugCamera_.UpdateMatrices();

    tripodCamera_.Initialize(aspect);
    tripodCamera_.SetMode(CameraMode::LookAt);
    tripodCamera_.SetPosition(tripodPos_);
    tripodCamera_.LookAt(tripodTarget_);
    tripodCamera_.SetPerspectiveFovDeg(60.0f);
    tripodCamera_.UpdateMatrices();
#endif

    currentCamera_ = &camera_;

    DirectXCommon *dx = ctx_->dxCommon;
    ModelManager *model = ctx_->model;
    TextureManager *texture = ctx_->texture;

    dx->BeginUpload();
    uint32_t enemyModel = 0;
    const uint32_t bulletModel = model->Load(L"app/resources/model/bullet/bullet.obj");
    try {
        enemyModel = model->Load(L"app/resources/model/boss/boss.gltf");
    } catch (const std::exception &) {
        enemyModel = model->Load(L"app/resources/model/enemy/enemy.glb");
    }
    dx->EndUpload();
    texture->ReleaseUploadBuffers();

    enemy_.Initialize(enemyModel, bulletModel);
    enemyModelId_ = enemyModel;
    enemy_.DebugResetState();

    EnemyTuningPreset enemyPreset{};
    if (EnemyTuningPresetIO::Load("app/resources/enemy_tuning.csv", enemyPreset) ||
        EnemyTuningPresetIO::Load("app/resources/enemy_tuning.txt", enemyPreset)) {
        enemy_.ApplyTuningPreset(enemyPreset);
        enemy_.DebugResetState();
    }

    previewPlayerPos_ = {0.0f, 0.0f, 0.0f};
    playerObs_.position = previewPlayerPos_;
    playerObs_.velocity = {0.0f, 0.0f, 0.0f};
    playerObs_.counterAxis = CounterAxis::None;

    SyncEnemyAnimation();
}

void EnemyMotionDebugScene::Update() {
    if (ctx_ == nullptr || ctx_->input == nullptr) {
        return;
    }

    Input *input = ctx_->input;
    if (input->IsKeyTrigger(DIK_F8) && sceneManager_ != nullptr) {
        sceneManager_->ChangeScene(std::make_unique<GameScene>());
        return;
    }

    UpdateCamera();
    UpdateEnemyObservation();

    const float deltaTime = ctx_->deltaTime;
    sceneLightTime_ += deltaTime;
    if (!pauseEnemyUpdate_ && !lockIdle_) {
        enemy_.Update(playerObs_, deltaTime);
    }

    SyncEnemyAnimation();
    ctx_->model->UpdateAnimation(enemyModelId_, pauseEnemyUpdate_ ? 0.0f : deltaTime);
}

void EnemyMotionDebugScene::Draw() {
    ctx_->model->PreDraw();
    enemy_.Draw(ctx_->model, *currentCamera_);

#ifdef _DEBUG
    if (drawHitBoxes_ && ctx_->debugDraw != nullptr) {
        ModelDrawEffect hitBoxEffect{};
        hitBoxEffect.enabled = true;
        hitBoxEffect.intensity = 0.45f;
        hitBoxEffect.fresnelPower = 2.8f;
        hitBoxEffect.noiseAmount = 0.06f;
        hitBoxEffect.time = sceneLightTime_ * 5.0f;

        hitBoxEffect.color = {1.00f, 0.28f, 0.20f, 0.40f};
        ctx_->model->SetDrawEffect(hitBoxEffect);
        ctx_->debugDraw->DrawOBB(ctx_->model, enemy_.GetBodyOBB(), *currentCamera_);
        hitBoxEffect.color = {1.00f, 0.45f, 0.25f, 0.35f};
        ctx_->model->SetDrawEffect(hitBoxEffect);
        ctx_->debugDraw->DrawOBB(ctx_->model, enemy_.GetLeftHandOBB(), *currentCamera_);
        ctx_->debugDraw->DrawOBB(ctx_->model, enemy_.GetRightHandOBB(), *currentCamera_);

        if (enemy_.IsAttackActive()) {
            hitBoxEffect.color = {1.00f, 1.00f, 0.15f, 0.52f};
            hitBoxEffect.intensity = 0.62f;
            ctx_->model->SetDrawEffect(hitBoxEffect);
            ctx_->debugDraw->DrawOBB(ctx_->model, enemy_.GetAttackOBB(),
                                     *currentCamera_);
        }

        for (const auto &bullet : enemy_.GetBullets()) {
            if (!bullet.isAlive) {
                continue;
            }
            OBB bulletBox{};
            bulletBox.center = bullet.position;
            bulletBox.size = enemy_.GetBulletHitBoxSize();
            bulletBox.rotation = enemy_.GetTransform().rotation;
            hitBoxEffect.color = {0.95f, 0.20f, 1.00f, 0.34f};
            hitBoxEffect.intensity = 0.45f;
            ctx_->model->SetDrawEffect(hitBoxEffect);
            ctx_->debugDraw->DrawOBB(ctx_->model, bulletBox, *currentCamera_);
        }

        for (const auto &wave : enemy_.GetWaves()) {
            if (!wave.isAlive) {
                continue;
            }
            OBB waveBox{};
            waveBox.center = wave.position;
            waveBox.size = enemy_.GetWaveHitBoxSize();
            waveBox.rotation = enemy_.GetTransform().rotation;
            hitBoxEffect.color = {0.25f, 0.65f, 1.00f, 0.34f};
            hitBoxEffect.intensity = 0.45f;
            ctx_->model->SetDrawEffect(hitBoxEffect);
            ctx_->debugDraw->DrawOBB(ctx_->model, waveBox, *currentCamera_);
        }

        ctx_->model->ClearDrawEffect();
    }
#endif

    ctx_->model->PostDraw();
    DrawDebugUi();
}

void EnemyMotionDebugScene::UpdateCamera() {
#ifdef _DEBUG
    if (currentCamera_ == &debugCamera_) {
        debugCamera_.Update(*ctx_->input, ctx_->deltaTime);
        debugCamera_.UpdateMatrices();
        return;
    }

    if (currentCamera_ == &tripodCamera_) {
        tripodCamera_.SetPosition(tripodPos_);
        tripodCamera_.LookAt(tripodTarget_);
        tripodCamera_.UpdateMatrices();
        return;
    }
#endif

    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT3 cameraPos = {previewPlayerPos_.x, 2.0f, previewPlayerPos_.z + 1.0f};
    camera_.SetPosition(cameraPos);
    camera_.LookAt({enemyPos.x, enemyPos.y + 1.25f, enemyPos.z});
    camera_.UpdateMatrices();
}

void EnemyMotionDebugScene::UpdateEnemyObservation() {
    playerObs_.position = previewPlayerPos_;
    playerObs_.velocity = {0.0f, 0.0f, 0.0f};
}

void EnemyMotionDebugScene::SyncEnemyAnimation() {
    Model *enemyModel = ctx_->model->GetModel(enemyModelId_);
    if (enemyModel == nullptr || enemyModel->animations.empty()) {
        return;
    }

    bool shouldLoop = true;
    std::string nextAnimation = PickEnemyAnimation(enemyModel, enemy_, shouldLoop);
    if (nextAnimation.empty()) {
        return;
    }

    if (enemy_.IsIntroActive()) {
        bool introLoop = false;
        std::string introAnimation =
            PickEnemyIntroAnimation(enemyModel, enemy_, introLoop);
        if (!introAnimation.empty()) {
            const IntroPhase introPhase = enemy_.GetIntroPhase();
            const bool forceRestart =
                (enemyIntroPhase_ != introPhase &&
                 (introPhase == IntroPhase::SecondSlash ||
                  introPhase == IntroPhase::SpinSlash));
            if (!enemyIntroAnimationStarted_ || forceRestart ||
                enemyAnimationName_ != introAnimation ||
                enemyAnimationLoop_ != introLoop) {
                ctx_->model->PlayAnimation(enemyModelId_, introAnimation, introLoop);
                ctx_->model->UpdateAnimation(enemyModelId_, 0.0f);
                enemyAnimationName_ = introAnimation;
                enemyAnimationLoop_ = introLoop;
                enemyIntroAnimationStarted_ = true;
            }
            enemyIntroPhase_ = introPhase;
            return;
        }
    } else {
        enemyIntroAnimationStarted_ = false;
        enemyIntroPhase_ = IntroPhase::SecondSlash;
    }

    if (enemyAnimationName_ == nextAnimation && enemyAnimationLoop_ == shouldLoop) {
        return;
    }

    ctx_->model->PlayAnimation(enemyModelId_, nextAnimation, shouldLoop);
    ctx_->model->UpdateAnimation(enemyModelId_, 0.0f);
    enemyAnimationName_ = nextAnimation;
    enemyAnimationLoop_ = shouldLoop;
}

void EnemyMotionDebugScene::DrawDebugUi() {
#ifndef IMGUI_DISABLED
    ImGui::Begin("Enemy Motion Debug");
    ImGui::Text("F8: Back to GameScene");
    ImGui::Checkbox("Pause Enemy Update", &pauseEnemyUpdate_);
    ImGui::Checkbox("Idle Lock", &lockIdle_);
    ImGui::Checkbox("Draw HitBoxes", &drawHitBoxes_);

    if (ImGui::Button("Back To Game")) {
        if (sceneManager_ != nullptr) {
            sceneManager_->ChangeScene(std::make_unique<GameScene>());
            ImGui::End();
            return;
        }
    }

    if (ImGui::Button("Reset Enemy")) {
        lockIdle_ = false;
        enemy_.DebugResetState();
        SyncEnemyAnimation();
    }
    ImGui::SameLine();
    if (ImGui::Button("Idle")) {
        lockIdle_ = true;
        enemy_.DebugStartAction(ActionKind::None);
        SyncEnemyAnimation();
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart Intro")) {
        lockIdle_ = false;
        enemy_.RestartIntro();
        SyncEnemyAnimation();
    }
    ImGui::SameLine();
    if (ImGui::Button("Skip Intro")) {
        lockIdle_ = false;
        enemy_.SkipIntro();
        SyncEnemyAnimation();
    }

    ImGui::Separator();
    ImGui::Text("Boss Phase");
    if (ImGui::Button("Phase1")) {
        enemy_.DebugSetBossPhase(BossPhase::Phase1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Phase2")) {
        enemy_.DebugSetBossPhase(BossPhase::Phase2);
    }

    ImGui::Separator();
    ImGui::Text("Preview Player");
    ImGui::DragFloat3("Player Position", &previewPlayerPos_.x, 0.05f, -20.0f, 20.0f);
    ImGui::Checkbox("Player Guarding", &playerObs_.isGuarding);
    ImGui::Checkbox("Counter Stance", &playerObs_.isCounterStance);
    ImGui::Checkbox("Player Attacking", &playerObs_.isAttacking);
    ImGui::Checkbox("Just Countered", &playerObs_.justCountered);
    ImGui::Checkbox("Counter Failed", &playerObs_.justCounterFailed);
    ImGui::Checkbox("Counter Early", &playerObs_.justCounterEarly);
    ImGui::Checkbox("Counter Late", &playerObs_.justCounterLate);
    int counterAxis = static_cast<int>(playerObs_.counterAxis);
    if (ImGui::RadioButton("CounterAxis None", counterAxis == static_cast<int>(CounterAxis::None))) {
        playerObs_.counterAxis = CounterAxis::None;
    }
    if (ImGui::RadioButton("CounterAxis Vertical", counterAxis == static_cast<int>(CounterAxis::Vertical))) {
        playerObs_.counterAxis = CounterAxis::Vertical;
    }
    if (ImGui::RadioButton("CounterAxis Horizontal", counterAxis == static_cast<int>(CounterAxis::Horizontal))) {
        playerObs_.counterAxis = CounterAxis::Horizontal;
    }

    ImGui::Separator();
    ImGui::Text("Trigger Motion");
    if (ImGui::Button("Smash")) {
        lockIdle_ = false;
        enemy_.DebugStartAction(ActionKind::Smash);
        SyncEnemyAnimation();
    }
    ImGui::SameLine();
    if (ImGui::Button("Sweep")) {
        lockIdle_ = false;
        enemy_.DebugStartAction(ActionKind::Sweep);
        SyncEnemyAnimation();
    }
    ImGui::SameLine();
    if (ImGui::Button("Shot")) {
        lockIdle_ = false;
        enemy_.DebugStartAction(ActionKind::Shot);
        SyncEnemyAnimation();
    }
    ImGui::SameLine();
    if (ImGui::Button("Wave")) {
        lockIdle_ = false;
        enemy_.DebugStartAction(ActionKind::Wave);
        SyncEnemyAnimation();
    }
    ImGui::SameLine();
    if (ImGui::Button("Warp")) {
        lockIdle_ = false;
        enemy_.DebugStartAction(ActionKind::Warp);
        SyncEnemyAnimation();
    }

    if (ImGui::Button("Stalk")) {
        lockIdle_ = false;
        enemy_.DebugStartAction(ActionKind::Stalk);
        SyncEnemyAnimation();
    }

    ImGui::Separator();
    ImGui::Text("Current: %s / %s",
                GetActionKindName(enemy_.GetActionKind()),
                GetActionStepName(enemy_.GetActionStep()));
    ImGui::Text("Phase  : %s", GetPhaseName(enemy_.GetBossPhase()));
    ImGui::Text("Intro  : %s", enemy_.IsIntroActive() ? "true" : "false");
    ImGui::Text("Attack : %s", enemy_.IsAttackActive() ? "active" : "inactive");
    ImGui::Text("Dist   : %.2f", enemy_.GetDistanceToPlayer());

#ifdef _DEBUG
    ImGui::Separator();
    ImGui::Text("Camera");
    if (ImGui::Button("Normal")) {
        currentCamera_ = &camera_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Debug")) {
        currentCamera_ = &debugCamera_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Tripod")) {
        currentCamera_ = &tripodCamera_;
    }
    ImGui::DragFloat3("Tripod Pos", &tripodPos_.x, 0.05f, -20.0f, 20.0f);
    ImGui::DragFloat3("Tripod Target", &tripodTarget_.x, 0.05f, -20.0f, 20.0f);
#endif

    ImGui::End();
#endif
}

const char *EnemyMotionDebugScene::GetActionKindName(ActionKind kind) const {
    switch (kind) {
    case ActionKind::Smash:
        return "Smash";
    case ActionKind::Sweep:
        return "Sweep";
    case ActionKind::Shot:
        return "Shot";
    case ActionKind::Wave:
        return "Wave";
    case ActionKind::Warp:
        return "Warp";
    case ActionKind::Stalk:
        return "Stalk";
    case ActionKind::None:
    default:
        return "None";
    }
}

const char *EnemyMotionDebugScene::GetActionStepName(ActionStep step) const {
    switch (step) {
    case ActionStep::Charge:
        return "Charge";
    case ActionStep::Active:
        return "Active";
    case ActionStep::Recovery:
        return "Recovery";
    case ActionStep::Start:
        return "Start";
    case ActionStep::Move:
        return "Move";
    case ActionStep::Hold:
        return "Hold";
    case ActionStep::End:
        return "End";
    case ActionStep::None:
    default:
        return "None";
    }
}

const char *EnemyMotionDebugScene::GetPhaseName(BossPhase phase) const {
    switch (phase) {
    case BossPhase::Phase2:
        return "Phase2";
    case BossPhase::Phase1:
    default:
        return "Phase1";
    }
}
