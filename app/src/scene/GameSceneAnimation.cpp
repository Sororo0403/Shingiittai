#include "GameScene.h"
#include "BladeClashCinematic.h"
#include "ModelManager.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace DirectX;

static const std::string kBossAnimIdle = "Action";
static const std::string kBossAnimMove = "Action.001";
static const std::string kBossAnimSweep =
    "\xE6\xA8\xAA\xE8\x96\x99\xE3\x81\x8E\xE6\x89\x95\xE3\x81\x84";
static const std::string kBossAnimSmash =
    "\xE7\xB8\xA6\xE6\x8C\xAF\xE3\x82\x8A\xE4\xB8\x8B\xE3\x82\x8D\xE3\x81\x97";
static const std::string kBossAnimTeleport =
    "\xE3\x83\x86\xE3\x83\xAC\xE3\x83\x9D\xE3\x83\xBC\xE3\x83\x88";
static const std::string kBossAnimPhaseChange =
    "\xE7\xAC\xAC\xE4\xBA\x8C\xE5\xBD\xA2\xE6\x85\x8B\xE7\xA7\xBB\xE8\xA1\x8C";
static const std::string kBossBoneBase =
    "\xE3\x83\x9C\xE3\x83\xBC\xE3\x83\xB3";
static constexpr float kBladeClashActivePoseClipRatio = 0.18f;
static constexpr float kBladeClashGuardPoseClipRatio = 0.18f;
static constexpr float kBladeClashGuardOpenClipRatio = 0.025f;
static constexpr float kBladeClashStartupClipStartRatio = 0.025f;
static constexpr float kBladeClashStartupPoseTime = 0.32f;
namespace Clash = BladeClashCinematic;

static std::string BossBoneName(const char *suffix) {
    return suffix ? (kBossBoneBase + suffix) : kBossBoneBase;
}

static float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }

static float Smooth01(float value) {
    const float t = Clamp01(value);
    return t * t * (3.0f - 2.0f * t);
}

static float GetChargeStanceSettleTime(ActionKind kind) {
    switch (kind) {
    case ActionKind::Smash:
        return 0.46f;
    case ActionKind::Sweep:
        return 0.42f;
    default:
        return 0.42f;
    }
}

static bool IsChargeStanceSettled(ActionKind kind, ActionStep step,
                                  float timer) {
    if (step == ActionStep::Hold) {
        return true;
    }
    if (step != ActionStep::Charge) {
        return false;
    }
    return timer >= GetChargeStanceSettleTime(kind);
}

static int FindBoneIndex(const Model &model, const std::string &name) {
    const auto it = model.boneMap.find(name);
    if (it == model.boneMap.end()) {
        return -1;
    }
    return static_cast<int>(it->second);
}

static XMFLOAT3 GetMatrixTranslation(const XMFLOAT4X4 &matrix) {
    return {matrix._41, matrix._42, matrix._43};
}

static XMMATRIX MakePivotRotation(const XMFLOAT3 &pivot, float pitch, float yaw,
                                  float roll) {
    return XMMatrixTranslation(-pivot.x, -pivot.y, -pivot.z) *
           XMMatrixRotationRollPitchYaw(pitch, yaw, roll) *
           XMMatrixTranslation(pivot.x, pivot.y, pivot.z);
}

static void ApplyDeltaToBoneTree(Model &model, int boneIndex,
                                 const XMMATRIX &delta) {
    if (boneIndex < 0 ||
        static_cast<size_t>(boneIndex) >= model.skeletonSpaceMatrices.size()) {
        return;
    }

    std::vector<int> stack;
    stack.push_back(boneIndex);

    while (!stack.empty()) {
        const int current = stack.back();
        stack.pop_back();

        XMMATRIX currentMatrix =
            XMLoadFloat4x4(&model.skeletonSpaceMatrices[current]);
        XMStoreFloat4x4(&model.skeletonSpaceMatrices[current],
                        currentMatrix * delta);

        for (size_t i = 0; i < model.bones.size(); ++i) {
            if (model.bones[i].parentIndex == current) {
                stack.push_back(static_cast<int>(i));
            }
        }
    }
}

