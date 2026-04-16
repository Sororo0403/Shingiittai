#include "Enemy.h"
#include "ModelManager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

// ============================================================
// Idle更新
// ============================================================
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

        ActionKind nextKind = recoveryFollowupKind_;
        ActionStep nextStep = recoveryFollowupStep_;
        if (nextKind == ActionKind::Warp && IsWarpSuspendedForPresentation()) {
            nextKind = ActionKind::Rush;
            nextStep = ActionStep::Charge;
        }
        bool startRushFromShotCombo =
            (nextKind == ActionKind::Rush && isMargitComboBTransition_);

        ResetRecoveryBranchState();
        rushFromShotCombo_ = startRushFromShotCombo;
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
    float distance = GetDistanceToPlayer();
    float phase2PressureDistanceMax =
        farAttackDistance_ + phase2PressureMaxDistanceBonus_;

    if (forceEscapeWarpNext_) {
        return TacticState::Reset;
    }

    if (IsCounterFailObserved()) {
        return TacticState::CounterPunish;
    }

    if (counterMemory_.successCount >= 1.6f && distance <= farAttackDistance_) {
        return TacticState::CounterBait;
    }

    if ((playerObs_.isCounterStance ||
         counterMemory_.counterStancePressure >= 0.8f) &&
        distance <= farAttackDistance_) {
        return TacticState::CounterBait;
    }

    if (playerObs_.isGuarding) {
        return TacticState::AntiGuard;
    }

    if (phase_ == BossPhase::Phase2 && distance > nearAttackDistance_ &&
        distance <= phase2PressureDistanceMax) {
        if (lastActionKind_ == ActionKind::Shot ||
            lastActionKind_ == ActionKind::Warp) {
            return TacticState::Pressure;
        }

        float roll =
            static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        if (roll < phase2MidPressureTacticChance_) {
            return TacticState::Pressure;
        }
    }

    if (distance <= nearAttackDistance_) {
        return TacticState::Pressure;
    }

    if (distance > farAttackDistance_) {
        return TacticState::Chase;
    }

    return TacticState::Neutral;
}

void Enemy::BeginActionFromTactic(TacticState tactic) {
    switch (tactic) {
    case TacticState::Pressure:
        BeginPressureAction();
        break;
    case TacticState::CounterBait:
        BeginCounterBaitAction();
        break;
    case TacticState::CounterPunish:
        BeginCounterPunishAction();
        break;
    case TacticState::AntiGuard:
        BeginAntiGuardAction();
        break;
    case TacticState::Chase:
        BeginChaseAction();
        break;
    case TacticState::Reset:
        BeginResetAction();
        break;
    case TacticState::Neutral:
    default:
        BeginPressureAction();
        break;
    }
}

