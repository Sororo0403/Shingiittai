#include "Enemy.h"

#include <algorithm>
#include <cmath>

OBB Enemy::MakeOBB(const Transform &tf, const DirectX::XMFLOAT3 &size) const {
    OBB box{};
    box.center = tf.position;
    box.center.y += size.y * 0.5f;
    box.size = size;
    box.rotation = tf.rotation;
    return box;
}

OBB Enemy::GetBodyOBB() const {
    DirectX::XMFLOAT3 generousSize = bodySize_;
    generousSize.x *= 1.45f;
    generousSize.y *= 1.22f;
    generousSize.z *= 1.45f;
    return MakeOBB(bodyTf_, generousSize);
}

OBB Enemy::GetLeftHandOBB() const {
    DirectX::XMFLOAT3 generousSize = handSize_;
    generousSize.x *= 1.55f;
    generousSize.y *= 1.35f;
    generousSize.z *= 1.55f;
    return MakeOBB(leftHandTf_, generousSize);
}

OBB Enemy::GetRightHandOBB() const {
    DirectX::XMFLOAT3 generousSize = handSize_;
    generousSize.x *= 1.55f;
    generousSize.y *= 1.35f;
    generousSize.z *= 1.55f;
    return MakeOBB(rightHandTf_, generousSize);
}

OBB Enemy::GetAttackOBB() const {
    switch (action_.kind) {
    case ActionKind::Smash:
        return GetSmashAttackOBB();
    case ActionKind::Sweep:
        return GetSweepAttackOBB();
    case ActionKind::Shot:
        return MakeOBB(IsDualCounterHandStage() ? leftHandTf_ : rightHandTf_,
                       GetCurrentAttackHitBoxSize());
    case ActionKind::BladeClash:
        return MakeOBB(bodyTf_, GetCurrentAttackHitBoxSize());
    default:
        return OBB{};
    }
}

OBB Enemy::GetSmashAttackOBB() const {
    const float usedYaw = ShouldUseLockedAttackYaw() ? lockedAttackYaw_ : facingYaw_;
    const float forwardX = std::sin(usedYaw);
    const float forwardZ = std::cos(usedYaw);

    Transform attackTf{};
    attackTf.scale = {1.0f, 1.0f, 1.0f};
    attackTf.rotation = bodyTf_.rotation;
    attackTf.position = tf_.position;
    attackTf.position.x += forwardX * 1.05f;
    attackTf.position.y += 0.04f;
    attackTf.position.z += forwardZ * 1.05f;

    return MakeOBB(attackTf, GetCurrentAttackHitBoxSize());
}

OBB Enemy::GetSweepAttackOBB() const {
    const float usedYaw = ShouldUseLockedAttackYaw() ? lockedAttackYaw_ : facingYaw_;
    const float forwardX = std::sin(usedYaw);
    const float forwardZ = std::cos(usedYaw);

    Transform attackTf{};
    attackTf.scale = {1.0f, 1.0f, 1.0f};
    attackTf.rotation = bodyTf_.rotation;
    attackTf.position = tf_.position;
    attackTf.position.x += forwardX * 0.42f;
    attackTf.position.y += 0.04f;
    attackTf.position.z += forwardZ * 0.42f;

    return MakeOBB(attackTf, GetCurrentAttackHitBoxSize());
}

float Enemy::GetCurrentAttackDamage() const {
    const AttackParam *param = GetCurrentAttackParam();
    return param ? param->damage : 0.0f;
}

float Enemy::GetCurrentAttackKnockback() const {
    const AttackParam *param = GetCurrentAttackParam();
    return param ? param->knockback : 0.0f;
}

DirectX::XMFLOAT3 Enemy::GetCurrentAttackHitBoxSize() const {
    const AttackParam *param = GetCurrentAttackParam();
    return param ? param->hitBoxSize : DirectX::XMFLOAT3{0.1f, 0.1f, 0.1f};
}

bool Enemy::IsNovaImpactPending() const {
    return action_.kind == ActionKind::Nova && action_.step == ActionStep::Active &&
           stateTimer_ < config_.attacks.nova.impactTime;
}

