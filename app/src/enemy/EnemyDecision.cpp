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

CounterReadAxis Enemy::GetCounterReadAxis(ActionKind kind) const {
    switch (kind) {
    case ActionKind::Smash:
        return CounterReadAxis::Vertical;
    case ActionKind::Sweep:
        return CounterReadAxis::Horizontal;
    case ActionKind::Wave:
        return CounterReadAxis::Radial;
    case ActionKind::Shot:
        return CounterReadAxis::Projectile;
    default:
        return CounterReadAxis::None;
    }
}

bool Enemy::ShouldEnterSmashHold() const {
    float chance = GetAdaptiveHoldChance(ActionKind::Smash);
    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return r < chance;
}

bool Enemy::ShouldEnterSweepHold() const {
    float chance = GetAdaptiveHoldChance(ActionKind::Sweep);
    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return r < chance;
}

void Enemy::EnterHold(float duration) {
    holdConfigured_ = true;
    currentHoldDuration_ = duration;

    holdBranchType_ = HoldBranchType::None;
    holdBranchDecided_ = false;

    holdBranchDecisionTime_ = duration * 0.55f;
    if (holdBranchDecisionTime_ < 0.04f) {
        holdBranchDecisionTime_ = 0.04f;
    }

    ResetPreAttackPresentationState();
}

bool Enemy::IsCounterFailObserved() const {
    return playerObs_.justCounterFailed || playerObs_.justCounterEarly ||
           playerObs_.justCounterLate;
}

float Enemy::RandomRange(float minValue, float maxValue) const {
    if (maxValue < minValue) {
        return minValue;
    }

    float t = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return minValue + (maxValue - minValue) * t;
}

void Enemy::DecideHoldBranch(ActionKind kind) {
    holdBranchType_ = HoldBranchType::Active;
    holdBranchDecided_ = true;

    float warpChance = 0.0f;
    if (kind == ActionKind::Smash) {
        warpChance = smashHoldBranchWarpChance_;
    } else if (kind == ActionKind::Sweep) {
        warpChance = sweepHoldBranchWarpChance_;
    } else {
        return;
    }

    if (IsWarpSuspendedForPresentation()) {
        warpChance = 0.0f;
    }

    if (playerObs_.isCounterStance) {
        warpChance += 0.10f;
    }

    if (playerObs_.justCounterEarly || counterMemory_.earlyCount > 0.6f) {
        warpChance += 0.08f;
    }

    if (postCounterRhythmTimer_ > 0.0f) {
        warpChance += 0.10f;
    }

    float roll = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (roll < warpChance) {
        holdBranchType_ = HoldBranchType::Warp;
    }
}

bool Enemy::TryExecuteHoldBranch(ActionKind kind) {
    (void)kind;
    if (!holdBranchDecided_) {
        return false;
    }

    if (holdBranchType_ == HoldBranchType::Warp && PrepareWarpContext()) {
        BeginAction(ActionKind::Warp, ActionStep::Start);
        return true;
    }

    holdBranchType_ = HoldBranchType::Active;
    return false;
}

void Enemy::EnterTell(ActionKind kind) {
    tellActive_ = true;
    fakeCommitActive_ = false;
    freezeHoldActive_ = false;

    if (kind == ActionKind::Smash) {
        tellDuration_ = smashTellTime_;
    } else if (kind == ActionKind::Sweep) {
        tellDuration_ = sweepTellTime_;
    } else {
        tellDuration_ = 0.0f;
    }
}

bool Enemy::IsTellFinished() const { return stateTimer_ >= tellDuration_; }

