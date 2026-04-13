#include "Enemy.h"
#include "ModelManager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

// ============================================================
// 読み合い補助
// ============================================================
CounterReadAxis Enemy::GetCounterReadAxis(ActionKind kind) const {
    switch (kind) {
    case ActionKind::Smash:
        return CounterReadAxis::Vertical;
    case ActionKind::Sweep:
        return CounterReadAxis::Horizontal;
    case ActionKind::Rush:
        return CounterReadAxis::ThrustLike;
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
    float guardChance = 0.0f;
    float rushChance = 0.0f;

    if (kind == ActionKind::Smash) {
        warpChance = smashHoldBranchWarpChance_;
        guardChance = smashHoldBranchGuardChance_;
        rushChance = smashHoldBranchRushChance_;
    } else if (kind == ActionKind::Sweep) {
        warpChance = sweepHoldBranchWarpChance_;
        guardChance = sweepHoldBranchGuardChance_;
        rushChance = sweepHoldBranchRushChance_;
    } else {
        return;
    }

    if (playerObs_.isCounterStance) {
        warpChance += 0.10f;
        guardChance += 0.06f;
        rushChance += 0.08f;
    }

    if (playerObs_.justCounterEarly || counterMemory_.earlyCount > 0.6f) {
        warpChance += 0.08f;
        guardChance += 0.05f;
    }

    if (postCounterRhythmTimer_ > 0.0f) {
        warpChance += 0.10f;
        rushChance += 0.06f;
    }

    float distance = GetDistanceToPlayer();
    if (distance <= nearAttackDistance_ * 0.8f) {
        guardChance += 0.05f;
        rushChance += 0.05f;
    }

    float totalSpecial = warpChance + guardChance + rushChance;
    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);

    if (totalSpecial <= 0.0f || r >= totalSpecial) {
        holdBranchType_ = HoldBranchType::Active;
        return;
    }

    float pick = static_cast<float>(std::rand()) /
                 static_cast<float>(RAND_MAX) * totalSpecial;

    if (pick < warpChance) {
        holdBranchType_ = HoldBranchType::Warp;
    } else if (pick < warpChance + guardChance) {
        holdBranchType_ = HoldBranchType::Guard;
    } else {
        holdBranchType_ = HoldBranchType::Rush;
    }
}

bool Enemy::TryExecuteHoldBranch(ActionKind kind) {
    (void)kind;
    if (!holdBranchDecided_) {
        return false;
    }

    switch (holdBranchType_) {
    case HoldBranchType::Warp:
        if (PrepareWarpContext()) {
            BeginAction(ActionKind::Warp, ActionStep::Start);
            return true;
        }
        holdBranchType_ = HoldBranchType::Active;
        return false;

    case HoldBranchType::Guard:
        DecideGuardTarget();
        BeginAction(ActionKind::Guard, ActionStep::Move);
        return true;

    case HoldBranchType::Rush:
        BeginAction(ActionKind::Rush, ActionStep::Charge);
        return true;

    case HoldBranchType::Active:
    case HoldBranchType::None:
    default:
        return false;
    }
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

    if (chance < 0.0f) {
        chance = 0.0f;
    }
    if (chance > 0.95f) {
        chance = 0.95f;
    }

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
    isMargitComboBTransition_ = false;
}