static void PoseBoneTree(Model &model, int boneIndex, float pitch, float yaw,
                         float roll) {
    if (boneIndex < 0 ||
        static_cast<size_t>(boneIndex) >= model.skeletonSpaceMatrices.size()) {
        return;
    }

    const XMFLOAT3 pivot =
        GetMatrixTranslation(model.skeletonSpaceMatrices[boneIndex]);
    ApplyDeltaToBoneTree(model, boneIndex,
                         MakePivotRotation(pivot, pitch, yaw, roll));
}

static bool HasAnimation(const Model *model, const std::string &animationName) {
    if (!model) {
        return false;
    }

    return model->animations.find(animationName) != model->animations.end();
}

static std::string PickFirstAnimation(const Model *model,
                                      const std::vector<std::string> &names) {
    for (const std::string &name : names) {
        if (HasAnimation(model, name)) {
            return name;
        }
    }
    return {};
}

static bool ScrubAnimationClip(ModelManager *modelManager, uint32_t modelId,
                               Model *model, const std::string &animationName,
                               float clipRatio, bool loop,
                               std::string &currentName,
                               bool &currentLoop) {
    if (!modelManager || !model || animationName.empty()) {
        return false;
    }

    const auto clipIt = model->animations.find(animationName);
    if (clipIt == model->animations.end() || clipIt->second.duration <= 0.0f) {
        return false;
    }

    if (currentName != animationName || currentLoop != loop) {
        modelManager->PlayAnimation(modelId, animationName, loop);
        currentName = animationName;
        currentLoop = loop;
    }

    model->animationTime =
        clipIt->second.duration * std::clamp(clipRatio, 0.0f, 1.0f);
    model->isLoop = loop;
    model->isPlaying = false;
    model->animationFinished = !loop && clipRatio >= 1.0f;
    modelManager->UpdateAnimation(modelId, 0.0f);
    return true;
}

static std::string PickEnemyAnimation(const Model *model, const Enemy &enemy,
                                      bool &outLoop) {
    outLoop = true;

    if (!model || model->animations.empty()) {
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

    case ActionKind::BladeClash:
        outLoop = false;
        if (HasAnimation(model, kBossAnimSmash)) {
            return kBossAnimSmash;
        }
        if (HasAnimation(model, kBossAnimSweep)) {
            return kBossAnimSweep;
        }
        break;

    case ActionKind::Stalk:
        if (HasAnimation(model, kBossAnimMove)) {
            return kBossAnimMove;
        }
        break;

    case ActionKind::None:
    default:
        if (enemy.GetIsPhaseChanging()) {
            outLoop = false;
            if (HasAnimation(model, kBossAnimPhaseChange)) {
                return kBossAnimPhaseChange;
            }
        }

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

    return {};
}

float GameScene::ComputeGameplayTimeScale() const {
    return combatFeedback_.GetGameplayTimeScale();
}

void GameScene::SetEnemyAnimationFrozen(bool frozen) {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr) {
        enemyAnimationFrozen_ = frozen;
        return;
    }

    Model *enemyModel = ctx_->rendering.model->GetModel(enemyModelId_);
    if (enemyModel == nullptr) {
        enemyAnimationFrozen_ = frozen;
        return;
    }

    if (frozen) {
        if (HasAnimation(enemyModel, kBossAnimIdle)) {
            ctx_->rendering.model->PlayAnimation(enemyModelId_, kBossAnimIdle, true);
            ctx_->rendering.model->UpdateAnimation(enemyModelId_, 0.0f);
            enemyAnimationName_ = kBossAnimIdle;
            enemyAnimationLoop_ = true;
        } else {
            enemyModel->currentAnimation.clear();
            enemyModel->animationTime = 0.0f;
            ctx_->rendering.model->UpdateAnimation(enemyModelId_, 0.0f);
            enemyAnimationName_.clear();
            enemyAnimationLoop_ = true;
        }
        enemyModel->isPlaying = false;
        enemyModel->animationTime = 0.0f;
        enemyModel->animationFinished = false;
        enemyAnimationFrozen_ = true;
        return;
    }

    if (enemyAnimationFrozen_ && !enemyModel->animationFinished &&
        !enemyModel->currentAnimation.empty()) {
        enemyModel->isPlaying = true;
    }
    enemyAnimationFrozen_ = false;
}