bool Enemy::ShouldDoFakeCommit(ActionKind kind) const {
    float chance = 0.0f;

    if (kind == ActionKind::Smash) {
        chance = smashFakeCommitChance_;
        if (playerObs_.isCounterStance) {
            chance += 0.18f;
        }
        if (counterMemory_.earlyCount > 0.6f) {
            chance += 0.12f;
        }
        if (action_.id == ActionId::DelaySmash) {
            chance += 0.15f;
        }
    } else if (kind == ActionKind::Sweep) {
        chance = sweepFakeCommitChance_;
        if (playerObs_.isCounterStance) {
            chance += 0.12f;
        }
        if (counterMemory_.earlyCount > 0.6f) {
            chance += 0.08f;
        }
    }

    if (postCounterRhythmTimer_ > 0.0f) {
        chance += 0.10f;
    }

    chance = (std::clamp)(chance, 0.0f, 0.95f);
    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return r < chance;
}

void Enemy::EnterFakeCommit(ActionKind kind) {
    tellActive_ = false;
    fakeCommitActive_ = true;
    freezeHoldActive_ = false;

    if (kind == ActionKind::Smash) {
        fakeCommitDuration_ = smashFakeCommitTime_;
    } else if (kind == ActionKind::Sweep) {
        fakeCommitDuration_ = sweepFakeCommitTime_;
    } else {
        fakeCommitDuration_ = 0.0f;
    }
}

bool Enemy::IsFakeCommitFinished() const {
    return stateTimer_ >= fakeCommitDuration_;
}

void Enemy::EnterFreezeHold(ActionKind kind) {
    tellActive_ = false;
    fakeCommitActive_ = false;
    freezeHoldActive_ = true;

    if (kind == ActionKind::Smash) {
        freezeHoldDuration_ =
            RandomRange(smashFreezeHoldTimeMin_, smashFreezeHoldTimeMax_);
    } else if (kind == ActionKind::Sweep) {
        freezeHoldDuration_ =
            RandomRange(sweepFreezeHoldTimeMin_, sweepFreezeHoldTimeMax_);
    } else {
        freezeHoldDuration_ = 0.0f;
    }
}

bool Enemy::IsFreezeHoldFinished() const {
    return stateTimer_ >= freezeHoldDuration_;
}

void Enemy::ResetPreAttackPresentationState() {
    tellActive_ = false;
    fakeCommitActive_ = false;
    freezeHoldActive_ = false;

    tellDuration_ = 0.0f;
    fakeCommitDuration_ = 0.0f;
    freezeHoldDuration_ = 0.0f;
}

void Enemy::ResetRecoveryBranchState() {
    recoveryBranchType_ = RecoveryBranchType::None;
    recoveryFollowupKind_ = ActionKind::None;
    recoveryFollowupStep_ = ActionStep::None;
    recoveryFollowupDelayTimer_ = 0.0f;
    isMargitComboATransition_ = false;
}

