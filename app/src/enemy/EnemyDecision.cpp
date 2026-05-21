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
    case ActionKind::Cage:
        return CounterReadAxis::Radial;
    case ActionKind::BladeClash:
        return CounterReadAxis::None;
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
    if (playerObs_.isAttacking) {
        currentHoldDuration_ += RandomRange(0.10f, 0.22f);
    }
    if (playerObs_.justCounterEarly || counterMemory_.earlyCount > 0.6f) {
        currentHoldDuration_ += RandomRange(0.12f, 0.26f);
    }
    if (postCounterRhythmTimer_ > 0.0f || forceCounterBaitNext_) {
        currentHoldDuration_ += RandomRange(0.14f, 0.30f);
    }

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
    (void)kind;
    holdBranchType_ = HoldBranchType::Active;
    holdBranchDecided_ = true;
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

bool Enemy::TryBeginPhase2FeintWarp(ActionKind kind) {
    if (phase_ == BossPhase::Phase1 || phase2FeintFollowupLocked_ ||
        phase2FeintDecisionMade_ || IsWarpSuspendedForPresentation()) {
        return false;
    }
    if (!(kind == ActionKind::Smash || kind == ActionKind::Sweep) ||
        action_.kind != kind ||
        !(action_.step == ActionStep::Charge ||
          action_.step == ActionStep::Hold)) {
        return false;
    }

    constexpr float kFeintGreenCueRatio = 0.12f;
    if (GetReleaseAnticipationRatio() < kFeintGreenCueRatio) {
        return false;
    }

    phase2FeintDecisionMade_ = true;

    float chance = kind == ActionKind::Smash ? 0.42f : 0.36f;
    if (playerObs_.isCounterStance) {
        chance += 0.16f;
    }
    if (playerObs_.isAttacking || playerObs_.justCounterEarly) {
        chance += 0.12f;
    }
    if (postCounterRhythmTimer_ > 0.0f || forceCounterBaitNext_) {
        chance += 0.14f;
    }
    chance += counterMemory_.successCount * 0.05f;
    chance = (std::clamp)(chance, 0.0f, 0.78f);

    const float roll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (roll >= chance) {
        return false;
    }

    ResetWarpContext();
    warp_.type = WarpType::Approach;
    warp_.approachSlot =
        (std::rand() % 2 == 0) ? WarpApproachSlot::Back
                               : WarpApproachSlot::Front;
    if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
        ResetWarpContext();
        return false;
    }

    warp_.hasValidTarget = true;
    warp_.followupKind = kind;
    warp_.followupStep = ActionStep::Charge;
    warp_.phase2FeintFollowup = true;
    warp_.phase2FeintImmediateGreen = (std::rand() % 2) == 0;
    warp_.faceLivePlayerOnEnd = true;
    BeginAction(ActionKind::Warp, ActionStep::Start);
    return true;
}

