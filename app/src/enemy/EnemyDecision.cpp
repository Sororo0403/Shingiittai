#include "Enemy.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>

namespace {
enum class BossDecisionAction {
    Smash,
    Sweep,
    QuickSlash,
    BladeClash,
    Warp,
    FarWarpSlash,
    PhantomWarp,
    TripleIaiSlash,
    Stalk,
    ArcaneLaser,
    CataclysmLaser,
};

struct WeightedActionChoice {
    BossDecisionAction action = BossDecisionAction::Stalk;
    int weight = 0;
};

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

BossDecisionAction PickWeightedAction(
    std::initializer_list<WeightedActionChoice> choices,
    BossDecisionAction fallback) {
    int total = 0;
    for (const WeightedActionChoice &choice : choices) {
        total += (std::max)(0, choice.weight);
    }

    if (total <= 0) {
        return fallback;
    }

    int roll = std::rand() % total;
    for (const WeightedActionChoice &choice : choices) {
        const int weight = (std::max)(0, choice.weight);
        if (roll < weight) {
            return choice.action;
        }
        roll -= weight;
    }

    return fallback;
}
} // namespace

float Enemy::TechniqueUnlock(BossPhase requiredPhase) const {
    if (difficulty_ >= 7.0f) {
        return 1.0f;
    }

    const int currentPhase = static_cast<int>(phase_);
    const int required = static_cast<int>(requiredPhase);
    return currentPhase >= required ? 1.0f : 0.0f;
}

bool Enemy::ShouldEnterSmashHold() const {
    if (quickSlashActive_ || farSlashActive_) {
        return false;
    }
    if (phase_ == BossPhase::Phase3) {
        return false;
    }

    const float unlock = TechniqueUnlock(BossPhase::Phase2);
    if (unlock <= 0.0f) {
        return false;
    }
    float chance = config_.attacks.smash.melee.feintChance;
    if (phase_ == BossPhase::Phase2) {
        chance *= 0.25f;
    }
    chance *= unlock;
    chance = (std::clamp)(chance, 0.0f, 0.88f);
    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return r < chance;
}

bool Enemy::ShouldEnterSweepHold() const {
    if (quickSlashActive_ || farSlashActive_) {
        return false;
    }
    if (phase_ == BossPhase::Phase3) {
        return false;
    }

    const float unlock = TechniqueUnlock(BossPhase::Phase2);
    if (unlock <= 0.0f) {
        return false;
    }
    float chance = config_.attacks.sweep.melee.feintChance;
    if (phase_ == BossPhase::Phase2) {
        chance *= 0.25f;
    }
    chance *= unlock;
    chance = (std::clamp)(chance, 0.0f, 0.88f);
    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return r < chance;
}

void Enemy::EnterHold(float duration) {
    holdConfigured_ = true;
    currentHoldDuration_ = duration;

    ResetPreAttackPresentationState();
    IssueAttackCue(EnemyAttackCueType::Feint, action_.kind,
                   currentHoldDuration_);
}

