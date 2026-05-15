#include "GameScene.h"
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
static const std::string kBossAnimWave =
    "\xE6\xB3\xA2\xE7\x8A\xB6\xE6\x94\xBB\xE6\x92\x83";
static const std::string kBossAnimSmash =
    "\xE7\xB8\xA6\xE6\x8C\xAF\xE3\x82\x8A\xE4\xB8\x8B\xE3\x82\x8D\xE3\x81\x97";
static const std::string kBossAnimShot =
    "\xE5\xBC\xBE\xE7\x99\xBA\xE5\xB0\x84";
static const std::string kBossAnimTeleport =
    "\xE3\x83\x86\xE3\x83\xAC\xE3\x83\x9D\xE3\x83\xBC\xE3\x83\x88";
static const std::string kBossAnimPhaseChange =
    "\xE7\xAC\xAC\xE4\xBA\x8C\xE5\xBD\xA2\xE6\x85\x8B\xE7\xA7\xBB\xE8\xA1\x8C";
static const std::string kBossBoneBase =
    "\xE3\x83\x9C\xE3\x83\xBC\xE3\x83\xB3";

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
    case ActionKind::Shot:
    case ActionKind::BladeClash:
    case ActionKind::Wave:
    case ActionKind::Cage:
        return 0.48f;
    case ActionKind::Nova:
        return 0.64f;
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
        if (HasAnimation(model, kBossAnimSweep)) {
            return kBossAnimSweep;
        }
        if (HasAnimation(model, kBossAnimShot)) {
            return kBossAnimShot;
        }
        break;

    case ActionKind::Shot:
        outLoop = false;
        if (HasAnimation(model, kBossAnimShot)) {
            return kBossAnimShot;
        }
        if (HasAnimation(model, kBossAnimWave)) {
            return kBossAnimWave;
        }
        break;

    case ActionKind::Wave:
    case ActionKind::Cage:
    case ActionKind::Nova:
        outLoop = false;
        if (HasAnimation(model, kBossAnimWave)) {
            return kBossAnimWave;
        }
        break;

    case ActionKind::Stalk:
        if (HasAnimation(model, kBossAnimMove)) {
            return kBossAnimMove;
        }
        break;

    case ActionKind::Warp:
        outLoop = false;
        if (HasAnimation(model, kBossAnimTeleport)) {
            return kBossAnimTeleport;
        }
        if (HasAnimation(model, kBossAnimIdle)) {
            outLoop = true;
            return kBossAnimIdle;
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
    const float feedbackScale = combatFeedback_.GetGameplayTimeScale();
    const float focusScale =
        1.0f - (1.0f - chargeWeakPointFocusTimeScale_) *
                   chargeWeakPointFocusRatio_;
    return (std::min)(feedbackScale, focusScale);
}

