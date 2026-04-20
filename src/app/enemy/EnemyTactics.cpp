#include "Enemy.h"
#include "ModelManager.h"
#include "imgui.h"

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

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}
} // namespace

// ============================================================
// Idle更新
// ============================================================
void Enemy::UpdateIdle(float deltaTime) {
    if (runtime_.recoveryFollowupKind != ActionKind::None &&
        runtime_.recoveryFollowupStep != ActionStep::None) {
        if (runtime_.recoveryFollowupDelayTimer > 0.0f) {
            runtime_.recoveryFollowupDelayTimer -= deltaTime;
            if (runtime_.recoveryFollowupDelayTimer > 0.0f) {
                return;
            }
            runtime_.recoveryFollowupDelayTimer = 0.0f;
        }

        ActionKind nextKind = runtime_.recoveryFollowupKind;
        ActionStep nextStep = runtime_.recoveryFollowupStep;
        if (nextKind == ActionKind::Warp && IsWarpSuspendedForPresentation()) {
            nextKind = ActionKind::Rush;
            nextStep = ActionStep::Charge;
        }
        bool startRushFromShotCombo =
            (nextKind == ActionKind::Rush &&
             runtime_.isMargitComboBTransition);

        ResetRecoveryBranchState();
        runtime_.rushFromShotCombo = startRushFromShotCombo;
        BeginAction(nextKind, nextStep);
        return;
    }

    if (runtime_.stateTimer < 0.35f) {
        return;
    }

    // Idle is the tactic-selection layer: choose the next action here, then
    // let the action FSM advance its own Charge/Active/Recovery steps.
    runtime_.tactic = DecideTactic();
    BeginActionFromTactic(runtime_.tactic);
}