void GameScene::SyncEnemyAnimation() {
    ModelManager *modelManager = ctx_->rendering.model;
    Model *enemyModel = modelManager->GetModel(enemyModelId_);
    if (!enemyModel || enemyModel->animations.empty()) {
        return;
    }

    bool shouldLoop = true;
    std::string nextAnimation = PickEnemyAnimation(enemyModel, enemy_, shouldLoop);

    if (nextAnimation.empty()) {
        if (!enemyAnimationName_.empty() ||
            !enemyModel->currentAnimation.empty()) {
            enemyModel->currentAnimation.clear();
            enemyModel->animationTime = 0.0f;
            enemyModel->isLoop = true;
            enemyModel->isPlaying = false;
            enemyModel->animationFinished = false;
            modelManager->UpdateAnimation(enemyModelId_, 0.0f);
            enemyAnimationName_.clear();
            enemyAnimationLoop_ = true;
        }
        return;
    }

    if (enemyAnimationName_ == nextAnimation && enemyAnimationLoop_ == shouldLoop) {
        return;
    }

    modelManager->PlayAnimation(enemyModelId_, nextAnimation, shouldLoop);
    modelManager->UpdateAnimation(enemyModelId_, 0.0f);
    enemyAnimationName_ = nextAnimation;
    enemyAnimationLoop_ = shouldLoop;
}

void GameScene::UpdateBladeClashEnemyAnimation(float deltaTime) {
    (void)deltaTime;
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr) {
        return;
    }

    ModelManager *modelManager = ctx_->rendering.model;
    Model *enemyModel = modelManager->GetModel(enemyModelId_);
    if (enemyModel == nullptr || enemyModel->animations.empty()) {
        return;
    }

    const bool hasTeleport = HasAnimation(enemyModel, kBossAnimTeleport);
    const bool hasSweep = HasAnimation(enemyModel, kBossAnimSweep);
    const bool hasSmash = HasAnimation(enemyModel, kBossAnimSmash);
    if (!hasTeleport && !hasSweep && !hasSmash) {
        return;
    }

    std::string clip =
        hasTeleport ? kBossAnimTeleport : hasSweep ? kBossAnimSweep : kBossAnimSmash;
    float clipRatio =
        hasTeleport ? kBladeClashActivePoseClipRatio : hasSweep ? 0.62f : 0.42f;

    const bool bladeClashStartupAnim =
        !bladeClashActive_ && !bladeClashFinishActive_ &&
        enemy_.GetActionKind() == ActionKind::BladeClash &&
        enemy_.GetActionStep() == ActionStep::Charge;
    if (bladeClashStartupAnim) {
        const float startupT =
            Smooth01(enemy_.GetActionTimerForPresentation() /
                     kBladeClashStartupPoseTime);
        if (hasTeleport) {
            clip = kBossAnimTeleport;
            clipRatio =
                kBladeClashStartupClipStartRatio +
                (kBladeClashActivePoseClipRatio -
                 kBladeClashStartupClipStartRatio) *
                    startupT;
        } else if (hasSweep) {
            clip = kBossAnimSweep;
            clipRatio = 0.18f + 0.30f * startupT;
        } else {
            clip = kBossAnimSmash;
            clipRatio = 0.16f + 0.28f * startupT;
        }
    } else if (bladeClashFinishActive_ && bladeClashFinishPlayerWon_ &&
               bladeClashFinishTimer_ < Clash::kWinGuardBreakLead) {
        const float guardBreakT = Smooth01(
            (bladeClashFinishTimer_ - 0.04f) /
            (Clash::kWinGuardBreakLead - 0.04f));
        if (hasTeleport) {
            clip = kBossAnimTeleport;
            clipRatio =
                kBladeClashGuardPoseClipRatio +
                (kBladeClashGuardOpenClipRatio -
                 kBladeClashGuardPoseClipRatio) *
                    guardBreakT;
        } else if (hasSweep) {
            clip = kBossAnimSweep;
            clipRatio = 0.58f - 0.20f * guardBreakT;
        } else {
            clip = kBossAnimSmash;
            clipRatio = 0.34f - 0.12f * guardBreakT;
        }
    } else if (bladeClashFinishActive_ && bladeClashFinishPlayerWon_) {
        const float finishActionTimer =
            (std::max)(0.0f,
                       bladeClashFinishTimer_ - Clash::kWinGuardBreakLead) /
            Clash::kWinActionSlow;
        if (hasSmash) {
            clip = kBossAnimSmash;
            const float hit = Smooth01((finishActionTimer - 0.04f) / 0.24f);
            const float fall = Smooth01((finishActionTimer - 0.30f) / 0.54f);
            clipRatio = 0.32f + 0.26f * hit + 0.16f * fall;
        } else if (hasSweep) {
            clip = kBossAnimSweep;
            const float hit = Smooth01((finishActionTimer - 0.06f) / 0.20f);
            const float launch =
                Smooth01((finishActionTimer - 0.18f) / 0.42f);
            const float slam = Smooth01((finishActionTimer - 0.52f) / 0.44f);
            clipRatio = 0.42f + 0.18f * hit + 0.14f * launch + 0.08f * slam;
        } else if (hasTeleport) {
            clip = kBossAnimTeleport;
            const float hit = Smooth01((finishActionTimer - 0.06f) / 0.34f);
            clipRatio = kBladeClashActivePoseClipRatio + 0.18f * hit;
        }
        clipRatio = Clamp01(clipRatio);
    }

    if (!ScrubAnimationClip(modelManager, enemyModelId_, enemyModel, clip,
                            clipRatio, false, enemyAnimationName_,
                            enemyAnimationLoop_)) {
        modelManager->UpdateAnimation(enemyModelId_, 0.0f);
    }
    enemyAnimationFrozen_ = false;
}

