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

    action_.kind = kind;

    action_.step = step;
    hasTrackingLocked_ = false;
    holdConfigured_ = false;
    currentHoldDuration_ = 0.0f;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;
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

    isVisible_ = true;

    hasTrackingLocked_ = false;
    holdConfigured_ = false;
    currentHoldDuration_ = 0.0f;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;

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