void GameScene::SetEnemyAnimationFrozen(bool frozen) {
    if (ctx_ == nullptr || ctx_->model == nullptr) {
        enemyAnimationFrozen_ = frozen;
        return;
    }

    Model *enemyModel = ctx_->model->GetModel(enemyModelId_);
    if (enemyModel == nullptr) {
        enemyAnimationFrozen_ = frozen;
        return;
    }

    if (frozen) {
        if (HasAnimation(enemyModel, kBossAnimIdle)) {
            ctx_->model->PlayAnimation(enemyModelId_, kBossAnimIdle, true);
            ctx_->model->UpdateAnimation(enemyModelId_, 0.0f);
            enemyAnimationName_ = kBossAnimIdle;
            enemyAnimationLoop_ = true;
        } else {
            enemyModel->currentAnimation.clear();
            enemyModel->animationTime = 0.0f;
            ctx_->model->UpdateAnimation(enemyModelId_, 0.0f);
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
    ModelManager *modelManager = ctx_->model;
    Model *enemyModel = modelManager->GetModel(enemyModelId_);
    if (!enemyModel || enemyModel->animations.empty()) {
        return;
    }

    bool shouldLoop = true;
    std::string nextAnimation{};
    if (bladeClashFinishActive_) {
        shouldLoop = false;
        if (!bladeClashFinishPlayerWon_ && HasAnimation(enemyModel, kBossAnimSweep)) {
            nextAnimation = kBossAnimSweep;
        } else if (bladeClashFinishPlayerWon_ &&
                   HasAnimation(enemyModel, kBossAnimSmash)) {
            nextAnimation = kBossAnimSmash;
        } else if (HasAnimation(enemyModel, kBossAnimIdle)) {
            shouldLoop = true;
            nextAnimation = kBossAnimIdle;
        }
    } else {
        nextAnimation = PickEnemyAnimation(enemyModel, enemy_, shouldLoop);
    }

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

void GameScene::UpdatePhaseTransitionEnemyAnimation(float deltaTime) {
    if (ctx_ == nullptr || ctx_->model == nullptr) {
        return;
    }

    ModelManager *modelManager = ctx_->model;
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
    if (ctx_ == nullptr || ctx_->model == nullptr) {
        return;
    }

    Model *enemyModel = ctx_->model->GetModel(enemyModelId_);
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
    const bool phase2 = enemy_.GetBossPhase() == BossPhase::Phase2;
    const float phaseScale = phase2 ? 1.18f : 1.0f;
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

    if (bladeClashFinishActive_) {
        const float ratio =
            bladeClashFinishDuration_ > 0.0001f
                ? Clamp01(bladeClashFinishTimer_ / bladeClashFinishDuration_)
                : 1.0f;
        const float hit = std::sin(Clamp01(ratio / 0.16f) * 3.14159265f);
        if (bladeClashFinishPlayerWon_) {
            const float fall = Smooth01((ratio - 0.18f) / 0.72f);
            PoseBoneTree(*enemyModel, root,
                         0.12f * hit - 0.62f * fall,
                         0.10f * hit,
                         0.26f * hit + 0.48f * fall);
            PoseBoneTree(*enemyModel, spine,
                         0.24f * hit + 0.74f * fall,
                         -0.10f * fall,
                         -0.56f * hit - 0.36f * fall);
            PoseBoneTree(*enemyModel, chest,
                         0.38f * hit + 0.86f * fall,
                         -0.18f * fall,
                         -0.88f * hit - 0.42f * fall);
            PoseBoneTree(*enemyModel, head,
                         0.24f * hit + 0.42f * fall,
                         0.10f * hit,
                         -0.30f * hit - 0.18f * fall);
            poseArms(0.62f * hit + 0.34f * fall, -0.24f * hit,
                     0.72f * hit + 0.24f * fall);
        } else {
            const float followThrough = Smooth01((ratio - 0.24f) / 0.58f);
            PoseBoneTree(*enemyModel, root, 0.0f,
                         0.72f * hit + 0.28f * followThrough, 0.0f);
            PoseBoneTree(*enemyModel, spine, -0.12f * hit,
                         0.86f * hit + 0.22f * followThrough,
                         0.26f * hit);
            PoseBoneTree(*enemyModel, chest, -0.18f * hit,
                         1.18f * hit + 0.34f * followThrough,
                         0.42f * hit);
            poseArms(0.10f, 1.38f * hit + 0.30f * followThrough,
                     -0.68f * hit);
        }
        ctx_->model->GetRenderer()->UpdateSkinClusters(*enemyModel);
        return;
    }

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

    case ActionKind::Shot:
    case ActionKind::BladeClash:
    case ActionKind::Wave:
    case ActionKind::Cage:
        if (step == ActionStep::Charge) {
            PoseBoneTree(*enemyModel, chest, -0.05f * chargePose,
                         0.006f * pulse * chargePose,
                         0.008f * pulse * chargePose);
            poseArms(-0.22f * phaseScale * chargePose,
                     0.02f * pulse * chargePose, 0.06f * chargePose);
        } else {
            PoseBoneTree(*enemyModel, chest, 0.08f, 0.0f, 0.0f);
            poseArms(0.28f * phaseScale, 0.03f, -0.05f);
        }
        break;

    case ActionKind::Nova:
        if (step == ActionStep::Charge) {
            const float chargePulse = 0.98f + 0.02f * std::sin(time * 18.0f);
            PoseBoneTree(*enemyModel, root,
                         -0.12f * phaseScale * chargePose, 0.0f,
                         0.008f * chargePulse * chargePose);
            PoseBoneTree(*enemyModel, chest,
                         -0.34f * phaseScale * chargePose, 0.0f,
                         0.030f * chargePulse * chargePose);
            PoseBoneTree(*enemyModel, head, -0.10f * chargePose, 0.0f, 0.0f);
            poseArms(-0.70f * phaseScale * chargePose,
                     0.16f * chargePulse * chargePose,
                     0.34f * chargePulse * chargePose);
        } else if (step == ActionStep::Active) {
            const float burst = 0.7f + 0.3f * std::sin(time * 34.0f);
            PoseBoneTree(*enemyModel, root, 0.16f * phaseScale, 0.0f,
                         0.08f * burst);
            PoseBoneTree(*enemyModel, chest, 0.44f * phaseScale, 0.0f,
                         -0.20f * burst);
            PoseBoneTree(*enemyModel, head, 0.16f, 0.0f, 0.0f);
            poseArms(0.64f * phaseScale, 0.44f * burst,
                     -0.52f * phaseScale);
        } else {
            PoseBoneTree(*enemyModel, chest, 0.08f, 0.0f, -0.06f);
            poseArms(0.16f, 0.10f, -0.08f);
        }
        break;

    case ActionKind::Warp:
        PoseBoneTree(*enemyModel, root, -0.06f, 0.0f, 0.0f);
        PoseBoneTree(*enemyModel, chest, -0.12f, 0.08f * pulse,
                     0.14f * slowPulse);
        poseArms(-0.16f, 0.16f * pulse, 0.24f * slowPulse);
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

    ctx_->model->GetRenderer()->UpdateSkinClusters(*enemyModel);
}
