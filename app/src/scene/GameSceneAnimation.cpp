#include "GameScene.h"
#include "BladeClashCinematic.h"
#include "ModelManager.h"
#include <algorithm>
#include <initializer_list>
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
static const std::string kBossAnimBulletShot =
    "\xE5\xBC\xBE\xE7\x99\xBA\xE5\xB0\x84";
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
    case ActionKind::ArcaneLaser:
        return 0.72f;
    case ActionKind::CataclysmLaser:
        return 1.10f;
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
        return PickFirstAnimation(model, {kBossAnimSmash, kBossAnimIdle,
                                          kBossAnimMove});

    case ActionKind::Sweep:
        outLoop = false;
        return PickFirstAnimation(model, {kBossAnimSweep, kBossAnimIdle,
                                          kBossAnimMove});

    case ActionKind::BladeClash:
        outLoop = false;
        return PickFirstAnimation(model,
                                  {kBossAnimTeleport, kBossAnimSmash,
                                   kBossAnimSweep, kBossAnimIdle,
                                   kBossAnimMove});

    case ActionKind::Warp:
        outLoop = false;
        return PickFirstAnimation(model, {kBossAnimTeleport, kBossAnimIdle,
                                          kBossAnimMove});

    case ActionKind::Stalk:
        return PickFirstAnimation(model, {kBossAnimMove, kBossAnimIdle});

    case ActionKind::ArcaneLaser:
    case ActionKind::CataclysmLaser:
        outLoop = false;
        return PickFirstAnimation(model, {kBossAnimBulletShot,
                                          kBossAnimTeleport, kBossAnimIdle,
                                          kBossAnimMove});

    case ActionKind::None:
    default:
        if (enemy.GetIsPhaseChanging()) {
            outLoop = false;
            const std::string phaseAnimation =
                PickFirstAnimation(model, {kBossAnimPhaseChange});
            if (!phaseAnimation.empty()) {
                return phaseAnimation;
            }
        }

        if (HasAnimation(model, kBossAnimIdle)) {
            return kBossAnimIdle;
        }
        break;
    }

    return PickFirstAnimation(model, {kBossAnimIdle, kBossAnimMove});
}

struct EnemyPoseContext {
    Model &model;
    int root;
    int chest;
    int upperA;
    int upperB;
    int upperC;
    int upperD;
    float time;
    float pulse;
    float slowPulse;
    float phaseScale;
    float actionTimer;
    float chargePose;
    bool farSlashFlashHold;
    bool farSlashPostPierceSlash;

    void PoseArms(float pitch, float yaw, float roll) const {
        PoseBoneTree(model, upperA, pitch, yaw, roll);
        PoseBoneTree(model, upperB, pitch * 0.76f, yaw * 0.62f,
                     roll * 0.82f);
        PoseBoneTree(model, upperC, pitch * 0.62f, -yaw * 0.54f,
                     -roll * 0.70f);
        PoseBoneTree(model, upperD, pitch * 0.48f, -yaw * 0.42f,
                     -roll * 0.56f);
    }
};

static float GetEnemyPosePhaseScale(BossPhase phase) {
    if (phase == BossPhase::Phase3) {
        return 1.32f;
    }
    return phase == BossPhase::Phase1 ? 1.0f : 1.18f;
}

static float GetEnemyChargePose(ActionStep step, float actionTimer,
                                float settleTime, bool flashHold) {
    if (flashHold || step == ActionStep::Hold) {
        return 1.0f;
    }
    return step == ActionStep::Charge
               ? Smooth01(actionTimer / settleTime)
               : 0.0f;
}

static float PickPoseValue(bool condition, float trueValue, float falseValue) {
    return condition ? trueValue : falseValue;
}