bool Enemy::TryBranchFromRecovery(ActionKind finishedKind) {
    ResetRecoveryBranchState();

    if (!(finishedKind == ActionKind::Smash || finishedKind == ActionKind::Sweep ||
          finishedKind == ActionKind::Shot)) {
        return false;
    }

    if (finishedKind == ActionKind::Shot) {
        float distance = GetDistanceToPlayer();
        const bool canShotWarp = !IsWarpSuspendedForPresentation() &&
                                 distance >= shotWarpMinDistance_ &&
                                 distance <= shotWarpMaxDistance_;

        if (!canShotWarp) {
            return false;
        }

        float shotWarpChance = shotWarpFollowupChance_;
        if (phase_ == BossPhase::Phase2) {
            shotWarpChance += phase2ShotWarpFollowupBonus_;
        }
        if (playerObs_.isGuarding) {
            shotWarpChance += 0.06f;
        }
        if (playerObs_.isCounterStance) {
            shotWarpChance += 0.08f;
        }

        shotWarpChance = (std::clamp)(shotWarpChance, 0.0f, 0.80f);
        float roll =
            static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        if (roll >= shotWarpChance) {
            return false;
        }

        ResetWarpContext();
        warp_.type = WarpType::Approach;
        if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
            ResetWarpContext();
            return false;
        }

        warp_.hasValidTarget = true;
        recoveryBranchType_ = RecoveryBranchType::Recommit;
        recoveryFollowupKind_ = ActionKind::Warp;
        recoveryFollowupStep_ = ActionStep::Start;
        recoveryFollowupDelayTimer_ =
            RandomRange(shotWarpFollowupDelayMin_, shotWarpFollowupDelayMax_);
        return true;
    }

    const bool forceCombo =
        phase_ == BossPhase::Phase2 && finishedKind == ActionKind::Smash &&
        action_.id == ActionId::DelaySmash &&
        (currentActionConnected_ || currentActionGuarded_);
    if (forceCombo) {
        recoveryBranchType_ = RecoveryBranchType::Recommit;
        recoveryFollowupKind_ = ActionKind::Sweep;
        recoveryFollowupStep_ = ActionStep::Charge;
        recoveryFollowupDelayTimer_ =
            RandomRange(margitComboFollowupDelayMin_, margitComboFollowupDelayMax_);
        isMargitComboATransition_ = true;
        return true;
    }

    float recommitChance = recommitChance_;
    float delayedSecondChance = delayedSecondChance_;
    float fakeoutChance =
        IsWarpSuspendedForPresentation() ? 0.0f : escapeFakeoutChance_;

    if (phase_ == BossPhase::Phase2) {
        recommitChance += phase2RecommitBonus_;
        delayedSecondChance += phase2DelayedSecondBonus_;
    }

    if (playerObs_.isCounterStance) {
        delayedSecondChance += 0.08f;
        fakeoutChance += 0.08f;
    }

    if (postCounterRhythmTimer_ > 0.0f) {
        recommitChance += 0.04f;
        delayedSecondChance += 0.06f;
        fakeoutChance += 0.12f;
    }

    if (counterMemory_.earlyCount > 0.6f) {
        delayedSecondChance += 0.08f;
    }

    float total = recommitChance + delayedSecondChance + fakeoutChance;
    if (total <= 0.0f) {
        return false;
    }

    float roll = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (roll >= total) {
        return false;
    }

    float pick =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * total;
    if (pick < recommitChance) {
        recoveryBranchType_ = RecoveryBranchType::Recommit;
        recoveryFollowupKind_ =
            (finishedKind == ActionKind::Smash) ? ActionKind::Sweep
                                                : ActionKind::Smash;
        recoveryFollowupStep_ = ActionStep::Charge;
        recoveryFollowupDelayTimer_ =
            RandomRange(recommitDelayMin_, recommitDelayMax_);
        return true;
    }

    if (pick < recommitChance + delayedSecondChance) {
        recoveryBranchType_ = RecoveryBranchType::DelayedSecond;
        recoveryFollowupKind_ = finishedKind;
        recoveryFollowupStep_ = ActionStep::Charge;
        recoveryFollowupDelayTimer_ =
            RandomRange(delayedSecondDelayMin_, delayedSecondDelayMax_);
        return true;
    }

    recoveryBranchType_ = RecoveryBranchType::EscapeFakeout;
    ResetWarpContext();
    warp_.type = WarpType::Escape;
    if (!DecideWarpTargetFarFromPlayer(warp_.targetPos)) {
        ResetRecoveryBranchState();
        return false;
    }

    warp_.hasValidTarget = true;
    BeginAction(ActionKind::Warp, ActionStep::Start);
    return true;
}

void Enemy::UpdateCounterAdaptation(float deltaTime) {
    const float decay = (std::max)(0.0f, 1.0f - deltaTime * 0.55f);

    counterMemory_.counterStancePressure *= decay;
    counterMemory_.earlyCount *= decay;
    counterMemory_.lateCount *= decay;
    counterMemory_.successCount *= decay;
    counterMemory_.verticalBias *= decay;
    counterMemory_.horizontalBias *= decay;

    if (playerObs_.isCounterStance) {
        counterMemory_.counterStancePressure += deltaTime * 1.4f;
    }

    if (playerObs_.justCounterEarly) {
        counterMemory_.earlyCount += 1.0f;
    }
    if (playerObs_.justCounterLate) {
        counterMemory_.lateCount += 1.0f;
    }

    if (playerObs_.counterAxis == CounterAxis::Vertical) {
        counterMemory_.verticalBias += deltaTime * 1.2f;
    } else if (playerObs_.counterAxis == CounterAxis::Horizontal) {
        counterMemory_.horizontalBias += deltaTime * 1.2f;
    }

    if (postCounterRhythmTimer_ > 0.0f) {
        postCounterRhythmTimer_ -= deltaTime;
        if (postCounterRhythmTimer_ < 0.0f) {
            postCounterRhythmTimer_ = 0.0f;
        }
    } else {
        forceEscapeWarpNext_ = false;
        forceCounterBaitNext_ = false;
    }
}

