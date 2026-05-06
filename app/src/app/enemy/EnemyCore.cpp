#include "Enemy.h"

#include <cstdlib>
#include <cmath>

namespace {
float Saturate(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}
} // namespace

void Enemy::Initialize(uint32_t modelId, uint32_t projectileModelId) {
    modelId_ = modelId;
    projectileModelId_ = projectileModelId;
    runtime_.hp = config_.core.maxHp;
    runtime_.phase = BossPhase::Phase1;
    runtime_.introActive = true;
    runtime_.introTimer = 0.0f;
    runtime_.phaseTransitionActive = false;
    runtime_.phaseTransitionTimer = 0.0f;

    tf_.position = {0.0f, 0.0f, 10.0f};
    tf_.scale = {1.0f, 1.0f, 1.0f};
    tf_.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    ResetWarpTrails();

    visualTf_ = tf_;
    UpdateParts();
    ValidateAllTimings();
}

void Enemy::SkipIntro() {
    runtime_.introActive = false;
    runtime_.introTimer = 0.0f;
    runtime_.stateTimer = -0.10f;
}

void Enemy::RestartIntro() {
    runtime_.introActive = true;
    runtime_.introTimer = 0.0f;
    runtime_.stateTimer = 0.0f;
    runtime_.action.kind = ActionKind::None;
    runtime_.action.id = ActionId::None;
    runtime_.action.step = ActionStep::None;
}

void Enemy::DebugResetState() {
    isDying_ = false;
    deathFinished_ = false;
    deathTimer_ = 0.0f;
    deathStartY_ = tf_.position.y;
    hp_ = config_.core.maxHp;

    introActive_ = false;
    introTimer_ = 0.0f;
    phaseTransitionActive_ = false;
    phaseTransitionTimer_ = 0.0f;

    bullets_.clear();
    waves_.clear();
    shotsRemaining_ = 0;
    shotIntervalTimer_ = 0.0f;

    tactic_ = TacticState::DistanceAdjust;
    EndAttack();
    stateTimer_ = -0.10f;
    UpdateParts();
}

bool Enemy::DebugStartAction(ActionKind kind) {
    DebugResetState();
    if (kind == ActionKind::None) {
        return true;
    }
    const bool started = TryBeginTacticAction(kind);
    UpdateParts();
    return started;
}

bool Enemy::DebugTriggerWarpBackstab(const PlayerCombatObservation &playerObs) {
    if (deathFinished_ || isDying_ || introActive_ || phaseTransitionActive_) {
        return false;
    }

    runtime_.playerObs = playerObs;
    runtime_.playerPos = playerObs.position;
    runtime_.playerGuarding = playerObs.isGuarding;

    EndAttack();
    return TryBeginWarpBehindMeleeSkill(true);
}

void Enemy::DebugSetBossPhase(BossPhase phase) {
    phase_ = phase;
    phaseTransitionActive_ = false;
    phaseTransitionTimer_ = 0.0f;
    if (phase == BossPhase::Phase1) {
        hp_ = config_.core.maxHp;
    } else {
        hp_ = config_.core.maxHp * config_.core.phase2HealthRatioThreshold * 0.5f;
    }
    UpdateParts();
}

void Enemy::Update(const DirectX::XMFLOAT3 &playerPos, float deltaTime,
                   bool playerGuarding) {
    PlayerCombatObservation obs{};
    obs.position = playerPos;
    obs.isGuarding = playerGuarding;
    Update(obs, deltaTime);
}