bool Enemy::IsNovaImpactWindow() const {
    if (action_.kind != ActionKind::Nova || action_.step != ActionStep::Active) {
        return false;
    }

    const float start = config_.attacks.nova.impactTime;
    const float end = start + config_.attacks.nova.impactWindow;
    return stateTimer_ >= start && stateTimer_ <= end;
}

float Enemy::GetNovaImpactRadius() const {
    return config_.attacks.nova.impactRadius;
}

float Enemy::GetNovaImpactDamage() const {
    return config_.attacks.nova.impactDamage;
}

float Enemy::GetNovaImpactKnockback() const {
    return config_.attacks.nova.impactKnockback;
}

bool Enemy::IsPunishableRecovery() const {
    switch (action_.kind) {
    case ActionKind::Smash:
    case ActionKind::Sweep:
    case ActionKind::Shot:
    case ActionKind::BladeClash:
    case ActionKind::Wave:
    case ActionKind::Cage:
    case ActionKind::Nova:
        return action_.step == ActionStep::Recovery && hitReactionTimer_ <= 0.0f;
    default:
        return false;
    }
}

float Enemy::GetRecoveryProgressForPresentation() const {
    if (action_.step != ActionStep::Recovery) {
        return 0.0f;
    }

    float duration = 1.0f;
    switch (action_.kind) {
    case ActionKind::Smash:
    case ActionKind::Sweep:
        if (const AttackTimingParam *timing = GetCurrentAttackTiming()) {
            duration = timing->totalTime - timing->recoveryStartTime;
        }
        duration += (action_.kind == ActionKind::Smash) ? 0.18f : 0.16f;
        break;
    case ActionKind::Shot:
        duration = config_.attacks.shot.recoveryTime + 0.18f;
        break;
    case ActionKind::BladeClash:
        duration = config_.attacks.bladeClash.recoveryTime + 0.18f;
        break;
    case ActionKind::Wave:
        duration = config_.attacks.wave.recoveryTime + 0.18f;
        break;
    case ActionKind::Cage:
        duration = config_.attacks.cage.recoveryTime + 0.18f;
        break;
    case ActionKind::Nova:
        duration = config_.attacks.nova.recoveryTime + 0.25f;
        break;
    default:
        duration = 1.0f;
        break;
    }

    if (duration <= 0.0001f) {
        return 1.0f;
    }
    return (std::clamp)(stateTimer_ / duration, 0.0f, 1.0f);
}

float Enemy::GetDistanceToPlayer() const {
    const float dx = playerPos_.x - tf_.position.x;
    const float dy = playerPos_.y - tf_.position.y;
    const float dz = playerPos_.z - tf_.position.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

const AttackTimingParam *Enemy::GetCurrentAttackTiming() const {
    switch (action_.kind) {
    case ActionKind::Smash:
        return &config_.attacks.smash.melee.base.timing;
    case ActionKind::Sweep:
        return &config_.attacks.sweep.melee.base.timing;
    default:
        return nullptr;
    }
}

AttackParam *Enemy::GetCurrentAttackParam() {
    switch (action_.kind) {
    case ActionKind::Smash:
        return &config_.attacks.smash.melee.base.attack;
    case ActionKind::Sweep:
        return &config_.attacks.sweep.melee.base.attack;
    case ActionKind::Shot:
        return &config_.attacks.shot.attack;
    case ActionKind::BladeClash:
        return &config_.attacks.bladeClash.attack;
    case ActionKind::Wave:
        return &config_.attacks.wave.attack;
    case ActionKind::Cage:
        return &config_.attacks.cage.attack;
    case ActionKind::Nova:
        return &config_.attacks.wave.attack;
    default:
        return nullptr;
    }
}

const AttackParam *Enemy::GetCurrentAttackParam() const {
    switch (action_.kind) {
    case ActionKind::Smash:
        return &config_.attacks.smash.melee.base.attack;
    case ActionKind::Sweep:
        return &config_.attacks.sweep.melee.base.attack;
    case ActionKind::Shot:
        return &config_.attacks.shot.attack;
    case ActionKind::BladeClash:
        return &config_.attacks.bladeClash.attack;
    case ActionKind::Wave:
        return &config_.attacks.wave.attack;
    case ActionKind::Cage:
        return &config_.attacks.cage.attack;
    case ActionKind::Nova:
        return &config_.attacks.wave.attack;
    default:
        return nullptr;
    }
}

bool Enemy::IsCurrentAttackInActiveWindow() const {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        return false;
    }
    const float t = GetCurrentActionTime();
    return t >= timing->activeStartTime && t <= timing->activeEndTime;
}