TacticState Enemy::DecideTactic() const {
    float distance = GetDistanceToPlayer();
    float phase2PressureDistanceMax =
        config_.core.farAttackDistance + phase2PressureMaxDistanceBonus_;

    const float nearFactor = Clamp01(
        (config_.core.nearAttackDistance + 1.2f - distance) /
        (config_.core.nearAttackDistance + 1.2f));
    const float farFactor = Clamp01(
        (distance - config_.core.farAttackDistance + 0.75f) / 2.5f);
    const float midFactor =
        Clamp01(1.0f - (std::max)(nearFactor * 0.92f, farFactor));
    const float pressureFactor =
        Clamp01(closePressureTimer_ / closePressureTimeThreshold_);
    const float stagnationFactor =
        isDistanceStagnant_
            ? 1.0f
            : Clamp01(stagnantTimer_ / stagnantTimeThreshold_);
    const float counterThreat = Clamp01(
        (playerObs_.isCounterStance ? 0.45f : 0.0f) +
        counterMemory_.counterStancePressure * 0.55f +
        counterMemory_.successCount * 0.32f);
    const float guardFactor = playerObs_.isGuarding ? 1.0f : 0.0f;
    const float punishFactor = IsCounterFailObserved() ? 1.0f : 0.0f;
    const float attackFactor = playerObs_.isAttacking ? 1.0f : 0.0f;
    const bool isPhase2 = (runtime_.phase == BossPhase::Phase2);

    int pressureScore = 10 + static_cast<int>(nearFactor * 38.0f) +
                        static_cast<int>(pressureFactor * 18.0f);
    if (isPhase2 && distance <= phase2PressureDistanceMax) {
        pressureScore += 8;
    }
    if (lastActionKind_ == ActionKind::Shot || lastActionKind_ == ActionKind::Wave ||
        lastActionKind_ == ActionKind::Warp) {
        pressureScore += 10;
    }
    pressureScore -= static_cast<int>(counterThreat * 16.0f);
    pressureScore -= static_cast<int>(guardFactor * 8.0f);
    if (distance > phase2PressureDistanceMax) {
        pressureScore -= 16;
    }

    int counterBaitScore =
        2 + static_cast<int>(counterThreat * 34.0f) +
        static_cast<int>(midFactor * 12.0f) +
        static_cast<int>(nearFactor * 10.0f);
    if (forceCounterBaitNext_) {
        counterBaitScore += 18;
    }
    if (postCounterRhythmTimer_ > 0.0f) {
        counterBaitScore += 12;
    }
    if (guardFactor > 0.0f) {
        counterBaitScore -= 6;
    }
    if (distance > config_.core.farAttackDistance + 1.0f) {
        counterBaitScore -= 12;
    }

    int counterPunishScore =
        static_cast<int>(punishFactor * 56.0f) +
        static_cast<int>(attackFactor * 12.0f) +
        static_cast<int>(nearFactor * 8.0f) +
        static_cast<int>(midFactor * 6.0f);
    if (distance > config_.core.farAttackDistance) {
        counterPunishScore -= 12;
    }

    int antiGuardScore = static_cast<int>(guardFactor * 30.0f) +
                         static_cast<int>(midFactor * guardFactor * 10.0f) +
                         static_cast<int>(nearFactor * guardFactor * 8.0f);
    if (isPhase2) {
        antiGuardScore += 4;
    }

    int chaseScore = 6 + static_cast<int>(farFactor * 42.0f) +
                     static_cast<int>(stagnationFactor * 18.0f);
    if (distance > config_.core.farAttackDistance) {
        chaseScore += 8;
    }
    if (lastActionKind_ == ActionKind::Smash || lastActionKind_ == ActionKind::Sweep) {
        chaseScore += 6;
    }

    int resetScore = 0;
    if (forceEscapeWarpNext_) {
        resetScore += 44;
    }
    if (counterMemory_.consecutiveSuccess >= 2) {
        resetScore += 14;
    }
    if (pressureFactor > 0.85f && counterThreat > 0.75f) {
        resetScore += 10;
    }

    int neutralScore = 8 + static_cast<int>(midFactor * 26.0f) +
                       static_cast<int>((1.0f - counterThreat) * 8.0f) -
                       static_cast<int>(pressureFactor * 10.0f) -
                       static_cast<int>(farFactor * 8.0f);
    if (isPhase2) {
        neutralScore -= 2;
    }

    pressureScore = (std::max)(0, pressureScore);
    counterBaitScore = (std::max)(0, counterBaitScore);
    counterPunishScore = (std::max)(0, counterPunishScore);
    antiGuardScore = (std::max)(0, antiGuardScore);
    chaseScore = (std::max)(0, chaseScore);
    resetScore = (std::max)(0, resetScore);
    neutralScore = (std::max)(0, neutralScore);

    switch (PickWeightedIndex({pressureScore, counterBaitScore, counterPunishScore,
                               antiGuardScore, chaseScore, resetScore,
                               neutralScore})) {
    case 0:
        return TacticState::Pressure;
    case 1:
        return TacticState::CounterBait;
    case 2:
        return TacticState::CounterPunish;
    case 3:
        return TacticState::AntiGuard;
    case 4:
        return TacticState::Chase;
    case 5:
        return TacticState::Reset;
    case 6:
    default:
        return TacticState::Neutral;
    }
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
        BeginNeutralAction();
        break;
    }
}

bool Enemy::TryBeginStalkAction(float chance, float repeatScale) {
    if (runtime_.lastActionKind == ActionKind::Stalk) {
        chance *= repeatScale;
    }
    if (runtime_.stalkRepeatCount >= stalkRepeatLimit_) {
        chance = 0.0f;
    }

    float stalkRoll =
        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    if (stalkRoll >= chance) {
        return false;
    }

    return TryBeginTacticAction(ActionKind::Stalk);
}