bool Enemy::TryBeginChargeWarpFeint(ActionKind kind) {
    if (phase_ == BossPhase::Phase3) {
        return false;
    }

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
    const float unlock = TechniqueUnlock(BossPhase::Phase2);
    if (unlock <= 0.0f) {
        return false;
    }
    float chance = chargeWarpFeintChance_;
    if (phase_ == BossPhase::Phase1) {
        chance *= 0.55f;
    } else if (phase_ == BossPhase::Phase2) {
        chance *= 0.30f;
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
    if (phase_ == BossPhase::Phase3) {
        return false;
    }

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
    const float unlock = TechniqueUnlock(BossPhase::Phase2);
    if (unlock <= 0.0f) {
        return false;
    }
    float chance = directionFeintChance_;
    if (phase_ == BossPhase::Phase1) {
        chance *= 0.50f;
    } else if (phase_ == BossPhase::Phase2) {
        chance *= 0.30f;
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
    IssueAttackCue(EnemyAttackCueType::Feint, nextKind,
                   (std::max)(0.12f, nextChargeTime - stateTimer_));
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
    return false;
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
        chance *= 0.45f;
    }
    if (phase_ != BossPhase::Phase1) {
        chance *= 0.72f;
    }
    if (phase_ == BossPhase::Phase3) {
        chance *= 0.68f;
    }
    chance *= unlock;
    chance = std::clamp(chance, 0.0f, 0.42f);

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
    if (phase_ == BossPhase::Phase3) {
        return TryBeginTripleIaiSlash(chance);
    }

    const float unlock = TechniqueUnlock(BossPhase::Phase2);
    if (unlock <= 0.0f || !IsRangedAttackAvailable()) {
        return false;
    }
    if (lastActionKind_ == ActionKind::Warp) {
        chance *= 0.72f;
    }
    if (phase_ == BossPhase::Phase3) {
        chance += 0.16f;
    }
    chance *= unlock;
    chance = std::clamp(chance, 0.0f, 0.94f);

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
    RegisterRangedAttackCommit();
    BeginAction(ActionKind::Warp, ActionStep::Start);
    return true;
}

void Enemy::BeginDifficultyNineOpeningCutIn(
    const DirectX::XMFLOAT3 &targetPosition) {
    if (difficulty_ < 9.0f || deathFinished_ || isDying_ ||
        phaseTransitionActive_) {
        return;
    }

    hitReactionTimer_ = 0.0f;
    counterRecoilTimer_ = 0.0f;
    playerPos_ = targetPosition;
    FaceTargetImmediately(targetPosition);
    ResetWarpContext();
    warp_.isCutIn = true;
    warp_.farSlashFollowup = true;
    warp_.approachSlot = WarpApproachSlot::Front;
    if (!DecideWarpTargetFarSlash(warp_.targetPos)) {
        ResetWarpContext();
        return;
    }

    warp_.hasValidTarget = true;
    warp_.followupKind = SelectNearPressureAction();
    warp_.followupStep = ActionStep::Charge;
    warp_.faceLivePlayerOnEnd = true;
    BeginAction(ActionKind::Warp, ActionStep::Start);
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

bool Enemy::TryBeginTripleIaiSlash(float chance) {
    const float unlock = TechniqueUnlock(BossPhase::Phase3);
    if (unlock <= 0.0f || tripleIaiSlashCooldown_ > 0.0f ||
        !IsRangedAttackAvailable() || deathFinished_ || isDying_ ||
        phaseTransitionActive_) {
        return false;
    }

    const float distance = GetDistanceToPlayer();
    if (phase_ != BossPhase::Phase3 && distance < 4.2f) {
        return false;
    }

    if (lastActionKind_ == ActionKind::Warp) {
        chance *= 0.68f;
    }
    chance *= unlock;
    chance = std::clamp(chance, 0.0f,
                        phase_ == BossPhase::Phase3 ? 1.0f : 0.84f);

    const float roll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (roll >= chance) {
        return false;
    }

    tripleIaiSlashActive_ = true;
    tripleIaiSlashesRemaining_ = kTripleIaiCloneCount_;
    tripleIaiSlashIndex_ = 0;
    tripleIaiSlashCooldown_ = tripleIaiSlashCooldownDuration_;
    PrepareTripleIaiSlashClones();
    RegisterRangedAttackCommit();
    BeginTripleIaiSlashIntro();
    return true;
}

bool Enemy::TryBeginBladeClash(float chance) {
    // Blade clash is reserved for the phase 2 transition event.
    (void)chance;
    return false;
}

bool Enemy::TryBeginArcaneLaser(float chance) {
    const float unlock = TechniqueUnlock(BossPhase::Phase2);
    if (phase_ == BossPhase::Phase3 ||
        unlock <= 0.0f || arcaneLaserCooldown_ > 0.0f ||
        !IsRangedAttackAvailable() ||
        rangedReengagePending_ ||
        deathFinished_ || isDying_ || phaseTransitionActive_) {
        return false;
    }

    if (lastActionKind_ == ActionKind::ArcaneLaser) {
        chance *= 0.42f;
    } else if (lastActionKind_ == ActionKind::Warp) {
        chance *= 0.82f;
    }
    if (phase_ == BossPhase::Phase3) {
        chance += 0.24f;
    }
    chance *= unlock;
    chance = std::clamp(chance, 0.0f, 0.90f);

    const float roll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (roll >= chance) {
        return false;
    }

    arcaneLaserCooldown_ = arcaneLaserCooldownDuration_;
    rangedReengagePending_ = true;
    ResetWarpContext();
    warp_.isCutIn = true;
    warp_.followupKind = ActionKind::ArcaneLaser;
    warp_.followupStep = ActionStep::Charge;
    warp_.faceLivePlayerOnEnd = true;
    if (!DecideWarpTargetArcaneLaser(warp_.targetPos)) {
        ResetWarpContext();
        return false;
    }
    warp_.hasValidTarget = true;
    RegisterRangedAttackCommit();
    BeginAction(ActionKind::Warp, ActionStep::Start);
    return true;
}

bool Enemy::TryBeginArcaneLaserSlashFollowup(float chance) {
    if (phase_ == BossPhase::Phase3) {
        return TryBeginTripleIaiSlash(chance);
    }

    const float unlock = TechniqueUnlock(BossPhase::Phase2);
    if (unlock <= 0.0f || deathFinished_ || isDying_ ||
        !IsRangedAttackAvailable() ||
        phaseTransitionActive_) {
        return false;
    }

    const float distance = GetDistanceToPlayer();
    if (distance < arcaneLaserSlashMinDistance_) {
        return false;
    }

    if (phase_ == BossPhase::Phase3) {
        chance += 0.14f;
    }
    chance *= unlock;
    chance = std::clamp(chance, 0.0f, 0.82f);

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
    RegisterRangedAttackCommit();
    BeginAction(ActionKind::Warp, ActionStep::Start);
    return true;
}

bool Enemy::TryBeginLaserReengageWarp(float chance) {
    if (deathFinished_ || isDying_ || phaseTransitionActive_) {
        return false;
    }

    chance = std::clamp(chance, 0.0f, 1.0f);
    const float roll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (roll >= chance) {
        return false;
    }

    ResetWarpContext();
    warp_.approachSlot = WarpApproachSlot::Front;
    if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
        ResetWarpContext();
        return false;
    }

    warp_.hasValidTarget = true;
    warp_.followupKind = ActionKind::None;
    warp_.followupStep = ActionStep::None;
    warp_.faceLivePlayerOnEnd = true;
    BeginAction(ActionKind::Warp, ActionStep::Start);
    return true;
}

bool Enemy::TryBeginCataclysmLaser(float chance) {
    const float unlock = TechniqueUnlock(BossPhase::Phase3);
    if (unlock <= 0.0f || cataclysmLaserCooldown_ > 0.0f ||
        !IsRangedAttackAvailable() ||
        rangedReengagePending_ ||
        deathFinished_ || isDying_ || phaseTransitionActive_) {
        return false;
    }

    if (lastActionKind_ == ActionKind::CataclysmLaser) {
        chance *= 0.30f;
    } else if (lastActionKind_ == ActionKind::ArcaneLaser) {
        chance *= 0.58f;
    }
    if (phase_ == BossPhase::Phase3) {
        chance += 0.08f;
    }
    chance *= unlock;
    chance = std::clamp(chance, 0.0f, 0.78f);

    const float roll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (roll >= chance) {
        return false;
    }

    cataclysmLaserCooldown_ = cataclysmLaserCooldownDuration_;
    rangedReengagePending_ = true;
    ResetWarpContext();
    warp_.isCutIn = true;
    warp_.followupKind = ActionKind::CataclysmLaser;
    warp_.followupStep = ActionStep::Charge;
    warp_.faceLivePlayerOnEnd = true;
    if (!DecideWarpTargetCataclysmLaser(warp_.targetPos)) {
        ResetWarpContext();
        return false;
    }
    warp_.hasValidTarget = true;
    RegisterRangedAttackCommit();
    BeginAction(ActionKind::Warp, ActionStep::Start);
    return true;
}

bool Enemy::IsRangedAttackAvailable() const {
    return sharedRangedAttackCooldown_ <= 0.0f &&
           rangedAttackLockoutTimer_ <= 0.0f;
}

void Enemy::RegisterRangedAttackCommit() {
    sharedRangedAttackCooldown_ = sharedRangedAttackCooldownDuration_;
    ++consecutiveRangedAttackCount_;
    if (consecutiveRangedAttackCount_ >= rangedAttackChainLockoutThreshold_) {
        rangedAttackLockoutTimer_ = rangedAttackChainLockoutDuration_;
        consecutiveRangedAttackCount_ = 0;
    }
}

void Enemy::RegisterNonRangedAttackCommit(ActionKind kind) {
    if (kind == ActionKind::Smash || kind == ActionKind::Sweep ||
        kind == ActionKind::BladeClash) {
        consecutiveRangedAttackCount_ = 0;
    }
}

void Enemy::BeginPhantomWarpStep(int viewWarpsRemaining, bool finalBehind,
                                 ActionKind followupKind) {
    ResetWarpContext();
    warp_.phantomChain = true;
    warp_.phantomFinal = finalBehind;
    warp_.phantomViewWarpsRemaining = viewWarpsRemaining;
    warp_.faceLivePlayerOnEnd = true;

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

void Enemy::PrepareTripleIaiSlashClones() {
    ResetTripleIaiSlashClones();

    for (int i = 0; i < kTripleIaiCloneCount_; ++i) {
        tripleIaiSlashOrder_[i] = i;
        DirectX::XMFLOAT3 clonePos{};
        if (!DecideWarpTargetTripleIaiSlash(clonePos, i)) {
            continue;
        }

        EnemyTripleIaiClone &clone = tripleIaiClones_[i];
        clone.visual = visualTf_;
        clone.visual.position = clonePos;
        clone.targetPosition = clonePos;
        clone.slashKind = (i % 2 == 0) ? ActionKind::Smash : ActionKind::Sweep;
        clone.isActive = true;

        if (warp_.hasTargetYaw) {
            DirectX::XMStoreFloat4(
                &clone.visual.rotation,
                DirectX::XMQuaternionRotationRollPitchYaw(0.0f,
                                                          warp_.targetYaw,
                                                          0.0f));
        }
    }
    if (tripleIaiClones_[1].isActive) {
        runtime_.tripleIaiCenterFocusPosition =
            tripleIaiClones_[1].visual.position;
    } else {
        runtime_.tripleIaiCenterFocusPosition = tf_.position;
    }

    for (int i = kTripleIaiCloneCount_ - 1; i > 0; --i) {
        const int j = std::rand() % (i + 1);
        std::swap(tripleIaiSlashOrder_[i], tripleIaiSlashOrder_[j]);
    }

    ResetWarpContext();
}

void Enemy::BeginTripleIaiSlashIntro() {
    tripleIaiIntroActive_ = true;
    tripleIaiIntroTimer_ = 0.0f;
    tripleIaiIntroStartPosition_ = tf_.position;
    tripleIaiIntroLiftPosition_ = tf_.position;
    tripleIaiIntroLiftPosition_.y += tripleIaiIntroLiftHeight_;
    runtime_.tripleIaiCenterFocusPosition = tripleIaiIntroLiftPosition_;
    IssueAttackCue(EnemyAttackCueType::Cancel, ActionKind::None, 0.0f);

    for (int i = 0; i < kTripleIaiCloneCount_; ++i) {
        EnemyTripleIaiClone &clone = tripleIaiClones_[i];
        if (!clone.isActive) {
            continue;
        }

        const float jitterX =
            static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) -
            0.5f;
        const float jitterZ =
            static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) -
            0.5f;
        clone.visual.position = tripleIaiIntroLiftPosition_;
        clone.visual.position.x += jitterX * 0.55f;
        clone.visual.position.z += jitterZ * 0.55f;
        clone.visual.position.y += 0.18f * static_cast<float>(i);
    }
}