bool Enemy::TryApplyPhase2DirectionFeint(ActionKind kind) {
    if (phase_ == BossPhase::Phase1 || phase2FeintFollowupLocked_ ||
        phase2DirectionFeintDecisionMade_) {
        return false;
    }
    if (!(kind == ActionKind::Smash || kind == ActionKind::Sweep) ||
        action_.kind != kind || action_.step != ActionStep::Charge) {
        return false;
    }
    if (kind == ActionKind::Smash && action_.id == ActionId::DelaySmash) {
        return false;
    }

    const float chargeTime = kind == ActionKind::Smash
                                 ? GetCurrentSmashChargeTime()
                                 : GetCurrentSweepChargeTime();
    const float redSwitchTime = (std::max)(0.22f, chargeTime - 0.74f);
    if (stateTimer_ < redSwitchTime || GetReleaseAnticipationRatio() > 0.0f) {
        return false;
    }

    phase2DirectionFeintDecisionMade_ = true;
    float chance = kind == ActionKind::Smash ? 0.28f : 0.32f;
    if (playerObs_.isCounterStance || playerObs_.isAttacking) {
        chance += 0.12f;
    }
    if (postCounterRhythmTimer_ > 0.0f || forceCounterBaitNext_) {
        chance += 0.10f;
    }
    chance = (std::clamp)(chance, 0.0f, 0.62f);

    const float roll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (roll >= chance) {
        return false;
    }

    const ActionKind nextKind =
        kind == ActionKind::Smash ? ActionKind::Sweep : ActionKind::Smash;
    action_.kind = nextKind;
    action_.id = MakeDefaultActionId(nextKind);
    phase2FeintDecisionMade_ = true;
    hasTrackingLocked_ = false;
    holdConfigured_ = false;
    currentHoldDuration_ = 0.0f;
    holdBranchType_ = HoldBranchType::None;
    holdBranchDecided_ = false;
    holdBranchDecisionTime_ = 0.0f;
    ResetPreAttackPresentationState();

    constexpr float kDirectionFeintGreenCueWindow = 0.52f;
    constexpr float kDirectionFeintRedAfterSwitchTime = 0.08f;
    const float nextChargeTime =
        nextKind == ActionKind::Smash ? GetCurrentSmashChargeTime()
                                      : GetCurrentSweepChargeTime();
    stateTimer_ =
        (std::max)(0.18f, nextChargeTime - kDirectionFeintGreenCueWindow -
                              kDirectionFeintRedAfterSwitchTime);
    return true;
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
        if (playerObs_.isAttacking) {
            chance += 0.12f;
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
        if (playerObs_.isAttacking) {
            chance += 0.10f;
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
    if (playerObs_.isAttacking) {
        freezeHoldDuration_ += RandomRange(0.08f, 0.18f);
    }
    if (postCounterRhythmTimer_ > 0.0f || forceCounterBaitNext_) {
        freezeHoldDuration_ += RandomRange(0.10f, 0.24f);
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

    if (!(finishedKind == ActionKind::Smash ||
          finishedKind == ActionKind::Sweep ||
          finishedKind == ActionKind::Cage ||
          finishedKind == ActionKind::BladeClash)) {
        return false;
    }

    if (finishedKind == ActionKind::BladeClash) {
        return false;
    }

    if (finishedKind == ActionKind::Cage) {
        float chainChance = 0.34f;
        if (phase_ != BossPhase::Phase1) {
            chainChance += 0.22f;
        }
        if (playerObs_.isCounterStance || playerObs_.isAttacking) {
            chainChance += 0.12f;
        }
        if (postCounterRhythmTimer_ > 0.0f || forceCounterBaitNext_) {
            chainChance += 0.14f;
        }
        chainChance = (std::clamp)(chainChance, 0.0f, 0.82f);

        const float chainRoll =
            static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        if (chainRoll >= chainChance) {
            return false;
        }

        if (!IsWarpSuspendedForPresentation() &&
            TryBeginWarpBehindMeleeSkill(true)) {
            return true;
        }

        BeginPressureAction();
        return true;
    }

    if (finishedKind == ActionKind::Smash ||
        finishedKind == ActionKind::Sweep) {
        const bool forceCombo =
            phase_ != BossPhase::Phase1 && finishedKind == ActionKind::Smash &&
            action_.id == ActionId::DelaySmash &&
            (currentActionConnected_ || currentActionGuarded_);
        if (forceCombo) {
            BeginAction(ActionKind::Sweep, ActionStep::Charge);
            isMargitComboATransition_ = true;
            return true;
        }

        float recommitChance = recommitChance_;
        float delayedSecondChance = delayedSecondChance_;
        float fakeoutChance =
            IsWarpSuspendedForPresentation() ? 0.0f : escapeFakeoutChance_;

        if (phase_ != BossPhase::Phase1) {
            recommitChance += phase2RecommitBonus_;
            delayedSecondChance += phase2DelayedSecondBonus_;
            recommitChance += phase2RecoveryBranchChanceBonus_;
            delayedSecondChance += phase2RecoveryBranchChanceBonus_;
            fakeoutChance += phase2RecoveryBranchChanceBonus_;
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

        const float branchWeightTotal =
            recommitChance + delayedSecondChance + fakeoutChance;
        const float branchChance =
            (std::clamp)(branchWeightTotal, 0.0f,
                         phase_ != BossPhase::Phase1 ? 0.92f : 0.82f);
        if (branchWeightTotal <= 0.0f || branchChance <= 0.0f) {
            return false;
        }

        float roll =
            static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        if (roll >= branchChance) {
            return false;
        }

        float pick = static_cast<float>(std::rand()) /
                     static_cast<float>(RAND_MAX) * branchWeightTotal;
        if (pick < recommitChance) {
            const ActionKind followupKind =
                (finishedKind == ActionKind::Smash) ? ActionKind::Sweep
                                                    : ActionKind::Smash;
            BeginAction(followupKind, ActionStep::Charge);
            return true;
        }

        if (pick < recommitChance + delayedSecondChance) {
            BeginAction(finishedKind, ActionStep::Charge);
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

    return false;
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
    if (playerObs_.isAttacking) {
        chance += 0.16f;
    }
    if (postCounterRhythmTimer_ > 0.0f || forceCounterBaitNext_) {
        chance += 0.20f;
    }
    chance = (std::clamp)(chance, 0.0f, 0.95f);
    return chance;
}

float Enemy::GetAdaptiveChargeOffset(ActionKind kind) const {
    float offset = 0.0f;
    offset += counterMemory_.earlyCount * 0.035f;
    offset -= counterMemory_.lateCount * 0.015f;
    if (playerObs_.isAttacking) {
        offset += 0.06f;
    }

    if (postCounterRhythmTimer_ > 0.0f) {
        if (kind == ActionKind::Smash) {
            offset += 0.14f;
        } else if (kind == ActionKind::Sweep) {
            offset += 0.10f;
        }
    }
    if (forceCounterBaitNext_) {
        offset += 0.12f;
    }

    return (std::clamp)(offset, -0.08f, 0.42f);
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
    const bool shouldWarp = canWarp && forceEscapeWarpNext_;

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

    int stalkWeight = 36;
    int bladeClashWeight = distance >= config_.core.farAttackDistance ? 10 : 18;
    if (!CanBeginPhaseBladeClash()) {
        bladeClashWeight = 0;
    }

    if (lastActionKind_ == ActionKind::Stalk) {
        stalkWeight /= 2;
    } else if (lastActionKind_ == ActionKind::BladeClash) {
        bladeClashWeight /= 3;
    }

    if (playerObs_.isAttacking || playerObs_.isCounterStance) {
        bladeClashWeight += 12;
    }

    switch (PickWeightedIndex({stalkWeight, bladeClashWeight})) {
    case 1:
        return ActionKind::BladeClash;
    default:
        return ActionKind::Stalk;
    }
}

ActionKind Enemy::SelectNearPressureAction() const {
    int smashWeight = nearSmashWeight_;
    int delaySmashWeight = 12;
    int sweepWeight = nearSweepWeight_;
    int bladeClashWeight = 18;
    if (!CanBeginPhaseBladeClash()) {
        bladeClashWeight = 0;
    }

    if (forceCounterBaitNext_ || postCounterRhythmTimer_ > 0.0f ||
        playerObs_.justCounterEarly) {
        const float baitRoll =
            static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        if (baitRoll < 0.68f) {
            return DecideAdaptiveCounterBaitAction();
        }
    }

    if (phase_ != BossPhase::Phase1) {
        smashWeight += phase2NearSmashBonus_;
        delaySmashWeight += 4;
        sweepWeight += phase2NearSweepBonus_;
        bladeClashWeight += 6;
    }
    if (phase_ == BossPhase::Phase3) {
        smashWeight += phase3NearSmashBonus_;
        delaySmashWeight += 2;
        sweepWeight += phase3NearSweepBonus_;
        bladeClashWeight += 4;
    }

    if (postCounterRhythmTimer_ > 0.0f) {
        smashWeight = static_cast<int>(smashWeight * 0.7f);
        delaySmashWeight = static_cast<int>(delaySmashWeight * 0.8f);
        sweepWeight = static_cast<int>(sweepWeight * 0.7f);
        bladeClashWeight += 10;
    }

    if (playerObs_.isAttacking) {
        delaySmashWeight += 2;
        sweepWeight += 10;
        bladeClashWeight += 12;
    }
    if (playerObs_.isGuarding) {
        sweepWeight += 4;
    }
    if (playerObs_.isCounterStance) {
        smashWeight -= 6;
        delaySmashWeight += 3;
        sweepWeight += 4;
        bladeClashWeight += 12;
    }

    if (lastActionKind_ == ActionKind::Smash) {
        smashWeight /= 2;
        delaySmashWeight /= 2;
    } else if (lastActionKind_ == ActionKind::Sweep) {
        sweepWeight /= 2;
    } else if (lastActionKind_ == ActionKind::BladeClash) {
        bladeClashWeight /= 3;
    }

    switch (PickWeightedIndex(
        {smashWeight, delaySmashWeight, sweepWeight, bladeClashWeight})) {
    case 0:
        return ActionKind::Smash;
    case 1:
        return ActionKind::DelaySmash;
    case 3:
        return ActionKind::BladeClash;
    default:
        return ActionKind::Sweep;
    }
}

ActionKind Enemy::SelectChaseAction() const { return ActionKind::Stalk; }

bool Enemy::IsQuickCounterAction() const {
    return action_.id == ActionId::QuickSmash ||
           action_.id == ActionId::QuickSweep;
}

bool Enemy::ShouldBeginQuickCounterAttack() const {
    if (phaseTransitionActive_ || deathFinished_ || isDying_) {
        return false;
    }
    if (phase_ == BossPhase::Phase3) {
        return false;
    }

    if (!quickCounterOpeningUsed_) {
        return true;
    }

    float chance = quickCounterAttackChance_;
    if (phase_ == BossPhase::Phase2) {
        chance += 0.08f;
    }
    if (playerObs_.isGuarding || playerObs_.isCounterStance) {
        chance += 0.14f;
    }
    if (playerObs_.isAttacking) {
        chance += 0.08f;
    }
    if (postCounterRhythmTimer_ > 0.0f || forceCounterBaitNext_) {
        chance += 0.10f;
    }
    if (lastActionKind_ == ActionKind::Smash ||
        lastActionKind_ == ActionKind::Sweep) {
        chance *= 0.58f;
    }

    chance = (std::clamp)(chance, 0.0f, 0.85f);
    const float roll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return roll < chance;
}

void Enemy::BeginQuickCounterAttack() {
    const ActionKind kind = DecideAdaptiveCounterBaitAction();
    BeginAction(kind, ActionStep::Charge);
    action_.id =
        kind == ActionKind::Smash ? ActionId::QuickSmash : ActionId::QuickSweep;
    quickCounterOpeningUsed_ = true;
    phase2FeintDecisionMade_ = true;
    phase2DirectionFeintDecisionMade_ = true;
    holdConfigured_ = false;
    currentHoldDuration_ = 0.0f;
}

void Enemy::BeginNeutralAction() {
    const float distance = GetDistanceToPlayer();

    if (distance <= config_.core.nearAttackDistance) {
        BeginPressureAction();
        return;
    }

    stalkRepeatCount_ = 0;
    if (phase_ != BossPhase::Phase1 &&
        TryBeginPhase3PhantomWarpSkill(phase3PhantomWarpChance_ * 0.72f)) {
        return;
    }
    TryBeginTacticAction(SelectNeutralAction(distance));
}

void Enemy::BeginPressureAction() {
    const float distance = GetDistanceToPlayer();

    if (distance <= config_.core.nearAttackDistance) {
        stalkRepeatCount_ = 0;
        if (phase_ != BossPhase::Phase1 &&
            TryBeginPhase3PhantomWarpSkill(phase3PhantomWarpChance_)) {
            return;
        }
        if (ShouldBeginQuickCounterAttack()) {
            BeginQuickCounterAttack();
            return;
        }
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
    if (playerObs_.isCounterStance) {
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

bool Enemy::TryBeginPhase3PhantomWarpSkill(float chance) {
    if (phase_ == BossPhase::Phase1 || deathFinished_ || isDying_ ||
        hp_ <= 0.0f || phaseTransitionActive_ ||
        IsWarpSuspendedForPresentation() ||
        phase3PhantomWarpCooldown_ > 0.0f) {
        return false;
    }

    const float distance = GetDistanceToPlayer();
    if (distance < 1.65f || distance > 9.8f) {
        return false;
    }

    if (lastActionKind_ == ActionKind::Warp) {
        chance *= 0.42f;
    }
    if (playerObs_.isCounterStance || playerObs_.isGuarding) {
        chance += 0.08f;
    }
    if (playerObs_.isAttacking) {
        chance += 0.06f;
    }
    chance = (std::clamp)(chance, 0.0f, 0.72f);

    const float roll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (roll >= chance) {
        return false;
    }

    BeginPhase3PhantomWarpStep(3, false, ActionKind::None);
    phase3PhantomWarpCooldown_ = phase3PhantomWarpCooldownDuration_;
    return true;
}

bool Enemy::ForcePhase3PhantomWarpSkill() {
    if (deathFinished_ || isDying_ || hp_ <= 0.0f || phaseTransitionActive_ ||
        IsWarpSuspendedForPresentation()) {
        return false;
    }

    hitReactionTimer_ = 0.0f;
    counterRecoilTimer_ = 0.0f;
    BeginPhase3PhantomWarpStep(3, false, ActionKind::None);
    phase3PhantomWarpCooldown_ = phase3PhantomWarpCooldownDuration_;
    return true;
}
