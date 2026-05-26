#include "Enemy.h"

#include <algorithm>
#include <cmath>

void Enemy::Initialize(uint32_t modelId) {
    modelId_ = modelId;
    runtime_.hp = config_.core.maxHp;
    runtime_.phase = BossPhase::Phase1;
    runtime_.stateTimer = -0.10f;
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

void Enemy::Update(const PlayerCombatObservation &playerObs, float deltaTime) {
    if (deathFinished_) {
        return;
    }

    runtime_.playerObs = playerObs;
    runtime_.playerPos = playerObs.position;
    UpdateBossPhase();
    UpdateWarpTrails(deltaTime);
    if (phantomWarpCooldown_ > 0.0f) {
        phantomWarpCooldown_ -= deltaTime;
        if (phantomWarpCooldown_ < 0.0f) {
            phantomWarpCooldown_ = 0.0f;
        }
    }
    if (phantomFinalLockTimer_ > 0.0f) {
        phantomFinalLockTimer_ -= deltaTime;
        if (phantomFinalLockTimer_ < 0.0f) {
            phantomFinalLockTimer_ = 0.0f;
        }
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
        UpdateParts();

        if (phaseTransitionTimer_ >= phaseTransitionDuration_) {
            phaseTransitionActive_ = false;
            phaseTransitionTimer_ = 0.0f;
            SetIsPhaseChanging(false);
            stateTimer_ = 0.0f;
        }
        return;
    }

    if (action_.kind == ActionKind::None) {
        UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_);
    }

    stateTimer_ += deltaTime;
    isAttackActive_ = false;

    if (hitReactionTimer_ > 0.0f) {
        stateTimer_ -= deltaTime;
        if (stateTimer_ < 0.0f) {
            stateTimer_ = 0.0f;
        }

        hitReactionTimer_ -= deltaTime;
        if (hitReactionTimer_ < 0.0f) {
            hitReactionTimer_ = 0.0f;
        }

        UpdateParts();
        return;
    }

    UpdateByAction(deltaTime);
    UpdateParts();
}

void Enemy::UpdateTutorial(const PlayerCombatObservation &playerObs,
                           float deltaTime) {
    if (deathFinished_) {
        return;
    }

    runtime_.playerObs = playerObs;
    runtime_.playerPos = playerObs.position;
    UpdateWarpTrails(deltaTime);

    if (counterRecoilTimer_ > 0.0f) {
        counterRecoilTimer_ -= deltaTime;
        if (counterRecoilTimer_ < 0.0f) {
            counterRecoilTimer_ = 0.0f;
        }
    }

    if (action_.kind == ActionKind::None) {
        UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_);
        UpdateParts();
        return;
    }

    stateTimer_ += deltaTime;
    isAttackActive_ = false;

    if (hitReactionTimer_ > 0.0f) {
        stateTimer_ -= deltaTime;
        if (stateTimer_ < 0.0f) {
            stateTimer_ = 0.0f;
        }

        hitReactionTimer_ -= deltaTime;
        if (hitReactionTimer_ < 0.0f) {
            hitReactionTimer_ = 0.0f;
        }

        UpdateParts();
        return;
    }

    switch (action_.kind) {
    case ActionKind::Smash:
        UpdateSmashByStep(deltaTime);
        break;
    case ActionKind::Sweep:
        UpdateSweepByStep(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
    UpdateParts();
}

void Enemy::BeginTutorialAttack(ActionKind kind) {
    if (kind != ActionKind::Smash && kind != ActionKind::Sweep) {
        kind = ActionKind::Smash;
    }
    BeginAction(kind, ActionStep::Charge);
}

void Enemy::BeginDebugBladeClash(const DirectX::XMFLOAT3 &targetPosition) {
    if (deathFinished_ || isDying_ || phaseTransitionActive_) {
        return;
    }

    hitReactionTimer_ = 0.0f;
    counterRecoilTimer_ = 0.0f;
    playerPos_ = targetPosition;
    FaceTargetImmediately(targetPosition);
    BeginAction(ActionKind::BladeClash, ActionStep::Active);
    LockCurrentFacing();
    stateTimer_ = config_.attacks.bladeClash.profile.timing.activeStartTime;
    isAttackActive_ = true;
    UpdateParts();
}

void Enemy::ResetTutorialState() {
    EndAttack();
    runtime_.hp = config_.core.maxHp;
    runtime_.phase = BossPhase::Phase1;
    runtime_.phaseTransitionActive = false;
    runtime_.phaseTransitionTimer = 0.0f;
    runtime_.hitReactionTimer = 0.0f;
    runtime_.counterRecoilTimer = 0.0f;
    runtime_.isDying = false;
    runtime_.deathFinished = false;
    runtime_.deathTimer = 0.0f;
    ResetWarpTrails();
    UpdateParts();
}

void Enemy::SetTutorialPosition(const DirectX::XMFLOAT3 &position) {
    tf_.position = position;
    visualTf_ = tf_;
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
        ResetWarpTrails();
    } else {
        ResetWarpContext();
    }

    action_.kind = kind;

    action_.step = step;
    hasTrackingLocked_ = false;
    holdConfigured_ = false;
    currentHoldDuration_ = 0.0f;
    quickSlashActive_ = false;
    farSlashActive_ = false;
    warpFeintFollowupLocked_ = false;
    warpFeintImmediate_ = false;
    warpFeintDecisionMade_ = false;
    directionFeintDecisionMade_ = false;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;
    cinematicPitch_ = 0.0f;
    cinematicRoll_ = 0.0f;
    ResetPreAttackPresentationState();
}

void Enemy::ChangeActionStep(ActionStep step) {
    action_.step = step;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;

    if (step != ActionStep::Charge && step != ActionStep::Hold) {
        ResetPreAttackPresentationState();
    }
}

void Enemy::EndAttack() {
    action_.kind = ActionKind::None;
    action_.step = ActionStep::None;

    ResetWarpContext();
    isVisible_ = true;

    hasTrackingLocked_ = false;
    holdConfigured_ = false;
    currentHoldDuration_ = 0.0f;
    quickSlashActive_ = false;
    farSlashActive_ = false;
    warpFeintFollowupLocked_ = false;
    warpFeintImmediate_ = false;
    warpFeintDecisionMade_ = false;
    directionFeintDecisionMade_ = false;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;
    cinematicPitch_ = 0.0f;
    cinematicRoll_ = 0.0f;

    ResetPreAttackPresentationState();
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