void Enemy::UpdateTripleIaiSlashIntro(float deltaTime) {
    tripleIaiIntroTimer_ += deltaTime;

    const float duration = std::max(0.0001f, tripleIaiIntroDuration_);
    const float t = std::clamp(tripleIaiIntroTimer_ / duration, 0.0f, 1.0f);
    const float liftT = std::clamp(t / 0.34f, 0.0f, 1.0f);
    const float liftEase = 1.0f - std::pow(1.0f - liftT, 3.0f);
    const float splitT = std::clamp((t - 0.24f) / 0.62f, 0.0f, 1.0f);

    tf_.position.x =
        tripleIaiIntroStartPosition_.x +
        (tripleIaiIntroLiftPosition_.x - tripleIaiIntroStartPosition_.x) *
            liftEase;
    tf_.position.y =
        tripleIaiIntroStartPosition_.y +
        (tripleIaiIntroLiftPosition_.y - tripleIaiIntroStartPosition_.y) *
            liftEase;
    tf_.position.z =
        tripleIaiIntroStartPosition_.z +
        (tripleIaiIntroLiftPosition_.z - tripleIaiIntroStartPosition_.z) *
            liftEase;

    const float focusT = std::clamp((t - 0.18f) / 0.50f, 0.0f, 1.0f);
    runtime_.tripleIaiCenterFocusPosition = {
        tripleIaiIntroLiftPosition_.x +
            (playerPos_.x - tripleIaiIntroLiftPosition_.x) * focusT,
        tripleIaiIntroLiftPosition_.y +
            (playerPos_.y + 1.55f - tripleIaiIntroLiftPosition_.y) * focusT,
        tripleIaiIntroLiftPosition_.z +
            (playerPos_.z - tripleIaiIntroLiftPosition_.z) * focusT};

    for (int i = 0; i < kTripleIaiCloneCount_; ++i) {
        EnemyTripleIaiClone &clone = tripleIaiClones_[i];
        if (!clone.isActive) {
            continue;
        }

        const float stagger = std::clamp(
            (splitT - static_cast<float>(i) * 0.045f) / 0.82f, 0.0f, 1.0f);
        const float eased = stagger * stagger * (3.0f - 2.0f * stagger);
        clone.visual.position.x =
            tripleIaiIntroLiftPosition_.x +
            (clone.targetPosition.x - tripleIaiIntroLiftPosition_.x) * eased;
        clone.visual.position.y =
            tripleIaiIntroLiftPosition_.y +
            (clone.targetPosition.y - tripleIaiIntroLiftPosition_.y) * eased;
        clone.visual.position.z =
            tripleIaiIntroLiftPosition_.z +
            (clone.targetPosition.z - tripleIaiIntroLiftPosition_.z) * eased;

        if (eased > 0.94f) {
            clone.visual.position = clone.targetPosition;
        }
    }

    UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * 0.70f);
    isVisible_ = true;

    if (tripleIaiIntroTimer_ >= duration) {
        tripleIaiIntroActive_ = false;
        tf_.position = tripleIaiIntroLiftPosition_;
        BeginTripleIaiSlashStep();
    }
}

