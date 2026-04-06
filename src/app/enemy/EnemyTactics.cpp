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
    (void)deltaTime;

    if (stateTimer_ < 0.35f) {
        return;
    }

    if (recoveryFollowupKind_ != ActionKind::None &&
        recoveryFollowupStep_ != ActionStep::None) {
        ActionKind nextKind = recoveryFollowupKind_;
        ActionStep nextStep = recoveryFollowupStep_;

        ResetRecoveryBranchState();
        BeginAction(nextKind, nextStep);
        return;
    }

    tactic_ = DecideTactic();
    BeginActionFromTactic(tactic_);
}

TacticState Enemy::DecideTactic() const {
    float distance = GetDistanceToPlayer();

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

    if (distance <= nearAttackDistance_) {
        int smashWeight = nearSmashWeight_;
        int sweepWeight = nearSweepWeight_;
        int guardWeight = nearGuardWeight_;
        int rushWeight = nearRushWeight_;

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

        int total = smashWeight + sweepWeight + guardWeight + rushWeight;
        if (total <= 0) {
            total = 1;
        }

        int r = std::rand() % total;

        if (r < smashWeight) {
            BeginAction(ActionKind::Smash, ActionStep::Charge);
        } else if (r < smashWeight + sweepWeight) {
            BeginAction(ActionKind::Sweep, ActionStep::Charge);
        } else if (r < smashWeight + sweepWeight + guardWeight) {
            DecideGuardTarget();
            BeginAction(ActionKind::Guard, ActionStep::Move);
        } else {
            BeginAction(ActionKind::Rush, ActionStep::Charge);
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

    int guardWeight = counterBaitGuardWeight_;
    int feintMeleeBonus = 0;

    if (counterMemory_.counterStancePressure > 0.8f) {
        feintMeleeBonus += 10;
    }
    if (counterMemory_.successCount > 0.8f) {
        feintMeleeBonus += 10;
    }

    ActionKind baitKind = DecideAdaptiveCounterBaitAction();

    int baitWeight = 20 + feintMeleeBonus;
    int total = baitWeight + guardWeight;
    if (total <= 0) {
        total = 1;
    }

    int r = std::rand() % total;

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
    int shotWeight = farShotWeight_;
    int warpWeight = farWarpWeight_;
    int waveWeight = farWaveWeight_;

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