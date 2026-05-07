#include "GameScene.h"
#include "ModelManager.h"
#include <string>

static const std::string kBossAnimIdle = "Action";
static const std::string kBossAnimMove = "Action.001";
static const std::string kBossAnimSweep = "横薙ぎ払い";
static const std::string kBossAnimWave = "波状攻撃";
static const std::string kBossAnimSmash = "縦振り下ろし";
static const std::string kBossAnimShoot = "弾発射";
static const std::string kBossAnimTeleport = "テレポート";
static const std::string kBossAnimPhaseChange = "第二形態移行";

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

    case ActionKind::Shot:
        outLoop = false;
        if (HasAnimation(model, kBossAnimShoot)) {
            return kBossAnimShoot;
        }
        break;

    case ActionKind::Wave:
        outLoop = false;
        if (HasAnimation(model, kBossAnimWave)) {
            return kBossAnimWave;
        }
        break;

    case ActionKind::Warp:
        outLoop = false;
        if (HasAnimation(model, kBossAnimTeleport)) {
            return kBossAnimTeleport;
        }
        break;

    case ActionKind::PhaseTransition:
        outLoop = false;
        if (HasAnimation(model, kBossAnimPhaseChange)) {
            return kBossAnimPhaseChange;
        }
        break;

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

float GameScene::ComputeGameplayTimeScale() const {
    return 1.0f;
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
    std::string nextAnimation = PickEnemyAnimation(enemyModel, enemy_, shouldLoop);
    if (nextAnimation.empty()) {
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
