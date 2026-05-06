#include "Enemy.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>

namespace {
int PickWeightedIndex(std::initializer_list<int> weights) {
    int total = 0;
    for (int weight : weights) {
        total += (std::max)(0, weight);
    }

    if (total <= 0) {
        return 0;
    }

    int roll = std::rand() % total;
    int index = 0;
    for (int weight : weights) {
        int clampedWeight = (std::max)(0, weight);
        if (roll < clampedWeight) {
            return index;
        }
        roll -= clampedWeight;
        ++index;
    }

    return 0;
}
} // namespace

float Enemy::RandomRange(float minValue, float maxValue) const {
    if (maxValue < minValue) {
        return minValue;
    }

    float t = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return minValue + (maxValue - minValue) * t;
}

void Enemy::EnterTell(ActionVariant variant) {
    tellActive_ = true;

    if (variant == ActionVariant::Smash) {
        tellDuration_ = smashTellTime_;
    } else if (variant == ActionVariant::Sweep) {
        tellDuration_ = sweepTellTime_;
    } else {
        tellDuration_ = 0.0f;
    }
}

bool Enemy::IsTellFinished() const { return stateTimer_ >= tellDuration_; }

void Enemy::ResetPreAttackPresentationState() {
    tellActive_ = false;

    tellDuration_ = 0.0f;
}

void Enemy::UpdateIdle(float deltaTime) {
    (void)deltaTime;

    if (stateTimer_ < 0.35f) {
        return;
    }

    tactic_ = DecideTactic();
    BeginActionFromTactic(tactic_);
}

ActionKind Enemy::DecideTactic() const {
    const float distance = GetDistanceToPlayer();
    const bool canWarp = !IsWarpSuspendedForPresentation();
    const bool isNear = distance <= config_.core.nearAttackDistance;
    const bool isFar = distance >= config_.core.farAttackDistance;
    const bool shouldWarp =
        canWarp &&
        (isDistanceStagnant_ ||
         farDistanceTimer_ >= farDistanceWarpTimeThreshold_);

    if (shouldWarp) {
        return ActionKind::Warp;
    }
    if (isNear) {
        return ActionKind::Melee;
    }
    if (isFar) {
        return ActionKind::Ranged;
    }
    return ActionKind::Movement;
}

void Enemy::BeginActionFromTactic(ActionKind tactic) {
    switch (tactic) {
    case ActionKind::Warp:
        BeginResetAction();
        break;
    case ActionKind::Melee:
        BeginPressureAction();
        break;
    case ActionKind::Ranged:
        BeginNeutralAction();
        break;
    case ActionKind::Movement:
    default:
        BeginChaseAction();
        break;
    }
}

ActionVariant Enemy::SelectRangedVariant(float distance) const {
    int shotWeight = distance >= config_.core.farAttackDistance ? farShotWeight_
                                                                : midShotWeight_;
    int waveWeight = distance >= config_.core.farAttackDistance ? farWaveWeight_
                                                                : midWaveWeight_;

    if (distance < config_.core.farAttackDistance) {
        shotWeight += neutralMidShotBonus_;
        waveWeight += neutralMidWaveBonus_;
    }

    if (phase_ == BossPhase::Phase2) {
        shotWeight += phase2MidShotBonus_;
    }

    switch (PickWeightedIndex({shotWeight, waveWeight})) {
    case 0:
        return ActionVariant::Shot;
    default:
        return ActionVariant::Wave;
    }
}

ActionVariant Enemy::SelectMeleeVariant() const {
    int smashWeight = nearSmashWeight_;
    int sweepWeight = nearSweepWeight_;

    if (phase_ == BossPhase::Phase2) {
        smashWeight += phase2NearSmashBonus_;
        sweepWeight += phase2NearSweepBonus_;
    }

    if (lastActionVariant_ == ActionVariant::Smash) {
        smashWeight /= 2;
    } else if (lastActionVariant_ == ActionVariant::Sweep) {
        sweepWeight /= 2;
    }

    switch (PickWeightedIndex({smashWeight, sweepWeight})) {
    case 0:
        return ActionVariant::Smash;
    default:
        return ActionVariant::Sweep;
    }
}

void Enemy::BeginNeutralAction() {
    const float distance = GetDistanceToPlayer();

    if (distance <= config_.core.nearAttackDistance) {
        BeginPressureAction();
        return;
    }

    stalkRepeatCount_ = 0;
    TryBeginTacticAction(ActionKind::Ranged);
}

void Enemy::BeginPressureAction() {
    const float distance = GetDistanceToPlayer();

    if (distance <= config_.core.nearAttackDistance) {
        stalkRepeatCount_ = 0;
        TryBeginTacticAction(ActionKind::Melee);
        return;
    }

    if (!IsWarpSuspendedForPresentation()) {
        BeginResetAction();
        return;
    }

    BeginChaseAction();
}

void Enemy::BeginChaseAction() {
    const float distance = GetDistanceToPlayer();

    if (distance <= config_.core.nearAttackDistance) {
        BeginPressureAction();
        return;
    }

    if (distance >= config_.core.farAttackDistance + 1.5f &&
        !IsWarpSuspendedForPresentation()) {
        BeginResetAction();
        return;
    }

    if (distance >= config_.core.farAttackDistance && !isDistanceStagnant_) {
        BeginNeutralAction();
        return;
    }

    stalkRepeatCount_ = 0;
    TryBeginTacticAction(ActionKind::Movement);
}

void Enemy::BeginResetAction() {
    if (IsWarpSuspendedForPresentation()) {
        BeginChaseAction();
        return;
    }

    TryBeginTacticActionOrFallback(ActionKind::Warp, ActionKind::Movement);
}