static void ApplySmashPose(const EnemyPoseContext &p, ActionStep step) {
    if (step == ActionStep::Charge || step == ActionStep::Hold ||
        p.farSlashFlashHold) {
        const float hold = 0.98f + 0.02f * p.pulse;
        PoseBoneTree(p.model, p.root, -0.08f * p.phaseScale * p.chargePose,
                     0.0f, 0.0f);
        PoseBoneTree(p.model, p.chest,
                     -0.20f * p.phaseScale * p.chargePose, 0.0f,
                     0.012f * hold * p.chargePose);
        p.PoseArms(-0.58f * p.phaseScale * p.chargePose,
                   0.08f * p.chargePose, 0.18f * hold * p.chargePose);
    } else if (step == ActionStep::Active || p.farSlashPostPierceSlash) {
        const float strike = Smooth01(p.time / 0.18f);
        PoseBoneTree(p.model, p.root, 0.10f * p.phaseScale, 0.0f, 0.0f);
        PoseBoneTree(p.model, p.chest, 0.34f * p.phaseScale * strike, 0.0f,
                     -0.06f);
        p.PoseArms(0.72f * p.phaseScale * strike, 0.04f,
                   -0.16f * p.phaseScale);
    } else if (step == ActionStep::Recovery) {
        PoseBoneTree(p.model, p.chest, 0.12f, 0.0f, -0.04f);
        p.PoseArms(0.20f, 0.0f, -0.08f);
    }
}

static void ApplySweepPose(const EnemyPoseContext &p, ActionStep step) {
    if (step == ActionStep::Charge || step == ActionStep::Hold ||
        p.farSlashFlashHold) {
        PoseBoneTree(p.model, p.chest, -0.06f * p.chargePose,
                     -0.30f * p.phaseScale * p.chargePose,
                     -0.025f * p.chargePose);
        p.PoseArms(-0.05f * p.chargePose,
                   -0.34f * p.phaseScale * p.chargePose,
                   0.30f * p.chargePose);
    } else if (step == ActionStep::Active || p.farSlashPostPierceSlash) {
        const float strike = Smooth01(p.time / 0.24f);
        PoseBoneTree(p.model, p.root, 0.0f, 0.18f * p.phaseScale * strike,
                     0.0f);
        PoseBoneTree(p.model, p.chest, 0.04f, 0.56f * p.phaseScale * strike,
                     0.14f);
        p.PoseArms(0.08f, 0.74f * p.phaseScale * strike,
                   -0.28f * p.phaseScale);
    } else if (step == ActionStep::Recovery) {
        PoseBoneTree(p.model, p.chest, 0.04f, 0.18f, 0.05f);
        p.PoseArms(0.02f, 0.24f, -0.10f);
    }
}

static void ApplyBladeClashPose(const EnemyPoseContext &p, ActionStep step) {
    if (step == ActionStep::Charge) {
        const float brace = Smooth01(p.actionTimer / 0.62f);
        PoseBoneTree(p.model, p.root, -0.06f * p.phaseScale * brace, 0.0f,
                     0.0f);
        PoseBoneTree(p.model, p.chest, -0.14f * p.phaseScale * brace, 0.0f,
                     0.04f * p.pulse * brace);
        p.PoseArms(-0.24f * p.phaseScale * brace, 0.0f,
                   0.18f * p.phaseScale * brace);
    } else if (step == ActionStep::Active) {
        const float clash = 0.82f + 0.18f * p.pulse;
        PoseBoneTree(p.model, p.root, 0.05f * p.phaseScale, 0.0f,
                     0.02f * p.slowPulse);
        PoseBoneTree(p.model, p.chest, 0.18f * p.phaseScale * clash, 0.0f,
                     0.05f * p.pulse);
        p.PoseArms(0.36f * p.phaseScale * clash, 0.0f,
                   -0.22f * p.phaseScale);
    } else if (step == ActionStep::Recovery) {
        PoseBoneTree(p.model, p.chest, 0.08f, 0.0f, -0.03f);
        p.PoseArms(0.16f, 0.0f, -0.06f);
    }
}

