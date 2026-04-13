#include "Enemy.h"
#include "ModelManager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

// ============================================================
// 初期化処理
// ============================================================
void Enemy::Initialize(uint32_t modelId) {
    modelId_ = modelId;

    tf_.position = {0.0f, 0.0f, 10.0f};
    tf_.scale = {1.0f, 1.0f, 1.0f};
    tf_.rotation = {0.0f, 0.0f, 0.0f, 1.0f};

    UpdateParts();
    ValidateAllTimings();
}

// ============================================================
// 毎フレーム更新処理（旧互換）
// ============================================================
void Enemy::Update(const DirectX::XMFLOAT3 &playerPos, float deltaTime,
                   bool playerGuarding) {
    PlayerCombatObservation obs{};
    obs.position = playerPos;
    obs.isGuarding = playerGuarding;
    Update(obs, deltaTime);
}

// ============================================================
// 毎フレーム更新処理（新版）
// ============================================================
void Enemy::Update(const PlayerCombatObservation &playerObs, float deltaTime) {
    if (!IsAlive()) {
        return;
    }

    playerObs_ = playerObs;
    playerPos_ = playerObs.position;
    playerGuarding_ = playerObs.isGuarding;

    float currentDistance = GetDistanceToPlayer();
    float distanceDelta = std::fabs(currentDistance - lastDistanceToPlayer_);

    if (distanceDelta < stagnantDistanceThreshold_) {
        stagnantTimer_ += deltaTime;
    } else {
        stagnantTimer_ = 0.0f;
    }
    isDistanceStagnant_ = (stagnantTimer_ >= stagnantTimeThreshold_);

    if (currentDistance <= closePressureDistance_) {
        closePressureTimer_ += deltaTime;
        if (closePressureTimer_ > closePressureTimeThreshold_) {
            closePressureTimer_ = closePressureTimeThreshold_;
        }
    } else {
        closePressureTimer_ -= deltaTime;
        if (closePressureTimer_ < 0.0f) {
            closePressureTimer_ = 0.0f;
        }
    }

    if (currentDistance > farAttackDistance_) {
        farDistanceTimer_ += deltaTime;
    } else {
        farDistanceTimer_ = 0.0f;
    }

    if (warpEscapeCooldownTimer_ > 0.0f) {
        warpEscapeCooldownTimer_ -= deltaTime;
        if (warpEscapeCooldownTimer_ < 0.0f) {
            warpEscapeCooldownTimer_ = 0.0f;
        }
    }

    lastDistanceToPlayer_ = currentDistance;

    if (action_.kind == ActionKind::None) {
        UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_);
    }

    stateTimer_ += deltaTime;

    isAttackActive_ = false;
    isGuardActive_ = false;

    UpdateCounterAdaptation(deltaTime);

    // ------------------------------------------------------------
    // カウンター成功リアクション
    // ------------------------------------------------------------
    if (playerObs_.justCountered) {
        RegisterCounterSuccessReaction();

        const bool isCounterBreakableAction =
            (action_.kind == ActionKind::Smash) ||
            (action_.kind == ActionKind::Sweep) ||
            (action_.kind == ActionKind::Rush);

        if (isCounterBreakableAction) {
            float dx = tf_.position.x - playerPos_.x;
            float dz = tf_.position.z - playerPos_.z;
            float len = std::sqrtf(dx * dx + dz * dz);
            if (len < 0.0001f) {
                len = 1.0f;
            }

            dx /= len;
            dz /= len;

            const float counterPushBack = 0.9f;
            tf_.position.x += dx * counterPushBack;
            tf_.position.z += dz * counterPushBack;

            EndAttack();
            ResetChainContext();
            ResetPostActionState();

            stateTimer_ = -0.20f;

            tactic_ = TacticState::Reset;
            closePressureTimer_ = 0.0f;
            stagnantTimer_ = 0.0f;
            isDistanceStagnant_ = false;

            UpdateFacingToPlayer();
            UpdateParts();
            return;
        }
    }

    UpdateByAction(deltaTime);

    UpdateBullets(deltaTime);
    UpdateWaves(deltaTime);

    UpdateParts();
}

// ============================================================
// 描画処理
// ============================================================
void Enemy::Draw(ModelManager *modelManager, const Camera &camera) {
    if (!IsAlive()) {
        return;
    }

    if (isVisible_) {
        modelManager->Draw(modelId_, bodyTf_, camera);
        modelManager->Draw(modelId_, leftHandTf_, camera);
        modelManager->Draw(modelId_, rightHandTf_, camera);
    }

    for (const auto &bullet : bullets_) {
        if (!bullet.isAlive) {
            continue;
        }

        Transform bulletTf = tf_;
        bulletTf.position = bullet.position;
        bulletTf.scale = {0.2f, 0.2f, 0.2f};

        modelManager->Draw(modelId_, bulletTf, camera);
    }

    for (const auto &wave : waves_) {
        if (!wave.isAlive) {
            continue;
        }

        Transform waveTf = tf_;
        waveTf.position = wave.position;
        waveTf.scale = {0.6f, 0.2f, 1.2f};

        modelManager->Draw(modelId_, waveTf, camera);
    }
}

// ============================================================
// 被ダメージ処理
// ============================================================
void Enemy::TakeDamage(float damage) {
    hp_ -= damage;

    if (hp_ < 0.0f) {
        hp_ = 0.0f;
    }
}