bool Enemy::IsCurrentAttackInRecoveryWindow() const {
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    return timing ? GetCurrentActionTime() >= timing->recoveryStartTime : false;
}

bool Enemy::ShouldUseLockedAttackYaw() const {
    switch (action_.kind) {
    case ActionKind::Smash:
    case ActionKind::Sweep:
        return true;
    default:
        return false;
    }
}

void Enemy::TakeDamage(float damage) {
    TakeDamageDeferTransitions(damage);
    ResolveDeferredDamageTransitions();
}

void Enemy::TakeDamageDeferTransitions(float damage) {
    if (deathFinished_ || isDying_) {
        return;
    }

    hp_ -= damage;
    if (hp_ < 0.0f) {
        hp_ = 0.0f;
    }

    if (hp_ > 0.0f) {
        hitReactionTimer_ = (std::max)(hitReactionTimer_, hitReactionDuration_);
    }
}

void Enemy::ResolveDeferredDamageTransitions() {
    if (deathFinished_ || isDying_) {
        return;
    }

    const bool wasPhaseTransitionActive = phaseTransitionActive_;
    UpdateBossPhase();
    const bool beganPhaseTransition =
        !wasPhaseTransitionActive && phaseTransitionActive_;
    if (hp_ <= 0.0f) {
        isDying_ = true;
        deathTimer_ = 0.0f;
        deathStartY_ = tf_.position.y;
        hitReactionTimer_ = 0.0f;
        tf_.scale = {1.0f, 1.0f, 1.0f};
        EndAttack();
        UpdateParts();
        return;
    }

    if (beganPhaseTransition) {
        hitReactionTimer_ = 0.0f;
        counterRecoilTimer_ = 0.0f;
        UpdateParts();
        return;
    }

    hitReactionTimer_ = (std::max)(hitReactionTimer_, hitReactionDuration_);
}

void Enemy::SetPhase2DebugHealth() {
    if (deathFinished_ || isDying_) {
        return;
    }

    const float phase2Hp =
        config_.core.maxHp * config_.core.phase2HealthRatioThreshold;
    hp_ = (std::max)(1.0f, phase2Hp);
    hitReactionTimer_ = 0.0f;
    counterRecoilTimer_ = 0.0f;
    UpdateBossPhase();
    UpdateParts();
}

void Enemy::NotifyAttackConnected() { currentActionConnected_ = true; }

void Enemy::NotifyAttackGuarded() { currentActionGuarded_ = true; }

void Enemy::ForcePunishRelease() {
    if (!(action_.kind == ActionKind::Smash ||
          action_.kind == ActionKind::Sweep ||
          action_.kind == ActionKind::Shot)) {
        return;
    }
    if (!(action_.step == ActionStep::Charge ||
          action_.step == ActionStep::Hold ||
          action_.step == ActionStep::Active)) {
        return;
    }

    if (!hasTrackingLocked_) {
        LockCurrentFacing();
        hasTrackingLocked_ = true;
    }
    ChangeActionStep(ActionStep::Active);
    if (action_.kind == ActionKind::Shot) {
        dualCounterStage_ = 0;
        dualCounterStageResolved_ = false;
    }
    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (timing != nullptr) {
        stateTimer_ = timing->activeStartTime;
        isAttackActive_ = true;
    } else if (action_.kind == ActionKind::Shot) {
        stateTimer_ = 0.20f;
        isAttackActive_ = true;
    }
}

bool Enemy::IsDualCounterAction() const {
    return action_.kind == ActionKind::Shot &&
           (action_.step == ActionStep::Charge ||
            action_.step == ActionStep::Active);
}

bool Enemy::IsBladeClashAction() const {
    return action_.kind == ActionKind::BladeClash &&
           (action_.step == ActionStep::Charge ||
            action_.step == ActionStep::Active);
}