static void ApplyLaserPose(const EnemyPoseContext &p, ActionStep step,
                           bool cataclysm) {
    const float aimDuration = PickPoseValue(cataclysm, 1.10f, 0.72f);
    if (step == ActionStep::Charge) {
        const float aim = Smooth01(p.actionTimer / aimDuration);
        PoseBoneTree(p.model, p.root,
                     PickPoseValue(cataclysm, -0.10f, -0.04f) * p.phaseScale * aim,
                     0.0f, PickPoseValue(cataclysm, 0.018f * p.slowPulse * aim, 0.0f));
        PoseBoneTree(p.model, p.chest,
                     PickPoseValue(cataclysm, -0.24f, -0.10f) * p.phaseScale * aim,
                     0.0f, PickPoseValue(cataclysm, 0.060f, 0.035f) * p.pulse * aim);
        p.PoseArms(PickPoseValue(cataclysm, -0.42f, -0.18f) * p.phaseScale * aim,
                   0.0f, PickPoseValue(cataclysm, -0.36f, -0.22f) * p.phaseScale * aim);
    } else if (step == ActionStep::Active) {
        const float recoil = PickPoseValue(cataclysm, 0.78f, 0.84f) +
                             PickPoseValue(cataclysm, 0.22f, 0.16f) * p.pulse;
        PoseBoneTree(p.model, p.root,
                     PickPoseValue(cataclysm, -0.12f, -0.06f) * p.phaseScale, 0.0f,
                     PickPoseValue(cataclysm, 0.012f * p.slowPulse, 0.0f));
        PoseBoneTree(p.model, p.chest,
                     PickPoseValue(cataclysm, -0.30f, -0.16f) * p.phaseScale * recoil,
                     0.0f, PickPoseValue(cataclysm, 0.085f, 0.05f) * p.pulse);
        p.PoseArms(PickPoseValue(cataclysm, -0.58f, -0.34f) * p.phaseScale * recoil,
                   0.0f, PickPoseValue(cataclysm, -0.44f, -0.30f) * p.phaseScale);
    } else if (step == ActionStep::Recovery) {
        PoseBoneTree(p.model, p.chest,
                     PickPoseValue(cataclysm, 0.10f, 0.06f), 0.0f,
                     PickPoseValue(cataclysm, 0.03f, 0.02f));
        p.PoseArms(PickPoseValue(cataclysm, 0.16f, 0.10f), 0.0f,
                   PickPoseValue(cataclysm, -0.10f, -0.08f));
    }
}

static void ApplyWarpPose(const EnemyPoseContext &p, ActionStep step) {
    if (step == ActionStep::Start) {
        const float chant = Smooth01(p.actionTimer / 0.42f);
        const float gather = 0.86f + 0.14f * p.pulse;
        PoseBoneTree(p.model, p.root, -0.07f * p.phaseScale * chant, 0.0f,
                     0.018f * p.slowPulse * chant);
        PoseBoneTree(p.model, p.chest, -0.18f * p.phaseScale * chant,
                     0.020f * p.slowPulse * chant,
                     0.060f * p.pulse * chant);
        p.PoseArms(-0.42f * p.phaseScale * chant,
                   0.08f * p.slowPulse * chant,
                   -0.34f * p.phaseScale * gather * chant);
    } else if (step == ActionStep::Move) {
        const float vanish = 0.74f + 0.26f * p.pulse;
        PoseBoneTree(p.model, p.chest, -0.10f * p.phaseScale * vanish, 0.0f,
                     0.08f * vanish);
        p.PoseArms(-0.22f * p.phaseScale * vanish, 0.0f,
                   -0.18f * p.phaseScale);
    } else if (step == ActionStep::End) {
        const float settle = Smooth01(p.actionTimer / 0.24f);
        PoseBoneTree(p.model, p.root,
                     -0.04f * p.phaseScale * (1.0f - settle), 0.0f, 0.0f);
        PoseBoneTree(p.model, p.chest,
                     -0.08f * p.phaseScale * (1.0f - settle), 0.0f,
                     0.04f * p.pulse * (1.0f - settle));
        p.PoseArms(-0.18f * p.phaseScale * (1.0f - settle), 0.0f,
                   -0.16f * p.phaseScale * (1.0f - settle));
    }
}