bool Enemy::TryBranchFromRecovery(ActionKind finishedKind) {
    ResetRecoveryBranchState();

    if (!(finishedKind == ActionKind::Smash ||
          finishedKind == ActionKind::Sweep ||
          finishedKind == ActionKind::Rush ||
          finishedKind == ActionKind::Shot)) {
        return false;
    }

    if (finishedKind == ActionKind::Shot) {
        float distance = GetDistanceToPlayer();
        const bool canShotRush =
            (distance >= shotRushMinDistance_ && distance <= shotRushMaxDistance_);
        const bool canShotWarp =
            (distance >= shotWarpMinDistance_ && distance <= shotWarpMaxDistance_);

        if (canShotRush || canShotWarp) {
            float shotRushChance = canShotRush ? shotRushFollowupChance_ : 0.0f;
            float shotWarpChance = canShotWarp ? shotWarpFollowupChance_ : 0.0f;

            if (phase_ == BossPhase::Phase2) {
                shotRushChance += phase2ShotRushFollowupBonus_;
                shotWarpChance += phase2ShotWarpFollowupBonus_;
            }
            if (playerObs_.isGuarding) {
                shotRushChance += 0.10f;
                shotWarpChance += 0.06f;
            }
            if (playerObs_.isCounterStance) {
                shotRushChance -= 0.08f;
                shotWarpChance += 0.08f;
            }

            if (shotRushChance < 0.0f) {
                shotRushChance = 0.0f;
            }
            if (shotRushChance > 0.90f) {
                shotRushChance = 0.90f;
            }
            if (shotWarpChance < 0.0f) {
                shotWarpChance = 0.0f;
            }
            if (shotWarpChance > 0.80f) {
                shotWarpChance = 0.80f;
            }

            float totalFollowupChance = shotRushChance + shotWarpChance;
            if (totalFollowupChance > 0.95f) {
                float scale = 0.95f / totalFollowupChance;
                shotRushChance *= scale;
                shotWarpChance *= scale;
                totalFollowupChance = 0.95f;
            }

            float roll =
                static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
            if (roll < shotRushChance) {
                recoveryBranchType_ = RecoveryBranchType::Recommit;
                recoveryFollowupKind_ = ActionKind::Rush;
                recoveryFollowupStep_ = ActionStep::Charge;
                recoveryFollowupDelayTimer_ =
                    RandomRange(shotRushFollowupDelayMin_,
                                shotRushFollowupDelayMax_);
                isMargitComboBTransition_ = true;
                return true;
            }

            if (roll < shotRushChance + shotWarpChance) {
                ResetWarpContext();
                warp_.type = WarpType::Approach;

                if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
                    ResetWarpContext();
                    return false;
                }

                warp_.hasValidTarget = true;
                DecideWarpFollowupFromContext();
                SetupChainFromWarpContext();

                recoveryBranchType_ = RecoveryBranchType::Recommit;
                recoveryFollowupKind_ = ActionKind::Warp;
                recoveryFollowupStep_ = ActionStep::Start;
                recoveryFollowupDelayTimer_ =
                    RandomRange(shotWarpFollowupDelayMin_,
                                shotWarpFollowupDelayMax_);
                return true;
            }
        }

        return false;
    }

    const bool shouldForceMargitComboA =
        (phase_ == BossPhase::Phase2 && finishedKind == ActionKind::Smash &&
         action_.id == ActionId::DelaySmash &&
         (currentActionConnected_ || currentActionGuarded_));

    if (shouldForceMargitComboA) {
        recoveryBranchType_ = RecoveryBranchType::Recommit;
        recoveryFollowupKind_ = ActionKind::Sweep;
        recoveryFollowupStep_ = ActionStep::Charge;
        recoveryFollowupDelayTimer_ = RandomRange(margitComboFollowupDelayMin_,
                                                  margitComboFollowupDelayMax_);
        isMargitComboATransition_ = true;
        return true;
    }

    float recommitChance = recommitChance_;
    float delayedSecondChance = delayedSecondChance_;
    float fakeoutChance = escapeFakeoutChance_;

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

        if (finishedKind == ActionKind::Smash) {
            recoveryFollowupKind_ = ActionKind::Sweep;
        } else if (finishedKind == ActionKind::Sweep) {
            recoveryFollowupKind_ = ActionKind::Smash;
        } else {
            recoveryFollowupKind_ =
                (std::rand() % 2 == 0) ? ActionKind::Smash : ActionKind::Sweep;
        }

        recoveryFollowupStep_ = ActionStep::Charge;
        recoveryFollowupDelayTimer_ =
            RandomRange(recommitDelayMin_, recommitDelayMax_);
        return true;
    }

    if (pick < recommitChance + delayedSecondChance) {
        recoveryBranchType_ = RecoveryBranchType::DelayedSecond;

        if (finishedKind == ActionKind::Sweep) {
            recoveryFollowupKind_ = ActionKind::Sweep;
        } else if (finishedKind == ActionKind::Smash) {
            recoveryFollowupKind_ = ActionKind::Smash;
        } else {
            recoveryFollowupKind_ = ActionKind::Rush;
        }

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

    if (std::rand() % 2 == 0) {
        warp_.followupKind = ActionKind::Shot;
        warp_.followupStep = ActionStep::Charge;
    } else {
        warp_.followupKind = ActionKind::Wave;
        warp_.followupStep = ActionStep::Charge;
    }

    BeginAction(ActionKind::Warp, ActionStep::Start);
    return true;
}

// ============================================================
// マルギット風 学習・適応
// ============================================================
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
        chance = smashFeintChance_;
        if (action_.id == ActionId::DelaySmash) {
            chance += 0.20f;
        }
    } else if (kind == ActionKind::Sweep) {
        chance = sweepFeintChance_;
    }

    chance += counterMemory_.counterStancePressure * 0.12f;
    chance += counterMemory_.successCount * 0.08f;
    chance += counterMemory_.earlyCount * 0.10f;

    if (tactic_ == TacticState::CounterBait) {
        chance += 0.15f;
    }

    if (chance < 0.0f) {
        chance = 0.0f;
    }
    if (chance > 0.95f) {
        chance = 0.95f;
    }

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

    if (offset > 0.28f) {
        offset = 0.28f;
    }
    if (offset < -0.08f) {
        offset = -0.08f;
    }

    return offset;
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