bool Enemy::IsBladeClashWindow() const {
    return action_.kind == ActionKind::BladeClash &&
           action_.step == ActionStep::Active && !dualCounterStageResolved_ &&
           stateTimer_ >= 0.12f &&
           stateTimer_ <= config_.attacks.bladeClash.activeTime;
}

bool Enemy::IsDualCounterWindow() const {
    return action_.kind == ActionKind::Shot && action_.step == ActionStep::Active &&
           !dualCounterStageResolved_ && stateTimer_ >= 0.14f &&
           stateTimer_ <= 0.72f;
}

bool Enemy::IsDualCounterHandStage() const {
    if (dualCounterStage_ <= 0) {
        return dualCounterFirstHand_;
    }
    return !dualCounterFirstHand_;
}

bool Enemy::NotifyDualCountered() {
    if (action_.kind != ActionKind::Shot || action_.step != ActionStep::Active) {
        return false;
    }

    currentActionGuarded_ = true;
    dualCounterStageResolved_ = true;
    isAttackActive_ = false;

    if (dualCounterStage_ <= 0) {
        dualCounterStage_ = 1;
        stateTimer_ = 0.0f;
        dualCounterStageResolved_ = false;
        LockCurrentFacing();
        return false;
    }

    return ApplyCounterBreakReaction(0.95f);
}

void Enemy::NotifyDualStrikeLanded() {
    if (action_.kind != ActionKind::Shot || action_.step != ActionStep::Active) {
        NotifyAttackConnected();
        return;
    }

    NotifyAttackConnected();
    dualCounterStageResolved_ = true;
    isAttackActive_ = false;

    if (dualCounterStage_ <= 0) {
        dualCounterStage_ = 1;
        stateTimer_ = 0.0f;
        dualCounterStageResolved_ = false;
        LockCurrentFacing();
        return;
    }

    ChangeActionStep(ActionStep::Recovery);
}

void Enemy::NotifyBladeClashLanded() {
    if (action_.kind != ActionKind::BladeClash ||
        action_.step != ActionStep::Active) {
        NotifyAttackConnected();
        return;
    }

    NotifyAttackConnected();
    dualCounterStageResolved_ = true;
    isAttackActive_ = false;
    ChangeActionStep(ActionStep::Recovery);
}

void Enemy::ResolveBladeClash(bool playerWon) {
    if (playerWon) {
        NotifyAttackConnected();
        EndAttack();
        hitReactionTimer_ = 0.0f;
        counterRecoilTimer_ = 0.0f;
        ResetChainContext();
        ResetPostActionState();
        stateTimer_ = 0.0f;
        UpdateFacingToPlayer();
        UpdateParts();
        return;
    }

    NotifyAttackConnected();
    EndAttack();
    UpdateFacingToPlayer();
    UpdateParts();
}

bool Enemy::NotifyCountered() { return ApplyCounterBreakReaction(); }

bool Enemy::NotifyCountered(float vulnerabilityDuration) {
    return ApplyCounterBreakReaction(vulnerabilityDuration);
}

void Enemy::FinishCounterRecoil() {
    counterRecoilTimer_ = 0.0f;
    if (hitReactionTimer_ > hitReactionDuration_) {
        hitReactionTimer_ = hitReactionDuration_;
    }
    UpdateParts();
}

bool Enemy::ApplyCounterBreakReaction(float vulnerabilityDuration) {
    RegisterCounterSuccessReaction();

    const bool isCounterBreakableAction =
        action_.kind == ActionKind::Smash || action_.kind == ActionKind::Sweep ||
        action_.kind == ActionKind::Shot ||
        action_.kind == ActionKind::BladeClash;
    if (!isCounterBreakableAction) {
        return false;
    }

    EndAttack();
    counterRecoilTimer_ = counterRecoilDuration_;
    hitReactionTimer_ = (std::max)(hitReactionTimer_, vulnerabilityDuration);
    ResetChainContext();
    ResetPostActionState();
    stateTimer_ = 0.0f;

    tactic_ = DecideTactic();
    closePressureTimer_ = 0.0f;
    stagnantTimer_ = 0.0f;
    isDistanceStagnant_ = false;

    UpdateFacingToPlayer();
    UpdateParts();
    return true;
}