static void ApplyEnemyActionPose(const EnemyPoseContext &p, ActionKind action,
                                 ActionStep step) {
    switch (action) {
    case ActionKind::Smash:
        ApplySmashPose(p, step);
        break;
    case ActionKind::Sweep:
        ApplySweepPose(p, step);
        break;
    case ActionKind::BladeClash:
        ApplyBladeClashPose(p, step);
        break;
    case ActionKind::ArcaneLaser:
        ApplyLaserPose(p, step, false);
        break;
    case ActionKind::CataclysmLaser:
        ApplyLaserPose(p, step, true);
        break;
    case ActionKind::Warp:
        ApplyWarpPose(p, step);
        break;
    case ActionKind::Stalk:
        PoseBoneTree(p.model, p.root, 0.04f * p.slowPulse, 0.0f,
                     0.06f * p.pulse);
        PoseBoneTree(p.model, p.chest, -0.04f, 0.08f * p.pulse,
                     -0.05f * p.slowPulse);
        p.PoseArms(0.06f * p.pulse, 0.12f * p.slowPulse, 0.10f * p.pulse);
        break;
    case ActionKind::None:
    default:
        p.PoseArms(0.035f * p.pulse, 0.025f * p.slowPulse,
                   0.045f * p.pulse);
        break;
    }
}

bool AllTrue(std::initializer_list<bool> values) {
    return std::all_of(values.begin(), values.end(),
                       [](bool value) { return value; });
}

bool AnyTrue(std::initializer_list<bool> values) {
    return std::any_of(values.begin(), values.end(),
                       [](bool value) { return value; });
}

std::string DefaultBladeClashClip(bool hasTeleport, bool hasSweep) {
    if (hasTeleport) {
        return kBossAnimTeleport;
    }
    return hasSweep ? kBossAnimSweep : kBossAnimSmash;
}

float DefaultBladeClashClipRatio(bool hasTeleport, bool hasSweep) {
    if (hasTeleport) {
        return kBladeClashActivePoseClipRatio;
    }
    return hasSweep ? 0.62f : 0.42f;
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
    if (!AllTrue({enemyModel != nullptr,
                  enemyModel != nullptr && !enemyModel->animations.empty()})) {
        return;
    }

    const bool hasTeleport = HasAnimation(enemyModel, kBossAnimTeleport);
    const bool hasSweep = HasAnimation(enemyModel, kBossAnimSweep);
    const bool hasSmash = HasAnimation(enemyModel, kBossAnimSmash);
    if (!AnyTrue({hasTeleport, hasSweep, hasSmash})) {
        return;
    }

    std::string clip = DefaultBladeClashClip(hasTeleport, hasSweep);
    float clipRatio = DefaultBladeClashClipRatio(hasTeleport, hasSweep);

    const bool bladeClashStartupAnim =
        AllTrue({!bladeClashActive_, !bladeClashFinishActive_,
                 enemy_.GetActionKind() == ActionKind::BladeClash,
                 enemy_.GetActionStep() == ActionStep::Charge});
    if (bladeClashStartupAnim) {
        SelectBladeClashStartupClip(hasTeleport, hasSweep, clip, clipRatio);
    } else if (AllTrue({bladeClashFinishActive_, bladeClashFinishPlayerWon_,
                        bladeClashFinishTimer_ <
                            Clash::kWinGuardBreakLead})) {
        SelectBladeClashGuardBreakClip(hasTeleport, hasSweep, clip, clipRatio);
    } else if (AllTrue(
                   {bladeClashFinishActive_, bladeClashFinishPlayerWon_})) {
        SelectBladeClashWinClip(hasTeleport, hasSweep, hasSmash, clip,
                                clipRatio);
    } else if (AllTrue(
                   {bladeClashFinishActive_, !bladeClashFinishPlayerWon_})) {
        SelectBladeClashLossClip(hasTeleport, hasSweep, hasSmash, clip,
                                 clipRatio);
    }

    if (!ScrubAnimationClip(modelManager, enemyModelId_, enemyModel, clip,
                            clipRatio, false, enemyAnimationName_,
                            enemyAnimationLoop_)) {
        modelManager->UpdateAnimation(enemyModelId_, 0.0f);
    }
    enemyAnimationFrozen_ = false;
}