void Enemy::Update(const PlayerCombatObservation &playerObs, float deltaTime) {
    if (deathFinished_) {
        return;
    }

    runtime_.playerObs = playerObs;
    runtime_.playerPos = playerObs.position;
    runtime_.playerGuarding = playerObs.isGuarding;
    UpdateBossPhase();
    UpdateWarpTrails(deltaTime);

    if (counterRecoilTimer_ > 0.0f) {
        counterRecoilTimer_ -= deltaTime;
        if (counterRecoilTimer_ < 0.0f) {
            counterRecoilTimer_ = 0.0f;
        }
    }

    if (introActive_) {
        const float introTotalDuration =
            introSecondSlashDuration_ + introSpinSlashDuration_ +
            introSettleDuration_;
        introTimer_ += deltaTime;
        introTimer_ = Saturate(introTimer_ / introTotalDuration) * introTotalDuration;
        isAttackActive_ = false;

        UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_ * 0.55f);
        UpdateParts();

        if (introTimer_ >= introTotalDuration) {
            introActive_ = false;
            introTimer_ = 0.0f;
            stateTimer_ = -0.10f;
            UpdateParts();
        }
        return;
    }

    if (isDying_) {
        deathTimer_ += deltaTime;
        float t = deathTimer_ / deathDuration_;
        if (t > 1.0f) {
            t = 1.0f;
        }

        tf_.position.y = deathStartY_ - deathSinkDistance_ * t;
        tf_.scale.x = 1.0f - 0.25f * t;
        tf_.scale.y = 1.0f - 0.55f * t;
        tf_.scale.z = 1.0f - 0.25f * t;

        UpdateBullets(deltaTime);
        UpdateWaves(deltaTime);
        UpdateParts();

        if (deathTimer_ >= deathDuration_) {
            deathFinished_ = true;
        }
        return;
    }

    if (phaseTransitionActive_) {
        phaseTransitionTimer_ += deltaTime;
        isAttackActive_ = false;

        UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_ * 0.35f);
        UpdateBullets(deltaTime);
        UpdateWaves(deltaTime);
        UpdateParts();

        if (phaseTransitionTimer_ >= phaseTransitionDuration_) {
            phaseTransitionActive_ = false;
            phaseTransitionTimer_ = 0.0f;
            stateTimer_ = 0.0f;
        }
        return;
    }

    const float currentDistance = GetDistanceToPlayer();
    const float distanceDelta =
        std::fabs(currentDistance - runtime_.lastDistanceToPlayer);

    if (distanceDelta < stagnantDistanceThreshold_) {
        runtime_.stagnantTimer += deltaTime;
    } else {
        runtime_.stagnantTimer = 0.0f;
    }
    runtime_.isDistanceStagnant =
        runtime_.stagnantTimer >= stagnantTimeThreshold_;

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

    if (currentDistance > config_.core.farAttackDistance) {
        farDistanceTimer_ += deltaTime;
    } else {
        farDistanceTimer_ = 0.0f;
    }

    runtime_.lastDistanceToPlayer = currentDistance;

    if (action_.kind == ActionKind::None) {
        UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_);
    }

    stateTimer_ += deltaTime;
    isAttackActive_ = false;

    UpdateCounterAdaptation(deltaTime);

    if (hitReactionTimer_ > 0.0f) {
        stateTimer_ -= deltaTime;
        if (stateTimer_ < 0.0f) {
            stateTimer_ = 0.0f;
        }

        hitReactionTimer_ -= deltaTime;
        if (hitReactionTimer_ < 0.0f) {
            hitReactionTimer_ = 0.0f;
        }

        float dx = tf_.position.x - runtime_.playerPos.x;
        float dz = tf_.position.z - runtime_.playerPos.z;
        float length = std::sqrt(dx * dx + dz * dz);
        if (length > 0.0001f) {
            dx /= length;
            dz /= length;
            tf_.position.x += dx * hitReactionMoveSpeed_ * deltaTime;
            tf_.position.z += dz * hitReactionMoveSpeed_ * deltaTime;
        }

        UpdateBullets(deltaTime);
        UpdateWaves(deltaTime);
        UpdateParts();
        return;
    }

    if (runtime_.playerObs.justCountered && ApplyCounterBreakReaction()) {
        return;
    }

    UpdateByAction(deltaTime);
    UpdateBullets(deltaTime);
    UpdateWaves(deltaTime);
    UpdateParts();
}

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
    case ActionKind::Warp:
        UpdateWarpByStep(deltaTime);
        break;
    case ActionKind::Stalk:
        UpdateStalkByStep(deltaTime);
        break;
    default:
        UpdateIdle(deltaTime);
        break;
    }
}