// ============================================================
// action ベース更新
// ============================================================
void Enemy::UpdateByAction(float deltaTime) {
    if (action_.kind == ActionKind::None) {
        UpdateIdle(deltaTime);
        return;
    }

    switch (action_.kind) {
    case ActionKind::Smash:
        UpdateSmashByStep(deltaTime);
        break;
    case ActionKind::Sweep:
        UpdateSweepByStep(deltaTime);
        break;
    case ActionKind::Shot:
        UpdateShotByStep(deltaTime);
        break;
    case ActionKind::Wave:
        UpdateWaveByStep(deltaTime);
        break;
    case ActionKind::Rush:
        UpdateRushByStep(deltaTime);
        break;
    case ActionKind::Warp:
        UpdateWarpByStep(deltaTime);
        break;
    case ActionKind::Guard:
        UpdateGuardByStep(deltaTime);
        break;
    case ActionKind::Stalk: // 追加
        UpdateStalkByStep(deltaTime);
        break;
    default:
        UpdateIdle(deltaTime);
        break;
    }
}

// ============================================================
// 行動開始・行動遷移
// ============================================================
void Enemy::BeginAction(ActionKind kind, ActionStep step) {
    lastActionKind_ = kind;

    if (kind == ActionKind::Warp) {
        stagnantTimer_ = 0.0f;
        isDistanceStagnant_ = false;

        if (warp_.type == WarpType::Escape) {
            warpEscapeCooldownTimer_ = warpEscapeCooldown_;
        }
    } else {
        ResetWarpContext();
    }

    action_.kind = kind;
    action_.id = MakeDefaultActionId(kind);

    if (kind == ActionKind::Smash) {
        float r =
            static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);

        float useDelayChance = delaySmashChance_;
        if (tactic_ == TacticState::CounterBait || playerObs_.isCounterStance) {
            useDelayChance += 0.20f;
        }

        if (r < useDelayChance) {
            action_.id = ActionId::DelaySmash;
        }
    }

    if (kind == ActionKind::Sweep) {
        float r =
            static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);

        float useDoubleChance = doubleSweepChance_;
        if (tactic_ == TacticState::CounterPunish || IsCounterFailObserved()) {
            useDoubleChance += 0.20f;
        }

        if (r < useDoubleChance) {
            action_.id = ActionId::DoubleSweep;
        }
    }

    action_.step = step;

    hasTrackingLocked_ = false;
    holdConfigured_ = false;
    currentHoldDuration_ = 0.0f;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;
    isDoubleSweepSecondStage_ = false;
    ResetPreAttackPresentationState();
    ResetRecoveryBranchState();

    // 追加：Stalk用初期化
    if (kind == ActionKind::Stalk) {
        stalkMoveDir_ = (std::rand() % 2 == 0) ? -1.0f : 1.0f;

        int biasRand = std::rand() % 3;
        if (biasRand == 0) {
            stalkForwardBias_ = -1.0f;
        } else if (biasRand == 1) {
            stalkForwardBias_ = 0.0f;
        } else {
            stalkForwardBias_ = 1.0f;
        }
    }
}

void Enemy::ChangeActionStep(ActionStep step) {
    action_.step = step;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;

    if (step != ActionStep::Hold) {
        holdBranchType_ = HoldBranchType::None;
        holdBranchDecided_ = false;
        holdBranchDecisionTime_ = 0.0f;
    }

    if (step != ActionStep::Charge && step != ActionStep::Hold) {
        ResetPreAttackPresentationState();
    }
}

void Enemy::EndAttack() {
    action_.kind = ActionKind::None;
    action_.id = ActionId::None;
    action_.step = ActionStep::None;

    ResetWarpContext();
    ResetChainContext();
    ResetPostActionState();
    isVisible_ = true;

    hasTrackingLocked_ = false;
    holdConfigured_ = false;
    currentHoldDuration_ = 0.0f;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;
    isDoubleSweepSecondStage_ = false;
    rushCurrentYaw_ = facingYaw_;

    if (postCounterRhythmTimer_ <= 0.0f) {
        counterMemory_.consecutiveSuccess = 0;
    }

    ResetPreAttackPresentationState();
    ResetRecoveryBranchState();

    // 追加
    stalkMoveDir_ = 1.0f;
    stalkForwardBias_ = 0.0f;
}

void Enemy::FinishCurrentAction() {
    ActionKind finishedKind = action_.kind;

    if (TryContinueChain()) {
        return;
    }

    if (TryBranchFromRecovery(finishedKind)) {
        if (recoveryBranchType_ == RecoveryBranchType::Recommit ||
            recoveryBranchType_ == RecoveryBranchType::DelayedSecond) {
            ActionKind nextKind = recoveryFollowupKind_;
            ActionStep nextStep = recoveryFollowupStep_;

            EndAttack();

            if (nextKind == ActionKind::Smash ||
                nextKind == ActionKind::Sweep || nextKind == ActionKind::Rush) {
                tactic_ = TacticState::Pressure;
            }

            recoveryFollowupKind_ = nextKind;
            recoveryFollowupStep_ = nextStep;
            return;
        }

        return;
    }

    if (TryStartBackWarpPostAction(finishedKind)) {
        return;
    }

    EndAttack();
}