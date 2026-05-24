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
        chance += 0.16f;
    }
    chance = (std::clamp)(chance, 0.0f, 0.75f);
    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return r < chance;
}

bool Enemy::ShouldEnterSweepHold() const {
    float chance = config_.attacks.sweep.melee.feintChance;
    if (playerObs_.isAttacking) {
        chance += 0.16f;
    }
    chance = (std::clamp)(chance, 0.0f, 0.75f);
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
    const bool isFar = distance >= config_.core.farAttackDistance;

    if (isNear) {
        return TacticState::Melee;
    }
    if (isFar) {
        return TacticState::Ranged;
    }
    return TacticState::DistanceAdjust;
}

void Enemy::BeginActionFromTactic(TacticState tactic) {
    switch (tactic) {
    case TacticState::Warp:
        BeginResetAction();
        break;
    case TacticState::Melee:
        BeginPressureAction();
        break;
    case TacticState::Ranged:
        BeginNeutralAction();
        break;
    case TacticState::DistanceAdjust:
    default:
        BeginChaseAction();
        break;
    }
}

ActionKind Enemy::SelectNeutralAction(float distance) const {
    if (distance <= config_.core.nearAttackDistance) {
        return SelectNearPressureAction();
    }

    return ActionKind::Stalk;
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
    if (playerObs_.isGuarding) {
        sweepWeight += 4;
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

ActionKind Enemy::SelectChaseAction() const { return ActionKind::Stalk; }

void Enemy::BeginNeutralAction() {
    const float distance = GetDistanceToPlayer();

    if (distance <= config_.core.nearAttackDistance) {
        BeginPressureAction();
        return;
    }

    stalkRepeatCount_ = 0;
    TryBeginTacticAction(SelectNeutralAction(distance));
}

void Enemy::BeginPressureAction() {
    const float distance = GetDistanceToPlayer();

    if (distance <= config_.core.nearAttackDistance) {
        stalkRepeatCount_ = 0;
        TryBeginTacticAction(SelectNearPressureAction());
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

    if (distance >= config_.core.farAttackDistance && !isDistanceStagnant_) {
        BeginNeutralAction();
        return;
    }

    stalkRepeatCount_ = 0;
    TryBeginTacticAction(SelectChaseAction());
}

void Enemy::BeginResetAction() {
    if (IsWarpSuspendedForPresentation()) {
        BeginChaseAction();
        return;
    }

    if (TryBeginWarpBehindMeleeSkill(false)) {
        return;
    }

    TryBeginTacticActionOrFallback(ActionKind::Warp, ActionKind::Stalk);
}

bool Enemy::TryBeginWarpBehindMeleeSkill(bool force) {
    const float distance = GetDistanceToPlayer();
    if (!force && (distance < 2.2f || distance > 8.5f)) {
        return false;
    }

    float chance = 0.08f;
    if (playerObs_.isGuarding) {
        chance += 0.08f;
    }
    if (playerObs_.isAttacking) {
        chance += 0.04f;
    }
    if (lastActionKind_ == ActionKind::Warp) {
        chance *= 0.5f;
    }

    if (!force) {
        const float roll = static_cast<float>(std::rand()) /
                           static_cast<float>(RAND_MAX);
        if (roll >= chance) {
            return false;
        }
    }

    float forwardX = std::sin(playerObs_.facingYaw);
    float forwardZ = std::cos(playerObs_.facingYaw);
    float forwardLength = std::sqrt(forwardX * forwardX + forwardZ * forwardZ);

    if (forwardLength <= 0.0001f) {
        forwardX = playerObs_.velocity.x;
        forwardZ = playerObs_.velocity.z;
        forwardLength = std::sqrt(forwardX * forwardX + forwardZ * forwardZ);
    }

    if (forwardLength <= 0.0001f) {
        forwardX = std::sin(facingYaw_);
        forwardZ = std::cos(facingYaw_);
        forwardLength = 1.0f;
    }

    forwardX /= forwardLength;
    forwardZ /= forwardLength;

    constexpr float backDistance = 2.25f;
    DirectX::XMFLOAT3 target = playerPos_;
    target.x -= forwardX * backDistance;
    target.z -= forwardZ * backDistance;
    target.y = tf_.position.y;

    ResetWarpContext();
    warp_.type = WarpType::Approach;
    warp_.approachSlot = WarpApproachSlot::Back;
    FinalizeWarpTargetFacing(target);
    warp_.targetPos = target;
    warp_.hasValidTarget = true;
    warp_.followupKind = SelectNearPressureAction();
    if (warp_.followupKind != ActionKind::Smash &&
        warp_.followupKind != ActionKind::Sweep) {
        warp_.followupKind = ActionKind::Smash;
    }
    warp_.followupStep = ActionStep::Charge;

    BeginAction(ActionKind::Warp, ActionStep::Start);
    action_.id = ActionId::WarpBackstab;
    return true;
}

