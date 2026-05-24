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
static const std::string kBossAnimTeleport =
    "\xE3\x83\x86\xE3\x83\xAC\xE3\x83\x9D\xE3\x83\xBC\xE3\x83\x88";
static const std::string kBossAnimPhaseChange =
    "\xE7\xAC\xAC\xE4\xBA\x8C\xE5\xBD\xA2\xE6\x85\x8B\xE7\xA7\xBB\xE8\xA1\x8C";
static const std::string kBossBoneBase =
    "\xE3\x83\x9C\xE3\x83\xBC\xE3\x83\xB3";
static constexpr float kBladeClashWinGuardBreakLead = 0.52f;
static constexpr float kBladeClashWinGuardBreakImpactTime = 0.24f;
static constexpr float kBladeClashWinActionSlow = 0.95f;
static constexpr float kBladeClashActivePoseClipRatio = 0.18f;
static constexpr float kBladeClashGuardPoseClipRatio = 0.18f;
static constexpr float kBladeClashGuardOpenClipRatio = 0.025f;
static constexpr float kBladeClashStartupClipStartRatio = 0.025f;
static constexpr float kBladeClashStartupPoseTime = 0.32f;
static constexpr float kCageCastClipStartRatio = 0.025f;
static constexpr float kCageCastClipHoldRatio = 0.22f;
static constexpr float kCageCastPoseTime = 0.62f;

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
    case ActionKind::BladeClash:
    case ActionKind::Wave:
    case ActionKind::Laser:
    case ActionKind::Cage:
        return 0.48f;
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
        if (HasAnimation(model, kBossAnimTeleport)) {
            return kBossAnimTeleport;
        }
        if (HasAnimation(model, kBossAnimSweep)) {
            return kBossAnimSweep;
        }
        break;

    case ActionKind::Wave:
        outLoop = false;
        if (HasAnimation(model, kBossAnimWave)) {
            return kBossAnimWave;
        }
        break;

    case ActionKind::Laser:
        outLoop = false;
        if (enemy.GetFarLaserFollowupKind() == ActionKind::Sweep &&
            HasAnimation(model, kBossAnimSweep)) {
            return kBossAnimSweep;
        }
        if (HasAnimation(model, kBossAnimSmash)) {
            return kBossAnimSmash;
        }
        if (HasAnimation(model, kBossAnimSweep)) {
            return kBossAnimSweep;
        }
        break;

    case ActionKind::Cage:
        outLoop = false;
        if (HasAnimation(model, kBossAnimTeleport)) {
            return kBossAnimTeleport;
        }
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
    std::string nextAnimation{};
    if (bladeClashActive_) {
        shouldLoop = false;
        if (HasAnimation(enemyModel, kBossAnimTeleport)) {
            nextAnimation = kBossAnimTeleport;
        } else if (HasAnimation(enemyModel, kBossAnimSweep)) {
            nextAnimation = kBossAnimSweep;
        } else if (HasAnimation(enemyModel, kBossAnimIdle)) {
            nextAnimation = kBossAnimIdle;
        }
    } else if (bladeClashFinishActive_) {
        if (bladeClashFinishPlayerWon_) {
            shouldLoop = true;
            if (HasAnimation(enemyModel, kBossAnimIdle)) {
                nextAnimation = kBossAnimIdle;
            }
        } else if (HasAnimation(enemyModel, kBossAnimSweep)) {
            shouldLoop = false;
            nextAnimation = kBossAnimSweep;
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
    const bool hasWave = HasAnimation(enemyModel, kBossAnimWave);
    if (!hasTeleport && !hasSweep && !hasSmash && !hasWave) {
        return;
    }

    std::string clip =
        hasTeleport ? kBossAnimTeleport : hasSweep ? kBossAnimSweep : kBossAnimWave;
    float clipRatio =
        hasTeleport ? kBladeClashActivePoseClipRatio : hasSweep ? 0.62f : 0.38f;
    const bool bladeClashStartupAnim =
        !bladeClashActive_ && !bladeClashFinishActive_ &&
        enemy_.GetActionKind() == ActionKind::BladeClash &&
        enemy_.GetActionStep() == ActionStep::Charge;
    const bool phase3GuardCounterAnim = enemy_.IsPhase3GuardCounterGuarding();
    if (bladeClashStartupAnim || phase3GuardCounterAnim) {
        const float startupT = Smooth01(
            enemy_.GetActionTimerForPresentation() /
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
        } else if (hasWave) {
            clip = kBossAnimWave;
            clipRatio = 0.08f + 0.30f * startupT;
        }
    } else if (bladeClashFinishActive_ && bladeClashFinishPlayerWon_ &&
        bladeClashFinishTimer_ < kBladeClashWinGuardBreakLead) {
        const float guardBreakT = Smooth01(
            (bladeClashFinishTimer_ - 0.04f) /
            (kBladeClashWinGuardBreakLead - 0.04f));
        if (hasTeleport) {
            clip = kBossAnimTeleport;
            clipRatio =
                kBladeClashGuardPoseClipRatio +
                (kBladeClashGuardOpenClipRatio -
                 kBladeClashGuardPoseClipRatio) *
                    guardBreakT;
        } else if (hasWave) {
            clip = kBossAnimWave;
            const float breakClipRatio =
                0.08f + 0.14f * guardBreakT;
            clipRatio = 0.38f + (breakClipRatio - 0.38f) * guardBreakT;
        }
    } else if (bladeClashFinishActive_ && bladeClashFinishPlayerWon_ &&
               !bladeClashFinal_) {
        const float finishActionTimer =
            (std::max)(0.0f, bladeClashFinishTimer_ -
                                  kBladeClashWinGuardBreakLead) /
            kBladeClashWinActionSlow;
        if (hasSmash) {
            clip = kBossAnimSmash;
            const float hit = Smooth01((finishActionTimer - 0.04f) / 0.24f);
            const float fall = Smooth01((finishActionTimer - 0.30f) / 0.54f);
            clipRatio = 0.32f + 0.26f * hit + 0.16f * fall;
        } else if (hasSweep) {
            clip = kBossAnimSweep;
            const float hit = Smooth01((finishActionTimer - 0.06f) / 0.20f);
            const float launch = Smooth01((finishActionTimer - 0.18f) / 0.42f);
            const float slam = Smooth01((finishActionTimer - 0.52f) / 0.44f);
            clipRatio = 0.42f + 0.18f * hit + 0.14f * launch +
                        0.08f * slam;
        } else if (hasWave) {
            clip = kBossAnimWave;
            const float hit = Smooth01((finishActionTimer - 0.06f) / 0.24f);
            const float fall = Smooth01((finishActionTimer - 0.42f) / 0.46f);
            clipRatio = 0.18f + 0.22f * hit + 0.16f * fall;
        } else if (hasTeleport) {
            clip = kBossAnimTeleport;
            const float hit = Smooth01((finishActionTimer - 0.06f) / 0.34f);
            clipRatio = kBladeClashActivePoseClipRatio + 0.18f * hit;
        }
        clipRatio = Clamp01(clipRatio);
    } else if (bladeClashFinishActive_ && bladeClashFinishPlayerWon_ &&
        bladeClashFinal_) {
        if (hasSmash) {
            clip = kBossAnimSmash;
        } else if (hasSweep) {
            clip = kBossAnimSweep;
        } else if (hasWave) {
            clip = kBossAnimWave;
        } else {
            clip = kBossAnimTeleport;
        }
        const float finishActionTimer =
            (std::max)(0.0f, bladeClashFinishTimer_ -
                                  kBladeClashWinGuardBreakLead) /
            kBladeClashWinActionSlow;
        const float finishActionDuration =
            (std::max)(0.001f, bladeClashFinishDuration_ -
                                  kBladeClashWinGuardBreakLead) /
            kBladeClashWinActionSlow;
        const float t = Clamp01(finishActionTimer / finishActionDuration);
        const float launch = Smooth01(Clamp01((finishActionTimer - 0.30f) /
                                              0.62f));
        const float bounce = Smooth01(Clamp01((finishActionTimer - 1.12f) /
                                              0.44f));
        const float slide = Smooth01(Clamp01((finishActionTimer - 1.84f) /
                                             0.72f));
        clipRatio = 0.18f + 0.30f * launch + 0.16f * bounce + 0.10f * slide;
        if (t > 0.82f) {
            clipRatio = 0.72f;
        }
        clipRatio = Clamp01(clipRatio);
    } else if (bladeClashFinal_) {
        const float t = bladeClashTimer_ > 0.0f
                            ? 1.0f - Clamp01(bladeClashTimer_ / 6.15f)
                            : 1.0f;
        if (hasSmash) {
            clip = kBossAnimSmash;
            clipRatio = 0.18f + 0.34f * Smooth01((t - 0.28f) / 0.58f);
        } else if (hasSweep) {
            clip = kBossAnimSweep;
            clipRatio = 0.24f + 0.30f * Smooth01((t - 0.30f) / 0.54f);
        } else if (hasWave) {
            clip = kBossAnimWave;
            clipRatio = 0.18f + 0.30f * Smooth01((t - 0.30f) / 0.54f);
        } else {
            clip = kBossAnimTeleport;
            clipRatio = 0.08f + 0.24f * Smooth01(t);
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

void GameScene::UpdateCageEnemyAnimation(float deltaTime) {
    (void)deltaTime;
    if (ctx_ == nullptr || ctx_->rendering.model == nullptr) {
        return;
    }

    ModelManager *modelManager = ctx_->rendering.model;
    Model *enemyModel = modelManager->GetModel(enemyModelId_);
    if (enemyModel == nullptr || enemyModel->animations.empty() ||
        enemy_.GetActionKind() != ActionKind::Cage) {
        return;
    }

    const bool hasTeleport = HasAnimation(enemyModel, kBossAnimTeleport);
    const bool hasWave = HasAnimation(enemyModel, kBossAnimWave);
    const bool hasSweep = HasAnimation(enemyModel, kBossAnimSweep);
    if (!hasTeleport && !hasWave && !hasSweep) {
        return;
    }

    const ActionStep step = enemy_.GetActionStep();
    const float timer = enemy_.GetActionTimerForPresentation();
    const float castT = Smooth01(timer / kCageCastPoseTime);
    std::string clip =
        hasTeleport ? kBossAnimTeleport : hasWave ? kBossAnimWave : kBossAnimSweep;
    float clipRatio = hasTeleport ? kCageCastClipStartRatio : 0.08f;

    if (step == ActionStep::Charge) {
        if (hasTeleport) {
            clip = kBossAnimTeleport;
            clipRatio = kCageCastClipStartRatio +
                        (kCageCastClipHoldRatio - kCageCastClipStartRatio) *
                            castT;
        } else if (hasWave) {
            clip = kBossAnimWave;
            clipRatio = 0.10f + 0.26f * castT;
        } else {
            clip = kBossAnimSweep;
            clipRatio = 0.12f + 0.20f * castT;
        }
    } else if (step == ActionStep::Active) {
        const float snapT = Smooth01(timer / 0.18f);
        if (hasTeleport) {
            clip = kBossAnimTeleport;
            clipRatio = kCageCastClipHoldRatio + 0.08f * snapT;
        } else if (hasWave) {
            clip = kBossAnimWave;
            clipRatio = 0.36f + 0.12f * snapT;
        } else {
            clip = kBossAnimSweep;
            clipRatio = 0.32f + 0.12f * snapT;
        }
    } else {
        clipRatio = hasTeleport ? kCageCastClipHoldRatio : 0.42f;
    }

    if (!ScrubAnimationClip(modelManager, enemyModelId_, enemyModel, clip,
                            Clamp01(clipRatio), false, enemyAnimationName_,
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
                                               kBossAnimWave});
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

    if (bladeClashActive_ || enemy_.GetActionKind() == ActionKind::BladeClash) {
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

    if (bladeClashFinishActive_ && bladeClashFinishPlayerWon_) {
        ctx_->rendering.model->GetRenderer()->UpdateSkinClusters(*enemyModel);
        return;
    }

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
        if (bladeClashFinishPlayerWon_ &&
            bladeClashFinishTimer_ < kBladeClashWinGuardBreakLead) {
            const float guardT =
                Clamp01(bladeClashFinishTimer_ / kBladeClashWinGuardBreakLead);
            const float guardLocked =
                1.0f - Smooth01((guardT - 0.36f) / 0.18f);
            const float open =
                1.0f - std::pow(1.0f - Clamp01((guardT - 0.50f) / 0.42f),
                                 3.0f);
            const float snap =
                std::sin(Clamp01((guardT - 0.50f) / 0.24f) * 3.14159265f);
            const float collapse =
                1.0f - std::pow(1.0f - Clamp01((guardT - 0.54f) / 0.38f),
                                 3.0f);
            PoseBoneTree(*enemyModel, root,
                         -0.052f * collapse, 0.0f,
                         0.032f * collapse);
            PoseBoneTree(*enemyModel, spine,
                         -0.135f * collapse, -0.022f * open,
                         -0.070f * collapse);
            PoseBoneTree(*enemyModel, chest,
                         -0.175f * collapse, -0.068f * open,
                         -0.094f * collapse);
            PoseBoneTree(*enemyModel, head,
                         -0.050f * collapse, 0.0f,
                         -0.024f * collapse);
            const float armBreak = 1.0f - guardLocked;
            poseArms((0.42f * open + 0.035f * snap) * armBreak,
                     0.92f * open * armBreak,
                     (-0.66f * open - 0.034f * snap -
                      0.028f * collapse) *
                         armBreak);
            ctx_->rendering.model->GetRenderer()->UpdateSkinClusters(*enemyModel);
            return;
        }

        const float finishActionTimer =
            bladeClashFinishPlayerWon_
                ? (std::max)(0.0f, bladeClashFinishTimer_ -
                                        kBladeClashWinGuardBreakLead) /
                      kBladeClashWinActionSlow
                : bladeClashFinishTimer_;
        const float finishActionDuration =
            bladeClashFinishPlayerWon_
                ? (std::max)(0.001f, bladeClashFinishDuration_ -
                                        kBladeClashWinGuardBreakLead) /
                      kBladeClashWinActionSlow
                : bladeClashFinishDuration_;
        const float ratio =
            finishActionDuration > 0.0001f
                ? Clamp01(finishActionTimer / finishActionDuration)
                : 1.0f;
        const float hit = std::sin(Clamp01(ratio / 0.16f) * 3.14159265f);
        if (bladeClashFinishPlayerWon_) {
            const float fall = Smooth01((ratio - 0.16f) / 0.62f);
            PoseBoneTree(*enemyModel, root,
                         0.10f * hit - 0.64f * fall,
                         0.08f * hit,
                         0.20f * hit + 0.46f * fall);
            PoseBoneTree(*enemyModel, spine,
                         0.18f * hit + 0.72f * fall,
                         -0.12f * fall,
                         -0.40f * hit - 0.38f * fall);
            PoseBoneTree(*enemyModel, chest,
                         0.30f * hit + 0.86f * fall,
                         -0.18f * fall,
                         -0.62f * hit - 0.44f * fall);
            PoseBoneTree(*enemyModel, head,
                         0.20f * hit + 0.46f * fall,
                         0.10f * hit,
                         -0.24f * hit - 0.20f * fall);
            poseArms(0.48f * hit + 0.34f * fall, -0.18f * hit,
                     0.54f * hit + 0.30f * fall);
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
        ctx_->rendering.model->GetRenderer()->UpdateSkinClusters(*enemyModel);
        return;
    }

    if (enemy_.IsPhase3GuardCounterGuarding()) {
        const float guard =
            Smooth01(actionTimer / (std::max)(0.001f, 0.22f));
        PoseBoneTree(*enemyModel, chest, -0.05f * guard,
                     0.006f * pulse * guard, 0.008f * pulse * guard);
        poseArms(-0.22f * phaseScale * guard, 0.02f * pulse * guard,
                 0.06f * guard);
        ctx_->rendering.model->GetRenderer()->UpdateSkinClusters(*enemyModel);
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

    case ActionKind::BladeClash:
    case ActionKind::Wave:
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

    case ActionKind::Cage:
        if (step == ActionStep::Charge) {
            const float drawIn = Smooth01((actionTimer - 0.22f) / 0.48f);
            const float sealPulse = (0.5f + 0.5f * pulse) * chargePose;
            PoseBoneTree(*enemyModel, root, -0.03f * chargePose, 0.0f, 0.0f);
            PoseBoneTree(*enemyModel, chest,
                         -0.13f * phaseScale * chargePose,
                         0.03f * slowPulse * chargePose,
                         0.02f * pulse * chargePose);
            PoseBoneTree(*enemyModel, head, -0.03f * chargePose,
                         0.012f * pulse * chargePose, 0.0f);
            poseArms((-0.28f - 0.10f * drawIn) * phaseScale * chargePose,
                     (0.36f - 0.58f * drawIn) * phaseScale * chargePose,
                     (0.30f + 0.22f * drawIn + 0.04f * sealPulse) *
                         chargePose);
        } else if (step == ActionStep::Active) {
            const float snap = Smooth01(actionTimer / 0.18f);
            PoseBoneTree(*enemyModel, root, -0.04f * snap, 0.0f, 0.0f);
            PoseBoneTree(*enemyModel, chest, -0.16f * phaseScale * snap,
                         0.0f, 0.05f * snap);
            poseArms(-0.48f * phaseScale, -0.28f * phaseScale * snap,
                     0.58f * snap);
        } else {
            PoseBoneTree(*enemyModel, chest, -0.04f, 0.0f, 0.0f);
            poseArms(-0.10f, -0.08f, 0.16f);
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

    ctx_->rendering.model->GetRenderer()->UpdateSkinClusters(*enemyModel);
}
