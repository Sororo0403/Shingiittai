#include "Enemy.h"

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

    tf_.position = {0.0f, 0.0f, 10.0f};
    tf_.scale = {1.0f, 1.0f, 1.0f};
    tf_.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    ResetWarpTrails();

    visualTf_ = tf_;
    UpdateParts();
    ValidateAllTimings();
}

void Enemy::Update(const DirectX::XMFLOAT3 &playerPos, float deltaTime) {
    if (deathFinished_) {
        return;
    }

    runtime_.playerPos = playerPos;
    UpdateBossPhase();
    UpdateWarpTrails(deltaTime);

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
    case ActionKind::Melee:
        UpdateMeleeByStep(deltaTime);
        break;
    case ActionKind::Ranged:
        UpdateRangedByStep(deltaTime);
        break;
    case ActionKind::Warp:
        UpdateWarpByStep(deltaTime);
        break;
    case ActionKind::Movement:
        UpdateStalkByStep(deltaTime);
        break;
    default:
        UpdateIdle(deltaTime);
        break;
    }
}

void Enemy::BeginAction(ActionKind kind, ActionStep step, ActionVariant variant) {
    if (kind == ActionKind::Melee && variant == ActionVariant::None) {
        variant = SelectMeleeVariant();
    } else if (kind == ActionKind::Ranged && variant == ActionVariant::None) {
        variant = SelectRangedVariant(GetDistanceToPlayer());
    }

    lastActionKind_ = kind;
    lastActionVariant_ = variant;

    if (kind == ActionKind::Warp) {
        stagnantTimer_ = 0.0f;
        isDistanceStagnant_ = false;
        ResetWarpTrails();
    } else {
        ResetWarpContext();
    }

    action_.kind = kind;
    action_.variant = variant;

    action_.step = step;
    hasTrackingLocked_ = false;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;
    ResetPreAttackPresentationState();

    if (kind == ActionKind::Movement) {
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
    case ActionKind::Melee:
        BeginAction(kind, ActionStep::Charge, SelectMeleeVariant());
        return true;
    case ActionKind::Ranged:
        BeginAction(kind, ActionStep::Charge,
                    SelectRangedVariant(GetDistanceToPlayer()));
        return true;
    case ActionKind::Warp:
        if (!PrepareWarpContext()) {
            return false;
        }
        BeginAction(kind, ActionStep::Start);
        return true;
    case ActionKind::Movement:
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

    if (step != ActionStep::Charge) {
        ResetPreAttackPresentationState();
    }
}

void Enemy::EndAttack() {
    action_.kind = ActionKind::None;
    action_.variant = ActionVariant::None;
    action_.step = ActionStep::None;

    ResetWarpContext();
    ResetChainContext();
    isVisible_ = true;

    hasTrackingLocked_ = false;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;
    ResetPreAttackPresentationState();
    stalkMoveDir_ = 1.0f;
    stalkForwardBias_ = 0.0f;
}

void Enemy::FinishCurrentAction() {
    if (TryContinueChain()) {
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