void Enemy::BeginAction(ActionKind kind, ActionStep step) {
    lastActionKind_ = kind;

    if (kind == ActionKind::Warp) {
        stagnantTimer_ = 0.0f;
        isDistanceStagnant_ = false;
        ResetWarpTrails();
    } else {
        ResetWarpContext();
    }

    action_.kind = kind;
    action_.id = MakeDefaultActionId(kind);

    if (kind == ActionKind::Smash) {
        float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        float useDelayChance = config_.attacks.smash.delayChance;
        if (playerObs_.isCounterStance) {
            useDelayChance += 0.20f;
        }
        if (phase_ == BossPhase::Phase2) {
            useDelayChance += phase2DelaySmashBonus_;
        }

        if (r < useDelayChance) {
            action_.id = ActionId::DelaySmash;
        }
    }

    if (kind == ActionKind::Sweep) {
        float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        float useDoubleChance = config_.attacks.sweep.doubleChance;
        if (IsCounterFailObserved()) {
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
    currentActionConnected_ = false;
    currentActionGuarded_ = false;
    ResetPreAttackPresentationState();
    ResetRecoveryBranchState();

    if (kind == ActionKind::Stalk) {
        stalkMoveDir_ = (std::rand() % 2 == 0) ? -1.0f : 1.0f;

        const int biasRand = std::rand() % 3;
        if (biasRand == 0) {
            stalkForwardBias_ = -1.0f;
        } else if (biasRand == 1) {
            stalkForwardBias_ = 0.0f;
        } else {
            stalkForwardBias_ = 1.0f;
        }
    }
}

bool Enemy::TryBeginTacticAction(ActionKind kind) {
    switch (kind) {
    case ActionKind::Smash:
    case ActionKind::Sweep:
    case ActionKind::Shot:
    case ActionKind::Wave:
        BeginAction(kind, ActionStep::Charge);
        return true;
    case ActionKind::Warp:
        if (!PrepareWarpContext()) {
            return false;
        }
        BeginAction(kind, ActionStep::Start);
        return true;
    case ActionKind::Stalk:
        BeginStalkAction();
        return true;
    default:
        return false;
    }
}

bool Enemy::TryBeginTacticActionOrFallback(ActionKind preferred,
                                           ActionKind fallback) {
    if (TryBeginTacticAction(preferred)) {
        return true;
    }
    return TryBeginTacticAction(fallback);
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
    currentActionConnected_ = false;
    currentActionGuarded_ = false;

    if (postCounterRhythmTimer_ <= 0.0f) {
        counterMemory_.consecutiveSuccess = 0;
    }

    ResetPreAttackPresentationState();
    ResetRecoveryBranchState();
    stalkMoveDir_ = 1.0f;
    stalkForwardBias_ = 0.0f;
}

void Enemy::FinishCurrentAction() {
    const ActionKind finishedKind = action_.kind;

    if (TryContinueChain()) {
        return;
    }

    if (TryBranchFromRecovery(finishedKind)) {
        if (recoveryBranchType_ == RecoveryBranchType::Recommit ||
            recoveryBranchType_ == RecoveryBranchType::DelayedSecond) {
            const ActionKind nextKind = recoveryFollowupKind_;
            const ActionStep nextStep = recoveryFollowupStep_;

            EndAttack();
            if (nextKind == ActionKind::Smash || nextKind == ActionKind::Sweep) {
                tactic_ = TacticState::Melee;
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

void Enemy::UpdateBossPhase() {
    if (phase_ == BossPhase::Phase2 || config_.core.maxHp <= 0.0f) {
        return;
    }

    const float hpRatio = hp_ / config_.core.maxHp;
    if (hpRatio <= config_.core.phase2HealthRatioThreshold) {
        EndAttack();
        phase_ = BossPhase::Phase2;
        phaseTransitionActive_ = true;
        phaseTransitionTimer_ = 0.0f;
        stateTimer_ = 0.0f;
    }
}
