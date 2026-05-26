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

float Enemy::TechniqueUnlock(BossPhase requiredPhase) const {
    if (difficulty_ >= 7.0f) {
        return 1.0f;
    }
    if (difficulty_ < 2.0f) {
        return 0.0f;
    }

    const int currentPhase = static_cast<int>(phase_);
    const int required = static_cast<int>(requiredPhase);
    return currentPhase >= required ? 1.0f : 0.0f;
}

bool Enemy::ShouldEnterSmashHold() const {
    const float unlock = TechniqueUnlock(BossPhase::Phase2);
    if (unlock <= 0.0f) {
        return false;
    }
    float chance = config_.attacks.smash.melee.feintChance;
    if (playerObs_.isAttacking) {
        chance += 0.20f;
    }
    chance *= unlock;
    chance = (std::clamp)(chance, 0.0f, 0.88f);
    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return r < chance;
}

bool Enemy::ShouldEnterSweepHold() const {
    const float unlock = TechniqueUnlock(BossPhase::Phase2);
    if (unlock <= 0.0f) {
        return false;
    }
    float chance = config_.attacks.sweep.melee.feintChance;
    if (playerObs_.isAttacking) {
        chance += 0.20f;
    }
    chance *= unlock;
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

bool Enemy::TryBeginChargeWarpFeint(ActionKind kind) {
    if (warpFeintFollowupLocked_ || warpFeintDecisionMade_ ||
        !(kind == ActionKind::Smash || kind == ActionKind::Sweep) ||
        action_.kind != kind ||
        !(action_.step == ActionStep::Charge ||
          action_.step == ActionStep::Hold)) {
        return false;
    }

    if (GetReleaseAnticipationRatio() < 0.12f) {
        return false;
    }

    warpFeintDecisionMade_ = true;
    const float unlock = TechniqueUnlock(BossPhase::Phase3);
    if (unlock <= 0.0f) {
        return false;
    }
    float chance = chargeWarpFeintChance_;
    if (phase_ == BossPhase::Phase1) {
        chance *= 0.55f;
    } else if (phase_ == BossPhase::Phase3) {
        chance += 0.12f;
    }
    if (playerObs_.isAttacking) {
        chance += 0.12f;
    }
    chance *= unlock;
    chance = std::clamp(chance, 0.0f, 0.76f);

    const float roll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (roll >= chance) {
        return false;
    }

    ResetWarpContext();
    warp_.approachSlot = (std::rand() % 100 < 58) ? WarpApproachSlot::Back
                                                  : WarpApproachSlot::Front;
    if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
        ResetWarpContext();
        return false;
    }

    warp_.hasValidTarget = true;
    warp_.followupKind = kind;
    warp_.followupStep = ActionStep::Charge;
    warp_.feintFollowup = true;
    warp_.immediateFollowup = (std::rand() % 2) == 0;
    warp_.faceLivePlayerOnEnd = true;
    BeginAction(ActionKind::Warp, ActionStep::Start);
    return true;
}