void Enemy::BeginTripleIaiSlashStep() {
    if (tripleIaiSlashesRemaining_ <= 0) {
        tripleIaiSlashActive_ = false;
        tripleIaiIntroActive_ = false;
        ResetTripleIaiSlashClones();
        BeginChaseAction();
        return;
    }

    const int slashIndex = tripleIaiSlashIndex_;
    const int cloneIndex =
        slashIndex >= 0 && slashIndex < kTripleIaiCloneCount_
            ? tripleIaiSlashOrder_[slashIndex]
            : slashIndex;
    runtime_.tripleIaiReturnCameraToCenter = false;
    ResetWarpContext();
    warp_.isCutIn = true;
    warp_.farSlashFollowup = true;
    warp_.approachSlot = WarpApproachSlot::Front;
    warp_.followupKind =
        (cloneIndex % 2 == 0) ? ActionKind::Smash : ActionKind::Sweep;
    warp_.followupStep = ActionStep::Charge;
    warp_.faceLivePlayerOnEnd = true;
    if (cloneIndex >= 0 && cloneIndex < kTripleIaiCloneCount_ &&
        tripleIaiClones_[cloneIndex].isActive) {
        warp_.targetPos = tripleIaiClones_[cloneIndex].targetPosition;
        runtime_.tripleIaiCenterFocusPosition = warp_.targetPos;
        FinalizeWarpTargetFacing(warp_.targetPos);
        tf_.position = warp_.targetPos;
        if (warp_.hasTargetYaw) {
            facingYaw_ = NormalizeAngle(warp_.targetYaw);
        }
        LockCurrentFacing();
        UpdateParts();
        warp_.departurePos = tf_.position;
        warp_.hasDeparturePos = true;
    } else if (!DecideWarpTargetTripleIaiSlash(warp_.targetPos, cloneIndex)) {
        tripleIaiSlashActive_ = false;
        tripleIaiSlashesRemaining_ = 0;
        tripleIaiSlashIndex_ = 0;
        ResetTripleIaiSlashClones();
        ResetWarpContext();
        BeginChaseAction();
        return;
    } else {
        runtime_.tripleIaiCenterFocusPosition = warp_.targetPos;
    }

    if (cloneIndex >= 0 && cloneIndex < kTripleIaiCloneCount_) {
        tripleIaiClones_[cloneIndex].isActive = false;
    }

    warp_.hasValidTarget = true;
    --tripleIaiSlashesRemaining_;
    ++tripleIaiSlashIndex_;
    BeginAction(ActionKind::Warp, ActionStep::Start);
}