void GameScene::UpdateBattleIntroEnemyAnimation(float) {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr) {
        return;
    }

    ModelManager *modelManager = ctx_->rendering.model;
    Model *enemyModel = modelManager->GetModel(enemyModelId_);
    if (enemyModel == nullptr || enemyModel->animations.empty()) {
        return;
    }

    const float t = battleIntroTimer_;
    std::string clip{};
    float clipRatio = 0.0f;

    if (t < 2.25f) {
        const float phase = Smooth01(t / 2.25f);
        clip = PickFirstAnimation(enemyModel, {kBossAnimTeleport,
                                               kBossAnimPhaseChange});
        clipRatio = 0.02f + 0.58f * phase;
    } else {
        const float phase = Smooth01((t - 2.25f) /
                                     (battleIntroDuration_ - 2.25f));
        clip = PickFirstAnimation(enemyModel, {kBossAnimPhaseChange,
                                               kBossAnimIdle});
        clipRatio = 0.08f + 0.64f * phase;
    }

    if (!ScrubAnimationClip(modelManager, enemyModelId_, enemyModel, clip,
                            clipRatio, false, enemyAnimationName_,
                            enemyAnimationLoop_)) {
        enemyModel->currentAnimation.clear();
        enemyModel->animationTime = 0.0f;
        enemyModel->isLoop = false;
        enemyModel->isPlaying = false;
        enemyModel->animationFinished = false;
        enemyAnimationName_.clear();
        enemyAnimationLoop_ = false;
        modelManager->UpdateAnimation(enemyModelId_, 0.0f);
    }
}