ActionKind Enemy::SelectNeutralAction(float distance) const {
    if (distance <= config_.core.nearAttackDistance) {
        int guardWeight = nearGuardWeight_ + neutralNearGuardBonus_;
        int sweepWeight = nearSweepWeight_ + neutralNearSweepBonus_;
        int shotWeight = neutralNearShotWeight_;
        int rushWeight = (std::max)(0, nearRushWeight_ - neutralNearRushPenalty_);
        int smashWeight = (std::max)(8, nearSmashWeight_ / 3);

        if (playerObs_.isCounterStance) {
            guardWeight += 12;
            shotWeight += 8;
            smashWeight -= 4;
        }
        if (playerObs_.isAttacking) {
            guardWeight += 10;
            sweepWeight += 6;
            rushWeight -= 6;
        }
        if (playerObs_.isGuarding) {
            shotWeight += 6;
        }

        switch (PickWeightedIndex(
            {guardWeight, sweepWeight, shotWeight, rushWeight, smashWeight})) {
        case 0:
            return ActionKind::Guard;
        case 1:
            return ActionKind::Sweep;
        case 2:
            return ActionKind::Shot;
        case 3:
            return ActionKind::Rush;
        default:
            return ActionKind::Smash;
        }
    }

    int shotWeight = midShotWeight_ + neutralMidShotBonus_;
    int waveWeight = midWaveWeight_ + neutralMidWaveBonus_;
    int guardWeight = nearGuardWeight_ + neutralMidGuardBonus_;
    int rushWeight = (std::max)(0, midRushWeight_ - neutralMidRushPenalty_);
    int warpWeight = farWarpWeight_ + neutralMidWarpBonus_;

    if (playerObs_.isGuarding) {
        waveWeight += 12;
        shotWeight += 6;
    }
    if (playerObs_.isCounterStance) {
        guardWeight += 10;
        warpWeight += 8;
        rushWeight -= 6;
    }
    if (isDistanceStagnant_) {
        warpWeight += stagnantWarpBonus_ + 8;
        waveWeight += 6;
    }

    switch (PickWeightedIndex(
        {shotWeight, waveWeight, guardWeight, rushWeight, warpWeight})) {
    case 0:
        return ActionKind::Shot;
    case 1:
        return ActionKind::Wave;
    case 2:
        return ActionKind::Guard;
    case 3:
        return ActionKind::Rush;
    default:
        return ActionKind::Warp;
    }
}

ActionKind Enemy::SelectNearPressureAction() const {
    int smashWeight = nearSmashWeight_;
    int sweepWeight = nearSweepWeight_;
    int guardWeight = nearGuardWeight_;
    int rushWeight = nearRushWeight_;

    if (runtime_.phase == BossPhase::Phase2) {
        smashWeight += phase2NearSmashBonus_;
        sweepWeight += phase2NearSweepBonus_;
        guardWeight -= phase2NearGuardPenalty_;
        rushWeight += phase2NearRushBonus_;
    }

    if (runtime_.postCounterRhythmTimer > 0.0f) {
        smashWeight = static_cast<int>(smashWeight * 0.7f);
        sweepWeight = static_cast<int>(sweepWeight * 0.7f);
        guardWeight += 5;
        rushWeight += 8;
    }

    if (playerObs_.isAttacking) {
        sweepWeight += 10;
        guardWeight += 12;
        rushWeight -= 10;
    }
    if (playerObs_.isGuarding) {
        rushWeight += 8;
        guardWeight -= 2;
    }
    if (playerObs_.isCounterStance) {
        smashWeight -= 6;
        sweepWeight += 4;
        guardWeight += 8;
    }

    if (runtime_.lastActionKind == ActionKind::Smash) {
        smashWeight /= 2;
    } else if (runtime_.lastActionKind == ActionKind::Sweep) {
        sweepWeight /= 2;
    } else if (runtime_.lastActionKind == ActionKind::Guard) {
        guardWeight /= 2;
    } else if (runtime_.lastActionKind == ActionKind::Rush) {
        rushWeight /= 2;
    }

    switch (PickWeightedIndex({smashWeight, sweepWeight, guardWeight, rushWeight})) {
    case 0:
        return ActionKind::Smash;
    case 1:
        return ActionKind::Sweep;
    case 2:
        return ActionKind::Guard;
    default:
        return ActionKind::Rush;
    }
}