void Enemy::BeginPressureAction() {
    float distance = GetDistanceToPlayer();
    float phase2PressureDistanceMax =
        farAttackDistance_ + phase2PressureMaxDistanceBonus_;

    if (distance <= nearAttackDistance_) {
        float stalkChance = stalkNearEnterChance_;

        if (playerObs_.isCounterStance) {
            stalkChance += 0.10f;
        }
        if (postCounterRhythmTimer_ > 0.0f) {
            stalkChance += 0.12f;
        }
        if (lastActionKind_ == ActionKind::Stalk) {
            stalkChance *= 0.45f;
        }
        if (stalkRepeatCount_ >= stalkRepeatLimit_) {
            stalkChance = 0.0f;
        }

        float stalkRoll =
            static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        if (stalkRoll < stalkChance) {
            BeginStalkAction();
            return;
        }

        int smashWeight = nearSmashWeight_;
        int sweepWeight = nearSweepWeight_;
        int guardWeight = nearGuardWeight_;
        int rushWeight = nearRushWeight_;

        if (phase_ == BossPhase::Phase2) {
            smashWeight += phase2NearSmashBonus_;
            sweepWeight += phase2NearSweepBonus_;
            guardWeight -= phase2NearGuardPenalty_;
            rushWeight += phase2NearRushBonus_;
        }

        if (postCounterRhythmTimer_ > 0.0f) {
            smashWeight = static_cast<int>(smashWeight * 0.7f);
            sweepWeight = static_cast<int>(sweepWeight * 0.7f);
            guardWeight += 5;
            rushWeight += 8;
        }

        if (lastActionKind_ == ActionKind::Smash) {
            smashWeight /= 2;
        } else if (lastActionKind_ == ActionKind::Sweep) {
            sweepWeight /= 2;
        } else if (lastActionKind_ == ActionKind::Guard) {
            guardWeight /= 2;
        } else if (lastActionKind_ == ActionKind::Rush) {
            rushWeight /= 2;
        }

        if (guardWeight < 0) {
            guardWeight = 0;
        }

        int total = smashWeight + sweepWeight + guardWeight + rushWeight;
        if (total <= 0) {
            total = 1;
        }

        int r = std::rand() % total;

        if (r < smashWeight) {
            stalkRepeatCount_ = 0;
            BeginAction(ActionKind::Smash, ActionStep::Charge);
        } else if (r < smashWeight + sweepWeight) {
            stalkRepeatCount_ = 0;
            BeginAction(ActionKind::Sweep, ActionStep::Charge);
        } else if (r < smashWeight + sweepWeight + guardWeight) {
            stalkRepeatCount_ = 0;
            DecideGuardTarget();
            BeginAction(ActionKind::Guard, ActionStep::Move);
        } else {
            stalkRepeatCount_ = 0;
            BeginAction(ActionKind::Rush, ActionStep::Charge);
        }
        return;
    }

    if (phase_ == BossPhase::Phase2 && distance <= phase2PressureDistanceMax) {
        int rushWeight = midRushWeight_ + phase2MidRushBonus_;
        int shotWeight = midShotWeight_ + phase2MidShotBonus_ + 6;
        int warpWeight = farWarpWeight_ + phase2FarWarpBonus_ + 10;
        int waveWeight = (std::max)(8, midWaveWeight_ / 2);

        if (lastActionKind_ == ActionKind::Shot) {
            shotWeight /= 2;
            rushWeight += 10;
            warpWeight += 12;
        } else if (lastActionKind_ == ActionKind::Warp) {
            warpWeight /= 2;
            rushWeight += 8;
            shotWeight += 8;
        } else if (lastActionKind_ == ActionKind::Rush) {
            rushWeight /= 2;
            shotWeight += 10;
            warpWeight += 8;
        }

        if (playerObs_.isCounterStance) {
            shotWeight += 6;
            warpWeight += 8;
        }

        int total = rushWeight + shotWeight + warpWeight + waveWeight;
        if (total <= 0) {
            total = 1;
        }

        int r = std::rand() % total;
        stalkRepeatCount_ = 0;

        if (r < shotWeight) {
            BeginAction(ActionKind::Shot, ActionStep::Charge);
        } else if (r < shotWeight + warpWeight) {
            if (PrepareWarpContext()) {
                BeginAction(ActionKind::Warp, ActionStep::Start);
            } else {
                BeginAction(ActionKind::Rush, ActionStep::Charge);
            }
        } else if (r < shotWeight + warpWeight + rushWeight) {
            BeginAction(ActionKind::Rush, ActionStep::Charge);
        } else {
            BeginAction(ActionKind::Wave, ActionStep::Charge);
        }
        return;
    }

    BeginChaseAction();
}

void Enemy::BeginCounterBaitAction() {
    float distance = GetDistanceToPlayer();

    if (distance > farAttackDistance_) {
        BeginChaseAction();
        return;
    }

    float stalkChance = stalkMidEnterChance_;
    if (counterMemory_.counterStancePressure > 0.8f) {
        stalkChance += 0.12f;
    }
    if (lastActionKind_ == ActionKind::Stalk) {
        stalkChance *= 0.45f;
    }
    if (stalkRepeatCount_ >= stalkRepeatLimit_) {
        stalkChance = 0.0f;
    }

    float stalkRoll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (stalkRoll < stalkChance) {
        BeginStalkAction();
        return;
    }

    int guardWeight = counterBaitGuardWeight_;
    int feintMeleeBonus = 0;

    if (counterMemory_.counterStancePressure > 0.8f) {
        feintMeleeBonus += 10;
    }
    if (counterMemory_.successCount > 0.8f) {
        feintMeleeBonus += 10;
    }
    if (phase_ == BossPhase::Phase2) {
        feintMeleeBonus += phase2CounterBaitMeleeBonus_;
        guardWeight = (std::max)(0, guardWeight - 6);
    }

    ActionKind baitKind = DecideAdaptiveCounterBaitAction();

    int baitWeight = 20 + feintMeleeBonus;
    int total = baitWeight + guardWeight;
    if (total <= 0) {
        total = 1;
    }

    int r = std::rand() % total;

    stalkRepeatCount_ = 0;

    if (r < baitWeight) {
        BeginAction(baitKind, ActionStep::Charge);
    } else {
        DecideGuardTarget();
        BeginAction(ActionKind::Guard, ActionStep::Move);
    }
}