void GameScene::SelectBladeClashStartupClip(bool hasTeleport, bool hasSweep,
                                            std::string &clip,
                                            float &clipRatio) const {
    const float t = Smooth01(enemy_.GetActionTimerForPresentation() /
                             kBladeClashStartupPoseTime);
    if (hasTeleport) {
        clip = kBossAnimTeleport;
        clipRatio = kBladeClashStartupClipStartRatio +
                    (kBladeClashActivePoseClipRatio -
                     kBladeClashStartupClipStartRatio) * t;
    } else if (hasSweep) {
        clip = kBossAnimSweep;
        clipRatio = 0.18f + 0.30f * t;
    } else {
        clip = kBossAnimSmash;
        clipRatio = 0.16f + 0.28f * t;
    }
}

void GameScene::SelectBladeClashGuardBreakClip(
    bool hasTeleport, bool hasSweep, std::string &clip,
    float &clipRatio) const {
    const float t = Smooth01((bladeClashFinishTimer_ - 0.04f) /
                             (Clash::kWinGuardBreakLead - 0.04f));
    if (hasTeleport) {
        clip = kBossAnimTeleport;
        clipRatio = kBladeClashGuardPoseClipRatio +
                    (kBladeClashGuardOpenClipRatio -
                     kBladeClashGuardPoseClipRatio) * t;
    } else if (hasSweep) {
        clip = kBossAnimSweep;
        clipRatio = 0.58f - 0.20f * t;
    } else {
        clip = kBossAnimSmash;
        clipRatio = 0.34f - 0.12f * t;
    }
}

void GameScene::SelectBladeClashWinClip(bool hasTeleport, bool hasSweep,
                                        bool hasSmash, std::string &clip,
                                        float &clipRatio) const {
    const float timer =
        (std::max)(0.0f, bladeClashFinishTimer_ - Clash::kWinGuardBreakLead) /
        Clash::kWinActionSlow;
    if (hasSmash) {
        clip = kBossAnimSmash;
        clipRatio = 0.32f + 0.26f * Smooth01((timer - 0.04f) / 0.24f) +
                    0.16f * Smooth01((timer - 0.30f) / 0.54f);
    } else if (hasSweep) {
        clip = kBossAnimSweep;
        clipRatio = 0.42f + 0.18f * Smooth01((timer - 0.06f) / 0.20f) +
                    0.14f * Smooth01((timer - 0.18f) / 0.42f) +
                    0.08f * Smooth01((timer - 0.52f) / 0.44f);
    } else if (hasTeleport) {
        clip = kBossAnimTeleport;
        clipRatio = kBladeClashActivePoseClipRatio +
                    0.18f * Smooth01((timer - 0.06f) / 0.34f);
    }
    clipRatio = Clamp01(clipRatio);
}