ActionKind Enemy::SelectMidPressureAction() const {
    int shotWeight = midShotWeight_ + phase2MidShotBonus_ + 6;
    int warpWeight = farWarpWeight_ + phase2FarWarpBonus_ + 10;
    int rushWeight = midRushWeight_ + phase2MidRushBonus_;
    int waveWeight = (std::max)(8, midWaveWeight_ / 2);

    if (runtime_.lastActionKind == ActionKind::Shot) {
        shotWeight /= 2;
        rushWeight += 10;
        warpWeight += 12;
    } else if (runtime_.lastActionKind == ActionKind::Warp) {
        warpWeight /= 2;
        rushWeight += 8;
        shotWeight += 8;
    } else if (runtime_.lastActionKind == ActionKind::Rush) {
        rushWeight /= 2;
        shotWeight += 10;
        warpWeight += 8;
    }

    if (runtime_.playerObs.isCounterStance) {
        shotWeight += 6;
        warpWeight += 8;
        rushWeight -= 8;
    }
    if (runtime_.playerObs.isGuarding) {
        waveWeight += 12;
        shotWeight += 6;
    }
    if (isDistanceStagnant_) {
        warpWeight += stagnantWarpBonus_ + 10;
        rushWeight += 6;
    }

    switch (PickWeightedIndex({shotWeight, warpWeight, rushWeight, waveWeight})) {
    case 0:
        return ActionKind::Shot;
    case 1:
        return ActionKind::Warp;
    case 2:
        return ActionKind::Rush;
    default:
        return ActionKind::Wave;
    }
}

ActionKind Enemy::SelectCounterPunishAction(float distance) const {
    int smashWeight = counterPunishSmashWeight_;
    int sweepWeight = counterPunishSweepWeight_;
    int rushWeight = counterPunishRushWeight_;

    if (runtime_.phase == BossPhase::Phase2) {
        smashWeight += phase2CounterPunishSmashBonus_;
        sweepWeight += phase2CounterPunishSweepBonus_;
        rushWeight -= 8;
    }

    if (distance <= config_.core.nearAttackDistance) {
        smashWeight += 10;
        sweepWeight += 5;
    } else {
        rushWeight += 15;
    }
    if (playerObs_.isAttacking) {
        rushWeight += 8;
        sweepWeight += 6;
    }

    switch (PickWeightedIndex({smashWeight, sweepWeight, rushWeight})) {
    case 0:
        return ActionKind::Smash;
    case 1:
        return ActionKind::Sweep;
    default:
        return ActionKind::Rush;
    }
}

ActionKind Enemy::SelectCounterBaitAction() const {
    int guardWeight = counterBaitGuardWeight_;
    int feintMeleeBonus = 0;

    if (runtime_.counterMemory.counterStancePressure > 0.8f) {
        feintMeleeBonus += 10;
    }
    if (runtime_.counterMemory.successCount > 0.8f) {
        feintMeleeBonus += 10;
    }
    if (runtime_.phase == BossPhase::Phase2) {
        feintMeleeBonus += phase2CounterBaitMeleeBonus_;
        guardWeight = (std::max)(0, guardWeight - 6);
    }

    ActionKind baitKind = DecideAdaptiveCounterBaitAction();
    int baitWeight = 20 + feintMeleeBonus;

    if (PickWeightedIndex({baitWeight, guardWeight}) == 0) {
        return baitKind;
    }
    return ActionKind::Guard;
}

ActionKind Enemy::SelectNearAntiGuardAction() const {
    int rushWeight = midRushWeight_ + antiGuardRushBonus_;
    int waveWeight = midWaveWeight_ + antiGuardWaveBonus_;
    int shotWeight = midShotWeight_ + antiGuardShotBonus_;

    if (runtime_.phase == BossPhase::Phase2) {
        rushWeight += phase2MidRushBonus_;
        shotWeight += phase2MidShotBonus_;
    }

    switch (PickWeightedIndex({rushWeight, waveWeight, shotWeight})) {
    case 0:
        return ActionKind::Rush;
    case 1:
        return ActionKind::Wave;
    default:
        return ActionKind::Shot;
    }
}