void Enemy::RegisterCounterSuccessReaction() {
    counterMemory_.successCount += 1.4f;
    counterMemory_.consecutiveSuccess++;

    if (action_.kind == ActionKind::Smash) {
        counterMemory_.verticalBias += 0.8f;
    } else if (action_.kind == ActionKind::Sweep) {
        counterMemory_.horizontalBias += 0.8f;
    }

    if (counterMemory_.consecutiveSuccess >= 2) {
        forceEscapeWarpNext_ = true;
        forceCounterBaitNext_ = true;
        postCounterRhythmTimer_ = 4.0f;
    } else {
        postCounterRhythmTimer_ = 2.0f;
    }
}

float Enemy::GetAdaptiveHoldChance(ActionKind kind) const {
    float chance = 0.0f;

    if (kind == ActionKind::Smash) {
        chance = config_.attacks.smash.melee.feintChance;
        if (action_.id == ActionId::DelaySmash) {
            chance += 0.20f;
        }
    } else if (kind == ActionKind::Sweep) {
        chance = config_.attacks.sweep.melee.feintChance;
    }

    chance += counterMemory_.counterStancePressure * 0.12f;
    chance += counterMemory_.successCount * 0.08f;
    chance += counterMemory_.earlyCount * 0.10f;
    chance = (std::clamp)(chance, 0.0f, 0.95f);
    return chance;
}

float Enemy::GetAdaptiveChargeOffset(ActionKind kind) const {
    float offset = 0.0f;
    offset += counterMemory_.earlyCount * 0.035f;
    offset -= counterMemory_.lateCount * 0.015f;

    if (postCounterRhythmTimer_ > 0.0f) {
        if (kind == ActionKind::Smash) {
            offset += 0.08f;
        } else if (kind == ActionKind::Sweep) {
            offset += 0.05f;
        }
    }

    return (std::clamp)(offset, -0.08f, 0.28f);
}

bool Enemy::ShouldSnapReleaseFromRead() const {
    if (currentHoldDuration_ <= 0.0f) {
        return false;
    }

    float releaseRatio = 0.60f;
    if (playerObs_.justCounterEarly) {
        releaseRatio = 0.30f;
    } else if (playerObs_.justCounterLate) {
        releaseRatio = 0.75f;
    } else if (playerObs_.justCounterFailed) {
        releaseRatio = 0.45f;
    } else if (playerObs_.isCounterStance) {
        releaseRatio = 0.55f;
    }

    return stateTimer_ >= currentHoldDuration_ * releaseRatio;
}

ActionKind Enemy::DecideAdaptiveCounterBaitAction() const {
    if (playerObs_.counterAxis == CounterAxis::Horizontal) {
        return ActionKind::Smash;
    }
    if (playerObs_.counterAxis == CounterAxis::Vertical) {
        return ActionKind::Sweep;
    }

    if (counterMemory_.horizontalBias > counterMemory_.verticalBias + 0.4f) {
        return ActionKind::Smash;
    }
    if (counterMemory_.verticalBias > counterMemory_.horizontalBias + 0.4f) {
        return ActionKind::Sweep;
    }

    return (std::rand() % 2 == 0) ? ActionKind::Smash : ActionKind::Sweep;
}

