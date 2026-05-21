#include "Enemy.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

void Enemy::Initialize(uint32_t modelId, uint32_t projectileModelId) {
    modelId_ = modelId;
    projectileModelId_ = projectileModelId;
    runtime_.hp = config_.core.maxHp;
    runtime_.phase = BossPhase::Phase1;
    runtime_.stateTimer = -0.10f;
    runtime_.phaseTransitionActive = false;
    runtime_.phaseTransitionTimer = 0.0f;
    runtime_.bladeClashUsedPhase2 = false;
    runtime_.bladeClashUsedPhase3 = false;
    runtime_.phase2BladeClashStandby = false;
    runtime_.phase3GuardCounterActive = false;
    runtime_.quickCounterOpeningUsed = false;
    runtime_.phase2FeintImmediateGreen = false;
    runtime_.phase2FeintBehindFollowup = false;
    runtime_.phase3PhantomWarpCooldown = 0.0f;

    tf_.position = {0.0f, 0.0f, 10.0f};
    tf_.scale = {1.0f, 1.0f, 1.0f};
    tf_.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    ResetWarpTrails();

    visualTf_ = tf_;
    UpdateParts();
    ValidateAllTimings();
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
    if (phase3PhantomWarpCooldown_ > 0.0f) {
        phase3PhantomWarpCooldown_ =
            (std::max)(0.0f, phase3PhantomWarpCooldown_ - deltaTime);
    }

    if (counterRecoilTimer_ > 0.0f) {
        counterRecoilTimer_ -= deltaTime;
        if (counterRecoilTimer_ < 0.0f) {
            counterRecoilTimer_ = 0.0f;
        }
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

        UpdateWaves(deltaTime);
        UpdateCageTrap(deltaTime);
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
        UpdateWaves(deltaTime);
        UpdateCageTrap(deltaTime);
        UpdateParts();

        if (phaseTransitionTimer_ >= phaseTransitionDuration_) {
            phaseTransitionActive_ = false;
            phaseTransitionTimer_ = 0.0f;
            SetIsPhaseChanging(false);
            stateTimer_ = 0.0f;
            if (phase_ == BossPhase::Phase2 && !bladeClashUsedPhase2_) {
                BeginAction(ActionKind::BladeClash, ActionStep::Charge);
                MarkPhaseBladeClashUsed();
                phase2BladeClashStandby_ = true;
            }
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

        UpdateWaves(deltaTime);
        UpdateCageTrap(deltaTime);
        ClampToArena();
        UpdateParts();
        return;
    }

    if (runtime_.playerObs.justCountered && ApplyCounterBreakReaction()) {
        return;
    }

    UpdateByAction(deltaTime);
    UpdateWaves(deltaTime);
    UpdateCageTrap(deltaTime);
    ClampToArena();
    UpdateParts();
}

void Enemy::SetCinematicTransform(const DirectX::XMFLOAT3 &position,
                                  float yaw) {
    SetCinematicTransform(position, yaw, 0.0f, 0.0f);
}

void Enemy::SetCinematicTransform(const DirectX::XMFLOAT3 &position, float yaw,
                                  float pitch, float roll) {
    tf_.position = position;
    facingYaw_ = yaw;
    lockedAttackYaw_ = yaw;
    cinematicPitch_ = pitch;
    cinematicRoll_ = roll;
    DirectX::XMVECTOR rot =
        DirectX::XMQuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f);
    DirectX::XMStoreFloat4(&tf_.rotation, rot);
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
    case ActionKind::BladeClash:
        UpdateBladeClashByStep(deltaTime);
        break;
    case ActionKind::Wave:
        UpdateWaveByStep(deltaTime);
        break;
    case ActionKind::Cage:
        UpdateCageByStep(deltaTime);
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
    ++runtime_.actionSerial;
    lastActionKind_ = kind;
    const bool keepFeintFollowupLock =
        kind == ActionKind::Warp || phase2FeintFollowupLocked_;
    const bool keepFeintBehindFollowup =
        phase2FeintBehindFollowup_ &&
        (kind == ActionKind::Warp || phase2FeintFollowupLocked_);

    if (kind == ActionKind::Warp) {
        stagnantTimer_ = 0.0f;
        isDistanceStagnant_ = false;
        ResetWarpTrails();
    } else {
        ResetWarpContext();
    }

    action_.kind = kind;
    action_.id = MakeDefaultActionId(kind);

    action_.step = step;
    hasTrackingLocked_ = false;
    holdConfigured_ = false;
    currentHoldDuration_ = 0.0f;
    phase2FeintDecisionMade_ = false;
    phase2DirectionFeintDecisionMade_ = false;
    phase3GuardCounterActive_ = false;
    phase3PhantomFinalLockDelay_ = 0.0f;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;
    currentActionConnected_ = false;
    currentActionGuarded_ = false;
    cageTrapSpawned_ = false;
    phase2FeintFollowupLocked_ = keepFeintFollowupLock;
    phase2FeintBehindFollowup_ =
        keepFeintFollowupLock && keepFeintBehindFollowup;
    if (!phase2FeintFollowupLocked_) {
        phase2FeintImmediateGreen_ = false;
        phase2FeintBehindFollowup_ = false;
    }
    phase2BladeClashStandby_ = false;
    dualCounterStage_ = 0;
    dualCounterFirstHand_ = (std::rand() % 2) == 0;
    dualCounterStageResolved_ = false;
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

void Enemy::ForceBladeClash() {
    if (deathFinished_ || isDying_) {
        return;
    }

    UpdateFacingToPlayerWithSpeed(1.0f, 999.0f);
    BeginAction(ActionKind::BladeClash, ActionStep::Active);
    phase2BladeClashStandby_ = false;
    LockCurrentFacing();
    dualCounterStage_ = 0;
    dualCounterStageResolved_ = false;
    UpdateParts();
}

bool Enemy::CanBeginPhaseBladeClash() const {
    if (phase_ == BossPhase::Phase2) {
        return !bladeClashUsedPhase2_;
    }
    if (phase_ == BossPhase::Phase3) {
        return !bladeClashUsedPhase3_;
    }
    return false;
}

void Enemy::MarkPhaseBladeClashUsed() {
    if (phase_ == BossPhase::Phase2) {
        bladeClashUsedPhase2_ = true;
    } else if (phase_ == BossPhase::Phase3) {
        bladeClashUsedPhase3_ = true;
    }
}

bool Enemy::TryBeginPhase3GuardCounter() {
    if (phase_ != BossPhase::Phase3 || deathFinished_ || isDying_ ||
        hp_ <= 0.0f || phaseTransitionActive_ || counterRecoilTimer_ > 0.0f ||
        action_.kind == ActionKind::BladeClash) {
        return false;
    }

    const ActionKind counterKind =
        (std::rand() % 2 == 0) ? ActionKind::Smash : ActionKind::Sweep;
    hitReactionTimer_ = 0.0f;
    counterRecoilTimer_ = 0.0f;
    UpdateFacingToPlayerWithSpeed(1.0f, 999.0f);
    BeginAction(counterKind, ActionStep::Charge);
    action_.id = MakeDefaultActionId(counterKind);
    phase3GuardCounterActive_ = true;
    LockCurrentFacing();
    UpdateParts();
    return true;
}

bool Enemy::TryBeginTacticAction(ActionKind kind) {
    switch (kind) {
    case ActionKind::Smash:
    case ActionKind::Sweep:
        BeginAction(kind, ActionStep::Charge);
        return true;
    case ActionKind::DelaySmash:
        BeginAction(ActionKind::Smash, ActionStep::Charge);
        action_.id = ActionId::DelaySmash;
        return true;
    case ActionKind::Wave:
    case ActionKind::Cage:
        return false;
    case ActionKind::BladeClash:
        if (!CanBeginPhaseBladeClash()) {
            return false;
        }
        BeginAction(kind, ActionStep::Charge);
        MarkPhaseBladeClashUsed();
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
    isVisible_ = true;

    hasTrackingLocked_ = false;
    holdConfigured_ = false;
    currentHoldDuration_ = 0.0f;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;
    currentActionConnected_ = false;
    currentActionGuarded_ = false;
    phase2FeintFollowupLocked_ = false;
    phase2FeintImmediateGreen_ = false;
    phase2FeintBehindFollowup_ = false;
    phase2FeintDecisionMade_ = false;
    phase2DirectionFeintDecisionMade_ = false;
    phase2BladeClashStandby_ = false;
    phase3GuardCounterActive_ = false;
    dualCounterStage_ = 0;
    dualCounterFirstHand_ = true;
    dualCounterStageResolved_ = false;
    if (postCounterRhythmTimer_ <= 0.0f) {
        counterMemory_.consecutiveSuccess = 0;
    }

    ResetPreAttackPresentationState();
    ResetRecoveryBranchState();
    stalkMoveDir_ = 1.0f;
    stalkForwardBias_ = 0.0f;
}

void Enemy::FinishCurrentAction() {
    EndAttack();
}

void Enemy::UpdateBossPhase() {
    if (phase_ == BossPhase::Phase3 || phaseTransitionActive_ ||
        config_.core.maxHp <= 0.0f) {
        return;
    }

    const float hpRatio = hp_ / config_.core.maxHp;
    BossPhase nextPhase = phase_;
    if (hpRatio <= config_.core.phase3HealthRatioThreshold) {
        nextPhase = BossPhase::Phase3;
    } else if (hpRatio <= config_.core.phase2HealthRatioThreshold) {
        nextPhase = BossPhase::Phase2;
    }

    if (nextPhase != phase_) {
        EndAttack();
        hitReactionTimer_ = 0.0f;
        counterRecoilTimer_ = 0.0f;
        SetIsPhaseChanging(true);
        phase_ = nextPhase;
        phaseTransitionActive_ = true;
        phaseTransitionTimer_ = 0.0f;
        stateTimer_ = 0.0f;
        UpdateFacingToPlayer();
        UpdateParts();
    }
}