void GameScene::SelectBladeClashLossClip(bool hasTeleport, bool hasSweep,
                                         bool hasSmash, std::string &clip,
                                         float &clipRatio) const {
    const float windup =
        Smooth01(bladeClashFinishTimer_ / Clash::kLossHitTime);
    const float strike =
        Smooth01((bladeClashFinishTimer_ - Clash::kLossHitTime) / 0.22f);
    const float recover = Smooth01(
        (bladeClashFinishTimer_ - Clash::kLossSlideStartTime) / 0.62f);
    if (hasSweep) {
        clip = kBossAnimSweep;
        clipRatio = 0.22f + 0.34f * windup + 0.28f * strike +
                    0.10f * recover;
    } else if (hasSmash) {
        clip = kBossAnimSmash;
        clipRatio = 0.24f + 0.30f * windup + 0.26f * strike +
                    0.08f * recover;
    } else if (hasTeleport) {
        clip = kBossAnimTeleport;
        clipRatio = kBladeClashActivePoseClipRatio + 0.20f * windup +
                    0.16f * strike;
    }
    clipRatio = Clamp01(clipRatio);
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

    const float difficulty = Clamp01(readyPreviewHeat_) * 9.0f;
    std::string clip{};
    float clipRatio = 0.0f;

    if (difficulty <= 2.0f && HasAnimation(enemyModel, kBossAnimTeleport)) {
        clip = kBossAnimTeleport;
        const float reverse = Smooth01(difficulty / 2.0f);
        clipRatio =
            kBladeClashGuardPoseClipRatio +
            (kBladeClashGuardOpenClipRatio - kBladeClashGuardPoseClipRatio) *
                reverse;
    } else if (HasAnimation(enemyModel, kBossAnimPhaseChange)) {
        clip = kBossAnimPhaseChange;
        constexpr float kPreviewChargeClipStart = 0.00f;
        constexpr float kPreviewReleaseGateClipRatio = 0.15f;
        constexpr float kPreviewReleaseMomentClipRatio = 0.36f;
        if (difficulty < 7.0f) {
            const float charge = Smooth01((difficulty - 2.0f) / 5.0f);
            clipRatio =
                kPreviewChargeClipStart +
                (kPreviewReleaseGateClipRatio - kPreviewChargeClipStart) *
                    charge;
        } else if (difficulty < 9.0f) {
            const float release = Smooth01((difficulty - 7.0f) / 2.0f);
            clipRatio =
                kPreviewReleaseGateClipRatio +
                (kPreviewReleaseMomentClipRatio -
                 kPreviewReleaseGateClipRatio) *
                    release;
        } else {
            clipRatio = kPreviewReleaseMomentClipRatio;
        }
    } else if (HasAnimation(enemyModel, kBossAnimTeleport)) {
        clip = kBossAnimTeleport;
        clipRatio = kBladeClashGuardOpenClipRatio;
    } else {
        clip = PickFirstAnimation(enemyModel, {kBossAnimIdle, kBossAnimMove});
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
    const float phaseScale = GetEnemyPosePhaseScale(enemy_.GetBossPhase());
    const float actionTimer = enemy_.GetActionTimerForPresentation();
    const float stanceSettleTime = GetChargeStanceSettleTime(action);
    const bool chargeSettled =
        IsChargeStanceSettled(action, step, actionTimer);
    const bool farSlashFlashHold =
        enemy_.IsFarWarpSlashActive() && step == ActionStep::Active &&
        actionTimer <= enemy_.GetFarWarpSlashStanceHoldDuration();
    const bool farSlashPostPierceSlash =
        enemy_.IsFarWarpSlashActive() && step == ActionStep::Recovery;
    const float chargePose = GetEnemyChargePose(
        step, actionTimer, stanceSettleTime, farSlashFlashHold);
    const float idleMotion = chargeSettled ? 0.10f : 1.0f;



    PoseBoneTree(*enemyModel, spine, -0.025f * slowPulse * idleMotion, 0.0f,
                 0.025f * pulse * idleMotion);
    PoseBoneTree(*enemyModel, chest, 0.018f * pulse * idleMotion,
                 0.018f * slowPulse * idleMotion,
                 -0.020f * pulse * idleMotion);
    PoseBoneTree(*enemyModel, head, 0.025f * slowPulse * idleMotion,
                 0.020f * pulse * idleMotion, 0.0f);
    PoseBoneTree(*enemyModel, headTip, 0.020f * slowPulse * idleMotion, 0.0f,
                 0.0f);

    const EnemyPoseContext pose{
        *enemyModel, root, chest, upperA, upperB, upperC, upperD,
        time, pulse, slowPulse, phaseScale, actionTimer, chargePose,
        farSlashFlashHold, farSlashPostPierceSlash};

    ApplyEnemyActionPose(pose, action, step);

    ctx_->rendering.model->GetRenderer()->UpdateSkinClusters(*enemyModel);
}