void Enemy::UpdateIdle(float deltaTime) {
    if (recoveryFollowupKind_ != ActionKind::None &&
        recoveryFollowupStep_ != ActionStep::None) {
        if (recoveryFollowupDelayTimer_ > 0.0f) {
            recoveryFollowupDelayTimer_ -= deltaTime;
            if (recoveryFollowupDelayTimer_ > 0.0f) {
                return;
            }
            recoveryFollowupDelayTimer_ = 0.0f;
        }

        const ActionKind nextKind = recoveryFollowupKind_;
        const ActionStep nextStep = recoveryFollowupStep_;
        ResetRecoveryBranchState();
        BeginAction(nextKind, nextStep);
        return;
    }

    if (stateTimer_ < 0.35f) {
        return;
    }

    tactic_ = DecideTactic();
    BeginActionFromTactic(tactic_);
}

TacticState Enemy::DecideTactic() const {
    const float distance = GetDistanceToPlayer();
    const bool canWarp = !IsWarpSuspendedForPresentation();
    const bool isNear = distance <= config_.core.nearAttackDistance;
    const bool isFar = distance >= config_.core.farAttackDistance;
    const bool isMidRange = !isNear && !isFar;
    const bool shouldWarp =
        canWarp &&
        (forceEscapeWarpNext_ || isDistanceStagnant_ ||
         farDistanceTimer_ >= farDistanceWarpTimeThreshold_ ||
         (isMidRange && lastActionKind_ != ActionKind::Warp &&
          (playerObs_.isAttacking || playerObs_.isGuarding)));

    if (shouldWarp) {
        return TacticState::Warp;
    }
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

bool Enemy::TryBeginStalkAction(float chance, float repeatScale) {
    if (lastActionKind_ == ActionKind::Stalk) {
        chance *= repeatScale;
    }
    if (stalkRepeatCount_ >= stalkRepeatLimit_) {
        chance = 0.0f;
    }

    float roll = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (roll >= chance) {
        return false;
    }

    return TryBeginTacticAction(ActionKind::Stalk);
}

ActionKind Enemy::SelectNeutralAction(float distance) const {
    if (distance <= config_.core.nearAttackDistance) {
        return SelectNearPressureAction();
    }

    int shotWeight = midShotWeight_ + neutralMidShotBonus_;
    int waveWeight = midWaveWeight_ + neutralMidWaveBonus_;

    if (playerObs_.isGuarding) {
        waveWeight += 12;
        shotWeight += 6;
    }

    switch (PickWeightedIndex({shotWeight, waveWeight})) {
    case 0:
        return ActionKind::Shot;
    default:
        return ActionKind::Wave;
    }
}

ActionKind Enemy::SelectNearPressureAction() const {
    int smashWeight = nearSmashWeight_;
    int sweepWeight = nearSweepWeight_;

    if (phase_ == BossPhase::Phase2) {
        smashWeight += phase2NearSmashBonus_;
        sweepWeight += phase2NearSweepBonus_;
    }

    if (postCounterRhythmTimer_ > 0.0f) {
        smashWeight = static_cast<int>(smashWeight * 0.7f);
        sweepWeight = static_cast<int>(sweepWeight * 0.7f);
    }

    if (playerObs_.isAttacking) {
        sweepWeight += 10;
    }
    if (playerObs_.isGuarding) {
        sweepWeight += 4;
    }
    if (playerObs_.isCounterStance) {
        smashWeight -= 6;
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

    float chance = 0.22f;
    if (playerObs_.isGuarding) {
        chance += 0.18f;
    }
    if (playerObs_.isCounterStance) {
        chance += 0.15f;
    }
    if (playerObs_.isAttacking) {
        chance += 0.08f;
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

    float forwardX = playerObs_.velocity.x;
    float forwardZ = playerObs_.velocity.z;
    float forwardLength = std::sqrt(forwardX * forwardX + forwardZ * forwardZ);

    if (forwardLength <= 0.0001f) {
        forwardX = playerPos_.x - tf_.position.x;
        forwardZ = playerPos_.z - tf_.position.z;
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
    warp_.approachSlot = WarpApproachSlot::DirectBack;
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