void Enemy::BeginCounterPunishAction() {
    float distance = GetDistanceToPlayer();

    if (distance > farAttackDistance_) {
        if (PrepareWarpContext()) {
            BeginAction(ActionKind::Warp, ActionStep::Start);
        } else {
            BeginAction(ActionKind::Rush, ActionStep::Charge);
        }
        return;
    }

    int smashWeight = counterPunishSmashWeight_;
    int sweepWeight = counterPunishSweepWeight_;
    int rushWeight = counterPunishRushWeight_;

    if (phase_ == BossPhase::Phase2) {
        smashWeight += phase2CounterPunishSmashBonus_;
        sweepWeight += phase2CounterPunishSweepBonus_;
        rushWeight -= 8;
    }

    if (distance <= nearAttackDistance_) {
        smashWeight += 10;
        sweepWeight += 5;
    } else {
        rushWeight += 15;
    }

    int total = smashWeight + sweepWeight + rushWeight;
    if (total <= 0) {
        total = 1;
    }

    int r = std::rand() % total;

    if (r < smashWeight) {
        BeginAction(ActionKind::Smash, ActionStep::Charge);
    } else if (r < smashWeight + sweepWeight) {
        BeginAction(ActionKind::Sweep, ActionStep::Charge);
    } else {
        BeginAction(ActionKind::Rush, ActionStep::Charge);
    }
}

void Enemy::BeginAntiGuardAction() {
    float distance = GetDistanceToPlayer();

    if (distance <= nearAttackDistance_) {
        int rushWeight = midRushWeight_ + antiGuardRushBonus_;
        int waveWeight = midWaveWeight_ + antiGuardWaveBonus_;
        int shotWeight = midShotWeight_ + antiGuardShotBonus_;

        if (phase_ == BossPhase::Phase2) {
            rushWeight += phase2MidRushBonus_;
            shotWeight += phase2MidShotBonus_;
        }

        int total = rushWeight + waveWeight + shotWeight;
        if (total <= 0) {
            total = 1;
        }

        int r = std::rand() % total;

        if (r < rushWeight) {
            BeginAction(ActionKind::Rush, ActionStep::Charge);
        } else if (r < rushWeight + waveWeight) {
            BeginAction(ActionKind::Wave, ActionStep::Charge);
        } else {
            BeginAction(ActionKind::Shot, ActionStep::Charge);
        }
        return;
    }

    int shotWeight = farShotWeight_ + antiGuardShotBonus_;
    int waveWeight = farWaveWeight_ + antiGuardWaveBonus_;
    int warpWeight = farWarpWeight_;

    if (phase_ == BossPhase::Phase2) {
        shotWeight += phase2MidShotBonus_;
        warpWeight += phase2FarWarpBonus_;
    }

    int total = shotWeight + waveWeight + warpWeight;
    if (total <= 0) {
        total = 1;
    }

    int r = std::rand() % total;
    if (r < shotWeight) {
        BeginAction(ActionKind::Shot, ActionStep::Charge);
    } else if (r < shotWeight + waveWeight) {
        BeginAction(ActionKind::Wave, ActionStep::Charge);
    } else {
        if (PrepareWarpContext()) {
            BeginAction(ActionKind::Warp, ActionStep::Start);
        } else {
            BeginAction(ActionKind::Wave, ActionStep::Charge);
        }
    }
}