bool Enemy::TryApplyDirectionFeint(ActionKind kind) {
    if (warpFeintFollowupLocked_ || directionFeintDecisionMade_ ||
        !(kind == ActionKind::Smash || kind == ActionKind::Sweep) ||
        action_.kind != kind || action_.step != ActionStep::Charge) {
        return false;
    }

    const float chargeTime = kind == ActionKind::Smash
                                 ? GetCurrentSmashChargeTime()
                                 : GetCurrentSweepChargeTime();
    const float switchTime = (std::max)(0.20f, chargeTime * 0.48f);
    if (stateTimer_ < switchTime || GetReleaseAnticipationRatio() > 0.0f) {
        return false;
    }

    directionFeintDecisionMade_ = true;
    const float unlock = TechniqueUnlock(BossPhase::Phase3);
    if (unlock <= 0.0f) {
        return false;
    }
    float chance = directionFeintChance_;
    if (phase_ == BossPhase::Phase1) {
        chance *= 0.50f;
    } else if (phase_ == BossPhase::Phase3) {
        chance += 0.10f;
    }
    if (playerObs_.isAttacking) {
        chance += 0.10f;
    }
    chance *= unlock;
    chance = std::clamp(chance, 0.0f, 0.64f);

    const float roll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (roll >= chance) {
        return false;
    }

    const ActionKind nextKind =
        kind == ActionKind::Smash ? ActionKind::Sweep : ActionKind::Smash;
    action_.kind = nextKind;
    hasTrackingLocked_ = false;
    holdConfigured_ = false;
    currentHoldDuration_ = 0.0f;
    warpFeintDecisionMade_ = true;
    ResetPreAttackPresentationState();

    const float nextChargeTime =
        nextKind == ActionKind::Smash ? GetCurrentSmashChargeTime()
                                      : GetCurrentSweepChargeTime();
    stateTimer_ = (std::max)(0.12f, nextChargeTime * 0.52f);
    return true;
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

bool Enemy::IsPlayerInMeleeFront() const {
    const float toPlayerX = playerPos_.x - tf_.position.x;
    const float toPlayerZ = playerPos_.z - tf_.position.z;
    const float distanceSq = toPlayerX * toPlayerX + toPlayerZ * toPlayerZ;
    const float frontDistance = config_.core.nearAttackDistance;
    if (distanceSq > frontDistance * frontDistance) {
        return false;
    }

    const float forwardX = std::sin(facingYaw_);
    const float forwardZ = std::cos(facingYaw_);
    const float forwardDistance = toPlayerX * forwardX + toPlayerZ * forwardZ;
    if (forwardDistance < 0.55f || forwardDistance > frontDistance) {
        return false;
    }

    const float lateralDistance =
        std::fabs(toPlayerX * forwardZ - toPlayerZ * forwardX);
    const float allowedHalfWidth =
        1.15f + std::clamp(forwardDistance / frontDistance, 0.0f, 1.0f) *
                    0.70f;
    return lateralDistance <= allowedHalfWidth;
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
    if (IsPlayerInMeleeFront()) {
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
    chance *= TechniqueUnlock(BossPhase::Phase2);
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

bool Enemy::TryBeginQuickSlash(float chance) {
    const float unlock = TechniqueUnlock(BossPhase::Phase2);
    if (unlock <= 0.0f) {
        return false;
    }
    if (!IsPlayerInMeleeFront()) {
        return false;
    }

    if (lastActionKind_ == ActionKind::Smash ||
        lastActionKind_ == ActionKind::Sweep) {
        chance *= 0.62f;
    }
    if (playerObs_.isAttacking) {
        chance += 0.10f;
    }
    if (phase_ != BossPhase::Phase1) {
        chance += 0.08f;
    }
    chance *= unlock;
    chance = std::clamp(chance, 0.0f, 0.78f);

    const float roll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (roll >= chance) {
        return false;
    }

    BeginAction(SelectNearPressureAction(), ActionStep::Charge);
    quickSlashActive_ = true;
    warpFeintDecisionMade_ = true;
    directionFeintDecisionMade_ = true;
    return true;
}

bool Enemy::TryBeginFarWarpSlash(float chance) {
    const float unlock = TechniqueUnlock(BossPhase::Phase3);
    if (unlock <= 0.0f) {
        return false;
    }
    if (lastActionKind_ == ActionKind::Warp) {
        chance *= 0.58f;
    }
    if (playerObs_.isAttacking) {
        chance += 0.08f;
    }
    if (phase_ == BossPhase::Phase3) {
        chance += 0.08f;
    }
    chance *= unlock;
    chance = std::clamp(chance, 0.0f, 0.86f);

    const float roll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (roll >= chance) {
        return false;
    }

    ResetWarpContext();
    warp_.isCutIn = true;
    warp_.farSlashFollowup = true;
    warp_.approachSlot = WarpApproachSlot::Front;
    if (!DecideWarpTargetFarSlash(warp_.targetPos)) {
        ResetWarpContext();
        return false;
    }

    warp_.hasValidTarget = true;
    warp_.followupKind = SelectNearPressureAction();
    warp_.followupStep = ActionStep::Charge;
    warp_.faceLivePlayerOnEnd = true;
    BeginAction(ActionKind::Warp, ActionStep::Start);
    return true;
}

bool Enemy::TryBeginPhantomWarpSkill(float chance) {
    const float unlock = TechniqueUnlock(BossPhase::Phase3);
    if (unlock <= 0.0f) {
        return false;
    }
    if (phantomWarpCooldown_ > 0.0f || deathFinished_ || isDying_ ||
        phaseTransitionActive_) {
        return false;
    }

    const float distance = GetDistanceToPlayer();
    if (distance < 1.65f || distance > 9.8f) {
        return false;
    }

    if (lastActionKind_ == ActionKind::Warp) {
        chance *= 0.42f;
    }
    if (playerObs_.isAttacking) {
        chance += 0.06f;
    }
    if (phase_ == BossPhase::Phase3) {
        chance += 0.12f;
    } else if (phase_ == BossPhase::Phase2) {
        chance += 0.06f;
    }
    chance *= unlock;
    chance = std::clamp(chance, 0.0f, 0.72f);

    const float roll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (roll >= chance) {
        return false;
    }

    BeginPhantomWarpStep(2, false, ActionKind::None);
    phantomWarpCooldown_ = phantomWarpCooldownDuration_;
    return true;
}

bool Enemy::TryBeginBladeClash(float chance) {
    const float unlock = TechniqueUnlock(BossPhase::Phase2);
    if (unlock <= 0.0f) {
        return false;
    }
    if (!IsPlayerInMeleeFront() || lastActionKind_ == ActionKind::BladeClash) {
        return false;
    }

    if (playerObs_.isAttacking) {
        chance += 0.10f;
    }
    if (phase_ == BossPhase::Phase1) {
        chance *= 0.55f;
    } else if (phase_ == BossPhase::Phase3) {
        chance += 0.10f;
    }
    chance *= unlock;
    chance = std::clamp(chance, 0.0f, 0.62f);

    const float roll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (roll >= chance) {
        return false;
    }

    BeginAction(ActionKind::BladeClash, ActionStep::Charge);
    return true;
}

void Enemy::BeginPhantomWarpStep(int viewWarpsRemaining, bool finalBehind,
                                 ActionKind followupKind) {
    ResetWarpContext();
    warp_.phantomChain = true;
    warp_.phantomFinal = finalBehind;
    warp_.phantomViewWarpsRemaining = viewWarpsRemaining;
    warp_.faceLivePlayerOnEnd = !finalBehind;

    if (finalBehind) {
        warp_.approachSlot = WarpApproachSlot::Back;
        if (!DecideWarpTargetBehindPlayer(warp_.targetPos)) {
            ResetWarpContext();
            BeginChaseAction();
            return;
        }
        warp_.followupKind = followupKind;
        warp_.followupStep = ActionStep::Charge;
    } else {
        warp_.approachSlot = WarpApproachSlot::Front;
        if (!DecideWarpTargetInPlayerView(warp_.targetPos)) {
            ResetWarpContext();
            BeginChaseAction();
            return;
        }
        warp_.followupKind = ActionKind::None;
        warp_.followupStep = ActionStep::None;
    }

    warp_.hasValidTarget = true;
    BeginAction(ActionKind::Warp, ActionStep::Start);
}

void Enemy::BeginPressureAction() {
    if (IsPlayerInMeleeFront()) {
        if (TryBeginPhantomWarpSkill(phantomWarpChance_)) {
            return;
        }
        if (TryBeginQuickSlash(quickSlashChance_)) {
            return;
        }
        if (TryBeginBladeClash(bladeClashChance_)) {
            return;
        }
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

    if (IsPlayerInMeleeFront()) {
        BeginPressureAction();
        return;
    }

    if (distance >= warpCutInDistance_ &&
        TryBeginFarWarpSlash(farWarpSlashChance_)) {
        return;
    }
    if (TryBeginPhantomWarpSkill(phantomWarpChance_ * 0.72f)) {
        return;
    }

    const float chance =
        distance >= warpCutInDistance_ ? warpCutInChance_ : warpFarChance_;
    if (TryBeginWarpAction(chance)) {
        return;
    }

    BeginStalkAction();
}

