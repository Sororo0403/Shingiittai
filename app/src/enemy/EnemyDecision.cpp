#include "Enemy.h"

#include <algorithm>
#include <cmath>
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

bool Enemy::ShouldEnterSmashHold() const {
    float chance = config_.attacks.smash.melee.feintChance;
    if (playerObs_.isAttacking) {
        chance += 0.20f;
    }
    chance = (std::clamp)(chance, 0.0f, 0.88f);
    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return r < chance;
}

bool Enemy::ShouldEnterSweepHold() const {
    float chance = config_.attacks.sweep.melee.feintChance;
    if (playerObs_.isAttacking) {
        chance += 0.20f;
    }
    chance = (std::clamp)(chance, 0.0f, 0.88f);
    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return r < chance;
}

void Enemy::EnterHold(float duration) {
    holdConfigured_ = true;
    currentHoldDuration_ = duration;
    if (playerObs_.isAttacking) {
        currentHoldDuration_ += RandomRange(0.10f, 0.22f);
    }

    ResetPreAttackPresentationState();
}

float Enemy::RandomRange(float minValue, float maxValue) const {
    if (maxValue < minValue) {
        return minValue;
    }

    float t = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return minValue + (maxValue - minValue) * t;
}

void Enemy::EnterTell(ActionKind kind) {
    tellActive_ = true;

    if (kind == ActionKind::Smash) {
        tellDuration_ = smashTellTime_;
    } else if (kind == ActionKind::Sweep) {
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

bool Enemy::ShouldSnapReleaseFromRead() const {
    if (currentHoldDuration_ <= 0.0f) {
        return false;
    }

    if (!playerObs_.isAttacking) {
        return false;
    }

    return stateTimer_ >= currentHoldDuration_ * 0.60f;
}

void Enemy::UpdateIdle(float deltaTime) {
    (void)deltaTime;

    if (stateTimer_ < 0.35f) {
        return;
    }

    tactic_ = DecideTactic();
    BeginActionFromTactic(tactic_);
}

TacticState Enemy::DecideTactic() const {
    const float distance = GetDistanceToPlayer();
    const bool isNear = distance <= config_.core.nearAttackDistance;

    if (isNear) {
        return TacticState::Melee;
    }
    return TacticState::Chase;
}

void Enemy::BeginActionFromTactic(TacticState tactic) {
    switch (tactic) {
    case TacticState::Melee:
        BeginPressureAction();
        break;
    case TacticState::Chase:
    default:
        BeginChaseAction();
        break;
    }
}

ActionKind Enemy::SelectNearPressureAction() const {
    int smashWeight = nearSmashWeight_;
    int sweepWeight = nearSweepWeight_;

    if (phase_ != BossPhase::Phase1) {
        smashWeight += phase2NearSmashBonus_;
        sweepWeight += phase2NearSweepBonus_;
    }
    if (phase_ == BossPhase::Phase3) {
        smashWeight += phase3NearSmashBonus_;
        sweepWeight += phase3NearSweepBonus_;
    }

    if (playerObs_.isAttacking) {
        sweepWeight += 10;
    }
    if (lastActionKind_ == ActionKind::Smash) {
        smashWeight /= 2;
    } else if (lastActionKind_ == ActionKind::Sweep) {
        sweepWeight /= 2;
    }

    switch (PickWeightedIndex({smashWeight, sweepWeight})) {
    case 0:
        return ActionKind::Smash;
    default:
        return ActionKind::Sweep;
    }
}

bool Enemy::TryBeginWarpAction(float chance) {
    if (lastActionKind_ == ActionKind::Warp) {
        chance *= 0.35f;
    }
    chance = std::clamp(chance, 0.0f, 1.0f);
    const float roll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (roll >= chance || !PrepareWarpContext()) {
        return false;
    }

    BeginAction(ActionKind::Warp, ActionStep::Start);
    return true;
}

void Enemy::BeginPressureAction() {
    const float distance = GetDistanceToPlayer();

    if (distance <= config_.core.nearAttackDistance) {
        const float chance = playerObs_.isAttacking ? warpNearChance_ * 1.35f
                                                    : warpNearChance_;
        if (TryBeginWarpAction(chance)) {
            return;
        }
        BeginAction(SelectNearPressureAction(), ActionStep::Charge);
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

    const float chance =
        distance >= warpCutInDistance_ ? warpCutInChance_ : warpFarChance_;
    if (TryBeginWarpAction(chance)) {
        return;
    }

    BeginStalkAction();
}