void Enemy::BeginChaseAction() {
    float distance = GetDistanceToPlayer();

    if (distance <= farAttackDistance_) {
        float stalkChance = stalkMidEnterChance_;
        if (lastActionKind_ == ActionKind::Stalk) {
            stalkChance *= 0.40f;
        }
        if (stalkRepeatCount_ >= stalkRepeatLimit_) {
            stalkChance = 0.0f;
        }

        float stalkRoll =
            static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        if (stalkRoll < stalkChance) {
            BeginStalkAction();
            return;
        }
    }

    int shotWeight = farShotWeight_;
    int warpWeight = farWarpWeight_;
    int waveWeight = farWaveWeight_;

    if (phase_ == BossPhase::Phase2) {
        shotWeight += phase2MidShotBonus_;
        warpWeight += phase2FarWarpBonus_;
    }

    if (isDistanceStagnant_) {
        warpWeight += stagnantWarpBonus_;
    }

    if (lastActionKind_ == ActionKind::Shot) {
        shotWeight /= 2;
    } else if (lastActionKind_ == ActionKind::Warp) {
        warpWeight /= 2;
    } else if (lastActionKind_ == ActionKind::Wave) {
        waveWeight /= 2;
    }

    if (playerGuarding_) {
        waveWeight += 10;
    }

    int total = shotWeight + warpWeight + waveWeight;
    if (total <= 0) {
        total = 1;
    }

    int r = std::rand() % total;
    stalkRepeatCount_ = 0;

    if (r < shotWeight) {
        BeginAction(ActionKind::Shot, ActionStep::Charge);

    } else if (r < shotWeight + warpWeight) {
        if (PrepareWarpContext()) {
            BeginAction(ActionKind::Warp, ActionStep::Start);
        } else {
            BeginAction(ActionKind::Wave, ActionStep::Charge);
        }

    } else {
        BeginAction(ActionKind::Wave, ActionStep::Charge);
    }
}

void Enemy::BeginResetAction() {
    if (IsWarpSuspendedForPresentation()) {
        BeginAction(ActionKind::Guard, ActionStep::Move);
        return;
    }

    if (forceEscapeWarpNext_) {
        ResetWarpContext();
        warp_.type = WarpType::Escape;

        if (DecideWarpTargetFarFromPlayer(warp_.targetPos)) {
            warp_.hasValidTarget = true;
            warp_.followupKind = ActionKind::Shot;
            warp_.followupStep = ActionStep::Charge;
            BeginAction(ActionKind::Warp, ActionStep::Start);
            return;
        }
    }

    if (PrepareWarpContext()) {
        BeginAction(ActionKind::Warp, ActionStep::Start);
    } else {
        BeginAction(ActionKind::Guard, ActionStep::Move);
    }
}

void Enemy::UpdateStalkByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Move:
        UpdateStalkMove(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

void Enemy::UpdateStalkMove(float deltaTime) {
    UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_ * 1.15f);

    float usedYaw = facingYaw_;

    float rightX = std::cosf(usedYaw);
    float rightZ = -std::sinf(usedYaw);

    float forwardX = std::sinf(usedYaw);
    float forwardZ = std::cosf(usedYaw);

    float moveX = 0.0f;
    float moveZ = 0.0f;

    moveX += rightX * stalkMoveDir_ * stalkStrafeRadiusWeight_;
    moveZ += rightZ * stalkMoveDir_ * stalkStrafeRadiusWeight_;

    moveX += forwardX * stalkForwardBias_ * stalkForwardAdjustWeight_;
    moveZ += forwardZ * stalkForwardBias_ * stalkForwardAdjustWeight_;

    float len = std::sqrtf(moveX * moveX + moveZ * moveZ);
    if (len > 0.0001f) {
        moveX /= len;
        moveZ /= len;
    }

    tf_.position.x += moveX * stalkMoveSpeed_ * deltaTime;
    tf_.position.z += moveZ * stalkMoveSpeed_ * deltaTime;

    if (stateTimer_ >= currentHoldDuration_) {
        EndAttack();
    }
}

void Enemy::BeginStalkAction() {
    EnterHold(RandomRange(stalkDurationMin_, stalkDurationMax_));
    BeginAction(ActionKind::Stalk, ActionStep::Move);
    stalkRepeatCount_++;
}