void GameScene::UpdateReadyPreviewEnemyAnimation() {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr) {
        return;
    }

    ModelManager *modelManager = ctx_->rendering.model;
    Model *enemyModel = modelManager->GetModel(enemyModelId_);
    if (enemyModel == nullptr || enemyModel->animations.empty()) {
        return;
    }

    const float heat = Clamp01(readyPreviewHeat_);
    const bool hasTeleport = HasAnimation(enemyModel, kBossAnimTeleport);
    const bool hasPhaseChange = HasAnimation(enemyModel, kBossAnimPhaseChange);
    std::string clip{};
    float clipRatio = 0.0f;

    if (hasPhaseChange && heat >= 0.20f) {
        clip = kBossAnimPhaseChange;
        const float phase = Smooth01((heat - 0.20f) / 0.80f);
        constexpr float kChargeStart = 0.10f;
        constexpr float kChargeEnd = 0.34f;
        constexpr float kReleaseStart = 0.38f;
        constexpr float kReleaseMoment = 0.92f;
        if (phase < 0.44f) {
            const float charge = Smooth01(phase / 0.44f);
            clipRatio = kChargeStart + (kChargeEnd - kChargeStart) * charge;
        } else {
            const float release = Smooth01((phase - 0.44f) / 0.56f);
            clipRatio =
                kReleaseStart + (kReleaseMoment - kReleaseStart) * release;
        }
    } else if (hasTeleport) {
        clip = kBossAnimTeleport;
        const float stance = Smooth01(heat / 0.20f);
        clipRatio = kBladeClashGuardPoseClipRatio + 0.08f * stance;
    } else {
        clip = PickFirstAnimation(enemyModel, {kBossAnimPhaseChange,
                                               kBossAnimIdle,
                                               kBossAnimMove});
        clipRatio = 0.0f;
    }

    if (!ScrubAnimationClip(modelManager, enemyModelId_, enemyModel, clip,
                            clipRatio, false, enemyAnimationName_,
                            enemyAnimationLoop_)) {
        modelManager->UpdateAnimation(enemyModelId_, 0.0f);
    }
    enemyAnimationFrozen_ = false;
}

void GameScene::UpdatePhaseTransitionEnemyAnimation(float deltaTime) {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr) {
        return;
    }

    ModelManager *modelManager = ctx_->rendering.model;
    Model *enemyModel = modelManager->GetModel(enemyModelId_);
    if (enemyModel == nullptr || enemyModel->animations.empty()) {
        return;
    }

    if (!HasAnimation(enemyModel, kBossAnimPhaseChange)) {
        SyncEnemyAnimation();
        SetEnemyAnimationFrozen(false);
        modelManager->UpdateAnimation(enemyModelId_, deltaTime * 0.35f);
        return;
    }

    if (enemyAnimationName_ != kBossAnimPhaseChange || enemyAnimationLoop_) {
        modelManager->PlayAnimation(enemyModelId_, kBossAnimPhaseChange, false);
        enemyAnimationName_ = kBossAnimPhaseChange;
        enemyAnimationLoop_ = false;
    }

    const auto clipIt = enemyModel->animations.find(kBossAnimPhaseChange);
    if (clipIt == enemyModel->animations.end() || clipIt->second.duration <= 0.0f) {
        modelManager->UpdateAnimation(enemyModelId_, 0.0f);
        return;
    }

    const float ratio = Clamp01(enemy_.GetPhaseTransitionRatio());
    constexpr float kReleaseStart = 0.88f;
    constexpr float kReleaseDuration = 0.05f;
    constexpr float kChargeClipStart = 0.00f;
    constexpr float kChargeClipEnd = 0.30f;
    constexpr float kReleaseClipStart = 0.36f;
    constexpr float kReleaseClipEnd = 0.99f;

    float clipRatio = 0.0f;
    if (ratio < kReleaseStart) {
        const float charge = Smooth01(ratio / kReleaseStart);
        clipRatio =
            kChargeClipStart + (kChargeClipEnd - kChargeClipStart) * charge;
    } else if (ratio < kReleaseStart + kReleaseDuration) {
        const float release =
            Smooth01((ratio - kReleaseStart) / kReleaseDuration);
        clipRatio =
            kReleaseClipStart + (kReleaseClipEnd - kReleaseClipStart) * release;
    } else {
        const float settle =
            Smooth01((ratio - kReleaseStart - kReleaseDuration) /
                     (1.0f - kReleaseStart - kReleaseDuration));
        clipRatio = kReleaseClipEnd + (1.0f - kReleaseClipEnd) * settle;
    }

    enemyModel->animationTime =
        clipIt->second.duration * std::clamp(clipRatio, 0.0f, 1.0f);
    enemyModel->isLoop = false;
    enemyModel->isPlaying = false;
    enemyModel->animationFinished = ratio >= 1.0f;
    enemyAnimationFrozen_ = false;
    modelManager->UpdateAnimation(enemyModelId_, 0.0f);
}