bool Enemy::TryContinueTripleIaiSlash() {
    if (!tripleIaiSlashActive_ || tripleIaiSlashesRemaining_ <= 0 ||
        deathFinished_ || isDying_ || phaseTransitionActive_) {
        return false;
    }

    EndAttack();
    BeginTripleIaiSlashStep();
    return true;
}

void Enemy::ResetTripleIaiSlashClones() {
    for (int i = 0; i < kTripleIaiCloneCount_; ++i) {
        tripleIaiClones_[i] = EnemyTripleIaiClone{};
        tripleIaiSlashOrder_[i] = i;
    }
    tripleIaiIntroActive_ = false;
    tripleIaiIntroTimer_ = 0.0f;
    runtime_.tripleIaiCenterFocusPosition = tf_.position;
    runtime_.tripleIaiReturnCameraToCenter = false;
}

void Enemy::BeginPressureAction() {
    if (!IsPlayerInMeleeFront()) {
        BeginChaseAction();
        return;
    }

    const float distance = GetDistanceToPlayer();
    const bool phase2Unlocked = TechniqueUnlock(BossPhase::Phase2) > 0.0f;
    const bool phase3Unlocked = TechniqueUnlock(BossPhase::Phase3) > 0.0f;
    const bool isPhase3 = phase_ == BossPhase::Phase3;
    const bool canQuickSlash = phase2Unlocked;
    const bool canBladeClash = false;
    const bool canWarp = phase2Unlocked;
    const bool canFarWarpSlash =
        phase2Unlocked && IsRangedAttackAvailable() && !rangedReengagePending_;
    const bool canPhantomWarp =
        phase3Unlocked && phantomWarpCooldown_ <= 0.0f && !deathFinished_ &&
        !isDying_ && !phaseTransitionActive_ && distance >= 1.65f &&
        distance <= 9.8f;
    const bool canTripleIaiSlash =
        phase3Unlocked && tripleIaiSlashCooldown_ <= 0.0f &&
        IsRangedAttackAvailable() &&
        (isPhase3 || distance >= 4.2f) && !deathFinished_ && !isDying_ &&
        !phaseTransitionActive_;
    const bool canArcaneLaser =
        phase2Unlocked && !isPhase3 && arcaneLaserCooldown_ <= 0.0f &&
        IsRangedAttackAvailable() &&
        !rangedReengagePending_;
    const bool canCataclysmLaser =
        phase3Unlocked && cataclysmLaserCooldown_ <= 0.0f &&
        IsRangedAttackAvailable() &&
        !rangedReengagePending_;
    int smashWeight = 55;
    int sweepWeight = 45;
    int quickSlashWeight = 0;
    int bladeClashWeight = 0;
    int warpWeight = 0;
    int farWarpSlashWeight = 0;
    int phantomWarpWeight = 0;
    int tripleIaiSlashWeight = 0;
    int arcaneLaserWeight = 0;
    int cataclysmLaserWeight = 0;

    if (isPhase3) {
        smashWeight = 0;
        sweepWeight = 0;
        quickSlashWeight = 0;
        bladeClashWeight = 0;
        warpWeight = 0;
        farWarpSlashWeight = 24;
        phantomWarpWeight = 24;
        tripleIaiSlashWeight = 22;
        arcaneLaserWeight = 0;
        cataclysmLaserWeight = 20;
    } else if (phase2Unlocked) {
        smashWeight = 5;
        sweepWeight = 5;
        quickSlashWeight = 6;
        bladeClashWeight = 0;
        warpWeight = 28;
        farWarpSlashWeight = 52;
        arcaneLaserWeight = 64;
    }

    const BossDecisionAction selected = PickWeightedAction(
        {{BossDecisionAction::Smash, smashWeight},
         {BossDecisionAction::Sweep, sweepWeight},
         {BossDecisionAction::QuickSlash,
          canQuickSlash ? quickSlashWeight : 0},
         {BossDecisionAction::BladeClash,
          canBladeClash ? bladeClashWeight : 0},
         {BossDecisionAction::Warp, canWarp ? warpWeight : 0},
         {BossDecisionAction::FarWarpSlash,
          canFarWarpSlash ? farWarpSlashWeight : 0},
         {BossDecisionAction::PhantomWarp,
          canPhantomWarp ? phantomWarpWeight : 0},
         {BossDecisionAction::TripleIaiSlash,
          canTripleIaiSlash ? tripleIaiSlashWeight : 0},
         {BossDecisionAction::ArcaneLaser,
          canArcaneLaser ? arcaneLaserWeight : 0},
         {BossDecisionAction::CataclysmLaser,
          canCataclysmLaser ? cataclysmLaserWeight : 0}},
        isPhase3 ? BossDecisionAction::Stalk : BossDecisionAction::Smash);

    auto beginFallback = [&]() {
        if (isPhase3) {
            BeginStalkAction();
            return;
        }
        RegisterNonRangedAttackCommit(ActionKind::Smash);
        BeginAction(SelectNearPressureAction(), ActionStep::Charge);
    };

    switch (selected) {
    case BossDecisionAction::Smash:
        RegisterNonRangedAttackCommit(ActionKind::Smash);
        BeginAction(ActionKind::Smash, ActionStep::Charge);
        return;
    case BossDecisionAction::Sweep:
        RegisterNonRangedAttackCommit(ActionKind::Sweep);
        BeginAction(ActionKind::Sweep, ActionStep::Charge);
        return;
    case BossDecisionAction::QuickSlash:
        RegisterNonRangedAttackCommit(ActionKind::Smash);
        BeginAction(SelectNearPressureAction(), ActionStep::Charge);
        quickSlashActive_ = true;
        warpFeintDecisionMade_ = true;
        directionFeintDecisionMade_ = true;
        return;
    case BossDecisionAction::BladeClash:
        RegisterNonRangedAttackCommit(ActionKind::BladeClash);
        BeginAction(ActionKind::BladeClash, ActionStep::Charge);
        return;
    case BossDecisionAction::Warp:
        if (PrepareWarpContext()) {
            BeginAction(ActionKind::Warp, ActionStep::Start);
            return;
        }
        beginFallback();
        return;
    case BossDecisionAction::FarWarpSlash:
        if (TryBeginFarWarpSlash(1.0f)) {
            return;
        }
        beginFallback();
        return;
    case BossDecisionAction::PhantomWarp:
        BeginPhantomWarpStep(2, false, ActionKind::None);
        phantomWarpCooldown_ = phantomWarpCooldownDuration_;
        return;
    case BossDecisionAction::TripleIaiSlash:
        if (TryBeginTripleIaiSlash(1.0f)) {
            return;
        }
        beginFallback();
        return;
    case BossDecisionAction::ArcaneLaser:
        if (TryBeginArcaneLaser(1.0f)) {
            return;
        }
        beginFallback();
        return;
    case BossDecisionAction::CataclysmLaser:
        if (TryBeginCataclysmLaser(1.0f)) {
            return;
        }
        beginFallback();
        return;
    default:
        beginFallback();
        return;
    }
}