ActionKind Enemy::SelectFarAntiGuardAction() const {
    int shotWeight = farShotWeight_ + antiGuardShotBonus_;
    int waveWeight = farWaveWeight_ + antiGuardWaveBonus_;
    int warpWeight = farWarpWeight_;

    if (runtime_.phase == BossPhase::Phase2) {
        shotWeight += phase2MidShotBonus_;
        warpWeight += phase2FarWarpBonus_;
    }

    switch (PickWeightedIndex({shotWeight, waveWeight, warpWeight})) {
    case 0:
        return ActionKind::Shot;
    case 1:
        return ActionKind::Wave;
    default:
        return ActionKind::Warp;
    }
}

ActionKind Enemy::SelectChaseAction() const {
    int shotWeight = farShotWeight_;
    int warpWeight = farWarpWeight_;
    int waveWeight = farWaveWeight_;

    if (runtime_.phase == BossPhase::Phase2) {
        shotWeight += phase2MidShotBonus_;
        warpWeight += phase2FarWarpBonus_;
    }

    if (isDistanceStagnant_) {
        warpWeight += stagnantWarpBonus_;
    }

    if (runtime_.lastActionKind == ActionKind::Shot) {
        shotWeight /= 2;
    } else if (runtime_.lastActionKind == ActionKind::Warp) {
        warpWeight /= 2;
    } else if (runtime_.lastActionKind == ActionKind::Wave) {
        waveWeight /= 2;
    }

    if (runtime_.playerGuarding) {
        waveWeight += 10;
    }
    if (farDistanceTimer_ >= farDistanceWarpTimeThreshold_) {
        warpWeight += farDistanceWarpBonus_;
    }
    if (playerObs_.isAttacking) {
        waveWeight += 8;
        shotWeight += 6;
    }

    switch (PickWeightedIndex({shotWeight, warpWeight, waveWeight})) {
    case 0:
        return ActionKind::Shot;
    case 1:
        return ActionKind::Warp;
    default:
        return ActionKind::Wave;
    }
}

void Enemy::BeginNeutralAction() {
    float distance = GetDistanceToPlayer();

    if (distance > config_.core.farAttackDistance + 1.0f) {
        BeginChaseAction();
        return;
    }

    float stalkChance = (distance <= config_.core.nearAttackDistance)
                            ? neutralNearStalkChance_
                            : neutralMidStalkChance_;
    if (playerObs_.isCounterStance) {
        stalkChance += 0.08f;
    }
    if (playerObs_.isAttacking) {
        stalkChance += 0.06f;
    }
    if (TryBeginStalkAction(stalkChance, 0.55f)) {
        return;
    }

    runtime_.stalkRepeatCount = 0;
    ActionKind nextAction = SelectNeutralAction(distance);
    if (nextAction == ActionKind::Warp) {
        TryBeginTacticActionOrFallback(nextAction, ActionKind::Wave);
    } else {
        TryBeginTacticAction(nextAction);
    }
}

void Enemy::BeginPressureAction() {
    float distance = GetDistanceToPlayer();
    float phase2PressureDistanceMax =
        config_.core.farAttackDistance + phase2PressureMaxDistanceBonus_;

    if (distance <= config_.core.nearAttackDistance) {
        float stalkChance = stalkNearEnterChance_;

        if (runtime_.playerObs.isCounterStance) {
            stalkChance += 0.10f;
        }
        if (runtime_.postCounterRhythmTimer > 0.0f) {
            stalkChance += 0.12f;
        }
        if (TryBeginStalkAction(stalkChance, 0.45f)) {
            return;
        }

        runtime_.stalkRepeatCount = 0;
        TryBeginTacticAction(SelectNearPressureAction());
        return;
    }

    if (runtime_.phase == BossPhase::Phase2 &&
        distance <= phase2PressureDistanceMax) {
        ActionKind nextAction = SelectMidPressureAction();
        runtime_.stalkRepeatCount = 0;

        if (nextAction == ActionKind::Warp) {
            TryBeginTacticActionOrFallback(nextAction, ActionKind::Rush);
        } else {
            TryBeginTacticAction(nextAction);
        }
        return;
    }

    BeginNeutralAction();
}