void GameScene::ApplyEnemyProceduralAnimation() {
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr) {
        return;
    }

    Model *enemyModel = ctx_->rendering.model->GetModel(enemyModelId_);
    if (enemyModel == nullptr || enemyModel->bones.empty() ||
        enemyModel->skeletonSpaceMatrices.empty()) {
        return;
    }

    const int root = FindBoneIndex(*enemyModel, BossBoneName(nullptr));
    const int spine = FindBoneIndex(*enemyModel, BossBoneName(".003"));
    const int chest = FindBoneIndex(*enemyModel, BossBoneName(".013"));
    const int head = FindBoneIndex(*enemyModel, BossBoneName(".004"));
    const int headTip = FindBoneIndex(*enemyModel, BossBoneName(".007"));
    const int upperA = FindBoneIndex(*enemyModel, BossBoneName(".014"));
    const int upperB = FindBoneIndex(*enemyModel, BossBoneName(".017"));
    const int upperC = FindBoneIndex(*enemyModel, BossBoneName(".018"));
    const int upperD = FindBoneIndex(*enemyModel, BossBoneName(".019"));

    const float time = enemyModel->animationTime;
    const float pulse = std::sin(time * 7.5f);
    const float slowPulse = std::sin(time * 3.2f);
    const ActionKind action = enemy_.GetActionKind();
    const ActionStep step = enemy_.GetActionStep();
    const bool phase2Plus = enemy_.GetBossPhase() != BossPhase::Phase1;
    const bool phase3 = enemy_.GetBossPhase() == BossPhase::Phase3;
    const float phaseScale = phase3 ? 1.32f : phase2Plus ? 1.18f : 1.0f;
    const float actionTimer = enemy_.GetActionTimerForPresentation();
    const float stanceSettleTime = GetChargeStanceSettleTime(action);
    const bool chargeSettled =
        IsChargeStanceSettled(action, step, actionTimer);
    const float chargePose =
        step == ActionStep::Hold
            ? 1.0f
            : (step == ActionStep::Charge
                   ? Smooth01(actionTimer / stanceSettleTime)
                   : 0.0f);
    const float idleMotion = chargeSettled ? 0.10f : 1.0f;

    auto poseArms = [&](float pitch, float yaw, float roll) {
        PoseBoneTree(*enemyModel, upperA, pitch, yaw, roll);
        PoseBoneTree(*enemyModel, upperB, pitch * 0.76f, yaw * 0.62f,
                     roll * 0.82f);
        PoseBoneTree(*enemyModel, upperC, pitch * 0.62f, -yaw * 0.54f,
                     -roll * 0.70f);
        PoseBoneTree(*enemyModel, upperD, pitch * 0.48f, -yaw * 0.42f,
                     -roll * 0.56f);
    };

    PoseBoneTree(*enemyModel, spine, -0.025f * slowPulse * idleMotion, 0.0f,
                 0.025f * pulse * idleMotion);
    PoseBoneTree(*enemyModel, chest, 0.018f * pulse * idleMotion,
                 0.018f * slowPulse * idleMotion,
                 -0.020f * pulse * idleMotion);
    PoseBoneTree(*enemyModel, head, 0.025f * slowPulse * idleMotion,
                 0.020f * pulse * idleMotion, 0.0f);
    PoseBoneTree(*enemyModel, headTip, 0.020f * slowPulse * idleMotion, 0.0f,
                 0.0f);

    switch (action) {
    case ActionKind::Smash:
        if (step == ActionStep::Charge || step == ActionStep::Hold) {
            const float hold = 0.98f + 0.02f * pulse;
            PoseBoneTree(*enemyModel, root,
                         -0.08f * phaseScale * chargePose, 0.0f, 0.0f);
            PoseBoneTree(*enemyModel, chest,
                         -0.20f * phaseScale * chargePose, 0.0f,
                         0.012f * hold * chargePose);
            poseArms(-0.58f * phaseScale * chargePose,
                     0.08f * chargePose, 0.18f * hold * chargePose);
        } else if (step == ActionStep::Active) {
            const float strike = Smooth01(time / 0.18f);
            PoseBoneTree(*enemyModel, root, 0.10f * phaseScale, 0.0f, 0.0f);
            PoseBoneTree(*enemyModel, chest, 0.34f * phaseScale * strike,
                         0.0f, -0.06f);
            poseArms(0.72f * phaseScale * strike, 0.04f,
                     -0.16f * phaseScale);
        } else if (step == ActionStep::Recovery) {
            PoseBoneTree(*enemyModel, chest, 0.12f, 0.0f, -0.04f);
            poseArms(0.20f, 0.0f, -0.08f);
        }
        break;

    case ActionKind::Sweep:
        if (step == ActionStep::Charge || step == ActionStep::Hold) {
            PoseBoneTree(*enemyModel, chest, -0.06f * chargePose,
                         -0.30f * phaseScale * chargePose,
                         -0.025f * chargePose);
            poseArms(-0.05f * chargePose,
                     -0.34f * phaseScale * chargePose, 0.30f * chargePose);
        } else if (step == ActionStep::Active) {
            const float strike = Smooth01(time / 0.24f);
            PoseBoneTree(*enemyModel, root, 0.0f, 0.18f * phaseScale * strike,
                         0.0f);
            PoseBoneTree(*enemyModel, chest, 0.04f, 0.56f * phaseScale * strike,
                         0.14f);
            poseArms(0.08f, 0.74f * phaseScale * strike,
                     -0.28f * phaseScale);
        } else if (step == ActionStep::Recovery) {
            PoseBoneTree(*enemyModel, chest, 0.04f, 0.18f, 0.05f);
            poseArms(0.02f, 0.24f, -0.10f);
        }
        break;

    case ActionKind::BladeClash:
        if (step == ActionStep::Charge) {
            const float brace = Smooth01(actionTimer / 0.62f);
            PoseBoneTree(*enemyModel, root, -0.06f * phaseScale * brace, 0.0f,
                         0.0f);
            PoseBoneTree(*enemyModel, chest,
                         -0.14f * phaseScale * brace, 0.0f,
                         0.04f * pulse * brace);
            poseArms(-0.24f * phaseScale * brace, 0.0f,
                     0.18f * phaseScale * brace);
        } else if (step == ActionStep::Active) {
            const float clash = 0.82f + 0.18f * pulse;
            PoseBoneTree(*enemyModel, root, 0.05f * phaseScale, 0.0f,
                         0.02f * slowPulse);
            PoseBoneTree(*enemyModel, chest, 0.18f * phaseScale * clash,
                         0.0f, 0.05f * pulse);
            poseArms(0.36f * phaseScale * clash, 0.0f,
                     -0.22f * phaseScale);
        } else if (step == ActionStep::Recovery) {
            PoseBoneTree(*enemyModel, chest, 0.08f, 0.0f, -0.03f);
            poseArms(0.16f, 0.0f, -0.06f);
        }
        break;

    case ActionKind::Stalk:
        PoseBoneTree(*enemyModel, root, 0.04f * slowPulse, 0.0f,
                     0.06f * pulse);
        PoseBoneTree(*enemyModel, chest, -0.04f, 0.08f * pulse,
                     -0.05f * slowPulse);
        poseArms(0.06f * pulse, 0.12f * slowPulse, 0.10f * pulse);
        break;

    case ActionKind::None:
    default:
        poseArms(0.035f * pulse, 0.025f * slowPulse, 0.045f * pulse);
        break;
    }

    ctx_->rendering.model->GetRenderer()->UpdateSkinClusters(*enemyModel);
}