void Enemy::BeginChaseAction() {
    const float distance = GetDistanceToPlayer();

    if (IsPlayerInMeleeFront()) {
        rangedReengagePending_ = false;
        BeginPressureAction();
        return;
    }

    const bool phase2Unlocked = TechniqueUnlock(BossPhase::Phase2) > 0.0f;
    const bool phase3Unlocked = TechniqueUnlock(BossPhase::Phase3) > 0.0f;
    const bool isPhase3 = phase_ == BossPhase::Phase3;
    const bool canWarp = phase2Unlocked;
    const bool canFarWarpSlash =
        phase2Unlocked && distance >= warpCutInDistance_ &&
        IsRangedAttackAvailable() && !rangedReengagePending_;
    const bool canPhantomWarp =
        phase3Unlocked && phantomWarpCooldown_ <= 0.0f && !deathFinished_ &&
        !isDying_ && !phaseTransitionActive_ && distance >= 1.65f &&
        distance <= 9.8f;
    const bool mustReengageAfterRanged =
        rangedReengagePending_ &&
        distance > config_.core.nearAttackDistance + 0.75f;
    const bool canTripleIaiSlash =
        phase3Unlocked && tripleIaiSlashCooldown_ <= 0.0f &&
        IsRangedAttackAvailable() && distance >= 4.2f && !deathFinished_ &&
        !isDying_ && !phaseTransitionActive_ && !mustReengageAfterRanged;
    const bool canArcaneLaser =
        phase2Unlocked && !isPhase3 && arcaneLaserCooldown_ <= 0.0f &&
        IsRangedAttackAvailable() &&
        !mustReengageAfterRanged;
    const bool canCataclysmLaser =
        phase3Unlocked && cataclysmLaserCooldown_ <= 0.0f &&
        IsRangedAttackAvailable() &&
        !mustReengageAfterRanged;
    int stalkWeight = 100;
    int warpWeight = 0;
    int farWarpSlashWeight = 0;
    int phantomWarpWeight = 0;
    int tripleIaiSlashWeight = 0;
    int arcaneLaserWeight = 0;
    int cataclysmLaserWeight = 0;

    if (isPhase3) {
        stalkWeight = 8;
        warpWeight = 0;
        farWarpSlashWeight = 28;
        phantomWarpWeight = 24;
        tripleIaiSlashWeight = 24;
        arcaneLaserWeight = 0;
        cataclysmLaserWeight = 24;
    } else if (phase2Unlocked) {
        stalkWeight = 3;
        warpWeight = 28;
        farWarpSlashWeight = 54;
        arcaneLaserWeight = 72;
    }

    if (mustReengageAfterRanged) {
        stalkWeight = 10;
        warpWeight = phase2Unlocked ? 72 : 0;
        farWarpSlashWeight = 0;
        phantomWarpWeight = 0;
        tripleIaiSlashWeight = 0;
        arcaneLaserWeight = 0;
        cataclysmLaserWeight = 0;
    }

    const BossDecisionAction selected = PickWeightedAction(
        {{BossDecisionAction::Stalk, stalkWeight},
         {BossDecisionAction::Warp, canWarp ? warpWeight : 0},
         {BossDecisionAction::FarWarpSlash,
          canFarWarpSlash ? farWarpSlashWeight : 0},
         {BossDecisionAction::PhantomWarp,
          canPhantomWarp ? phantomWarpWeight : 0},
         {BossDecisionAction::TripleIaiSlash,
          canTripleIaiSlash ? tripleIaiSlashWeight : 0},
         {BossDecisionAction::ArcaneLaser,
          canArcaneLaser ? arcaneLaserWeight : 0},
         {BossDecisionAction::CataclysmLaser,
          canCataclysmLaser ? cataclysmLaserWeight : 0}},
        BossDecisionAction::Stalk);

    switch (selected) {
    case BossDecisionAction::Warp:
        if (PrepareWarpContext()) {
            BeginAction(ActionKind::Warp, ActionStep::Start);
            return;
        }
        BeginStalkAction();
        return;
    case BossDecisionAction::FarWarpSlash:
        if (TryBeginFarWarpSlash(1.0f)) {
            return;
        }
        BeginStalkAction();
        return;
    case BossDecisionAction::PhantomWarp:
        BeginPhantomWarpStep(2, false, ActionKind::None);
        phantomWarpCooldown_ = phantomWarpCooldownDuration_;
        return;
    case BossDecisionAction::TripleIaiSlash:
        if (TryBeginTripleIaiSlash(1.0f)) {
            return;
        }
        BeginStalkAction();
        return;
    case BossDecisionAction::ArcaneLaser:
        if (TryBeginArcaneLaser(1.0f)) {
            return;
        }
        BeginStalkAction();
        return;
    case BossDecisionAction::CataclysmLaser:
        if (TryBeginCataclysmLaser(1.0f)) {
            return;
        }
        BeginStalkAction();
        return;
    case BossDecisionAction::Stalk:
    default:
        BeginStalkAction();
        return;
    }
}