void Enemy::BeginCounterBaitAction() {
    float distance = GetDistanceToPlayer();

    if (distance > config_.core.farAttackDistance) {
        BeginChaseAction();
        return;
    }

    float stalkChance = stalkMidEnterChance_;
    if (runtime_.counterMemory.counterStancePressure > 0.8f) {
        stalkChance += 0.12f;
    }
    if (TryBeginStalkAction(stalkChance, 0.45f)) {
        return;
    }

    runtime_.stalkRepeatCount = 0;
    TryBeginTacticAction(SelectCounterBaitAction());
}

void Enemy::BeginCounterPunishAction() {
    float distance = GetDistanceToPlayer();

    if (distance > config_.core.farAttackDistance) {
        TryBeginTacticActionOrFallback(ActionKind::Warp, ActionKind::Rush);
        return;
    }

    TryBeginTacticAction(SelectCounterPunishAction(distance));
}

void Enemy::BeginAntiGuardAction() {
    float distance = GetDistanceToPlayer();

    if (distance <= config_.core.nearAttackDistance) {
        TryBeginTacticAction(SelectNearAntiGuardAction());
        return;
    }

    ActionKind nextAction = SelectFarAntiGuardAction();
    if (nextAction == ActionKind::Warp) {
        TryBeginTacticActionOrFallback(nextAction, ActionKind::Wave);
    } else {
        TryBeginTacticAction(nextAction);
    }
}

void Enemy::BeginChaseAction() {
    float distance = GetDistanceToPlayer();

    if (distance <= config_.core.farAttackDistance) {
        float stalkChance = stalkMidEnterChance_;
        if (TryBeginStalkAction(stalkChance, 0.40f)) {
            return;
        }
    }

    if (distance <= config_.core.farAttackDistance && !isDistanceStagnant_ &&
        !playerObs_.isGuarding && !playerObs_.isCounterStance) {
        BeginNeutralAction();
        return;
    }

    ActionKind nextAction = SelectChaseAction();
    runtime_.stalkRepeatCount = 0;

    if (nextAction == ActionKind::Warp) {
        TryBeginTacticActionOrFallback(nextAction, ActionKind::Wave);
    } else {
        TryBeginTacticAction(nextAction);
    }
}

void Enemy::BeginResetAction() {
    if (IsWarpSuspendedForPresentation()) {
        TryBeginTacticAction(ActionKind::Guard);
        return;
    }

    if (runtime_.forceEscapeWarpNext) {
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

    TryBeginTacticActionOrFallback(ActionKind::Warp, ActionKind::Guard);
}

void Enemy::UpdateStalkByStep(float deltaTime) {
    switch (runtime_.action.step) {
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

    float usedYaw = runtime_.facingYaw;

    float rightX = std::cosf(usedYaw);
    float rightZ = -std::sinf(usedYaw);

    float forwardX = std::sinf(usedYaw);
    float forwardZ = std::cosf(usedYaw);

    float moveX = 0.0f;
    float moveZ = 0.0f;

    moveX += rightX * runtime_.stalkMoveDir * stalkStrafeRadiusWeight_;
    moveZ += rightZ * runtime_.stalkMoveDir * stalkStrafeRadiusWeight_;

    moveX += forwardX * runtime_.stalkForwardBias * stalkForwardAdjustWeight_;
    moveZ += forwardZ * runtime_.stalkForwardBias * stalkForwardAdjustWeight_;

    float len = std::sqrtf(moveX * moveX + moveZ * moveZ);
    if (len > 0.0001f) {
        moveX /= len;
        moveZ /= len;
    }

    tf_.position.x += moveX * stalkMoveSpeed_ * deltaTime;
    tf_.position.z += moveZ * stalkMoveSpeed_ * deltaTime;

    if (runtime_.stateTimer >= runtime_.currentHoldDuration) {
        EndAttack();
    }
}

void Enemy::BeginStalkAction() {
    EnterHold(RandomRange(stalkDurationMin_, stalkDurationMax_));
    BeginAction(ActionKind::Stalk, ActionStep::Move);
    runtime_.stalkRepeatCount++;
}
