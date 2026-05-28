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
    case ActionKind::BladeClash:
        return MakeOBB(bodyTf_, GetCurrentAttackHitBoxSize());
    case ActionKind::ArcaneLaser:
        return GetArcaneLaserAttackOBB();
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

OBB Enemy::GetArcaneLaserAttackOBB() const {
    const DirectX::XMFLOAT3 muzzle = GetArcaneLaserMuzzlePosition();
    const DirectX::XMFLOAT3 direction = GetArcaneLaserDirection();
    const float range = config_.attacks.arcaneLaser.range;
    const float radius = config_.attacks.arcaneLaser.radius;

    Transform attackTf{};
    attackTf.scale = {1.0f, 1.0f, 1.0f};
    attackTf.position = {muzzle.x + direction.x * range * 0.5f,
                         muzzle.y - radius * 0.50f,
                         muzzle.z + direction.z * range * 0.5f};
    const float yaw = std::atan2(direction.x, direction.z);
    DirectX::XMStoreFloat4(
        &attackTf.rotation,
        DirectX::XMQuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f));
    return MakeOBB(attackTf, {radius * 2.0f, radius * 2.0f, range});
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

bool Enemy::IsPunishableRecovery() const {
    switch (action_.kind) {
    case ActionKind::Smash:
    case ActionKind::Sweep:
    case ActionKind::BladeClash:
    case ActionKind::ArcaneLaser:
        return action_.step == ActionStep::Recovery && hitReactionTimer_ <= 0.0f;
    default:
        return false;
    }
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
    case ActionKind::BladeClash:
        return &config_.attacks.bladeClash.profile.timing;
    case ActionKind::ArcaneLaser:
        return &config_.attacks.arcaneLaser.profile.timing;
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
    case ActionKind::BladeClash:
        return &config_.attacks.bladeClash.profile.attack;
    case ActionKind::ArcaneLaser:
        return &config_.attacks.arcaneLaser.profile.attack;
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
    case ActionKind::BladeClash:
        return &config_.attacks.bladeClash.profile.attack;
    case ActionKind::ArcaneLaser:
        return &config_.attacks.arcaneLaser.profile.attack;
    default:
        return nullptr;
    }
}

bool Enemy::ShouldUseLockedAttackYaw() const {
    switch (action_.kind) {
    case ActionKind::Smash:
    case ActionKind::Sweep:
    case ActionKind::BladeClash:
    case ActionKind::ArcaneLaser:
        break;
    default:
        return false;
    }

    switch (action_.step) {
    case ActionStep::Charge:
        return hasTrackingLocked_;
    case ActionStep::Hold:
    case ActionStep::Active:
        return true;
    default:
        return false;
    }
}

DirectX::XMFLOAT3 Enemy::GetArcaneLaserMuzzlePosition() const {
    const float usedYaw =
        ShouldUseLockedAttackYaw() ? lockedAttackYaw_ : facingYaw_;
    const float forwardX = std::sin(usedYaw);
    const float forwardZ = std::cos(usedYaw);
    return {tf_.position.x +
                forwardX * config_.attacks.arcaneLaser.muzzleForwardOffset,
            tf_.position.y + config_.attacks.arcaneLaser.muzzleHeightOffset,
            tf_.position.z +
                forwardZ * config_.attacks.arcaneLaser.muzzleForwardOffset};
}

float Enemy::GetArcaneLaserChargeRatio() const {
    if (action_.kind != ActionKind::ArcaneLaser) {
        return 0.0f;
    }
    const float chargeTime = config_.attacks.arcaneLaser.profile.chargeTime;
    if (chargeTime <= 0.0001f) {
        return action_.step == ActionStep::Charge ? 1.0f : 0.0f;
    }
    if (action_.step == ActionStep::Charge) {
        return std::clamp(stateTimer_ / chargeTime, 0.0f, 1.0f);
    }
    return action_.step == ActionStep::Active ? 1.0f : 0.0f;
}

bool Enemy::IsArcaneLaserCounterWindow() const {
    if (action_.kind != ActionKind::ArcaneLaser ||
        action_.step != ActionStep::Active) {
        return false;
    }

    const auto &timing = config_.attacks.arcaneLaser.profile.timing;
    return stateTimer_ >= timing.activeStartTime &&
           stateTimer_ <= timing.activeEndTime;
}

float Enemy::TakeDamage(float damage) {
    const float appliedDamage = TakeDamageDeferTransitions(damage);
    ResolveDeferredDamageTransitions();
    return appliedDamage;
}

float Enemy::TakeDamageNoReaction(float damage) {
    const float previousHitReactionTimer = hitReactionTimer_;
    const float appliedDamage = TakeDamageDeferTransitionsNoReaction(damage);
    ResolveDeferredDamageTransitions();
    if (!deathFinished_ && !isDying_ && hp_ > 0.0f &&
        !phaseTransitionActive_) {
        hitReactionTimer_ = previousHitReactionTimer;
    }
    return appliedDamage;
}

float Enemy::TakeDamageDeferTransitions(float damage) {
    if (damage <= 0.0f || deathFinished_ || isDying_ || hp_ <= 0.0f) {
        return 0.0f;
    }

    const float previousHp = hp_;
    hp_ -= damage;
    if (hp_ < 0.0f) {
        hp_ = 0.0f;
    }
    const float appliedDamage = previousHp - hp_;

    if (hp_ > 0.0f) {
        hitReactionTimer_ = (std::max)(hitReactionTimer_, hitReactionDuration_);
    }
    return appliedDamage;
}

float Enemy::TakeDamageDeferTransitionsNoReaction(float damage) {
    const float previousHitReactionTimer = hitReactionTimer_;
    const float appliedDamage = TakeDamageDeferTransitions(damage);
    if (!deathFinished_ && !isDying_ && hp_ > 0.0f) {
        hitReactionTimer_ = previousHitReactionTimer;
    }
    return appliedDamage;
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

void Enemy::ForcePunishRelease() {
    if (!(action_.kind == ActionKind::Smash ||
          action_.kind == ActionKind::Sweep)) {
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

    attackReleaseCueIssued_ = true;
    ChangeActionStep(ActionStep::Active);
    IssueAttackCue(EnemyAttackCueType::Cancel, ActionKind::None, 0.0f);

    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (timing != nullptr) {
        stateTimer_ = timing->activeStartTime;
        isAttackActive_ = true;
    }
}

bool Enemy::NotifyCountered(float vulnerabilityDuration) {
    return ApplyCounterBreakReaction(vulnerabilityDuration);
}

bool Enemy::IsBladeClashAction() const {
    return action_.kind == ActionKind::BladeClash;
}

bool Enemy::IsBladeClashWindow() const {
    if (!IsBladeClashAction() || action_.step != ActionStep::Active) {
        return false;
    }

    const AttackTimingParam &timing = config_.attacks.bladeClash.profile.timing;
    return stateTimer_ >= timing.activeStartTime &&
           stateTimer_ <= timing.activeEndTime;
}

void Enemy::NotifyBladeClashLanded() {
    if (IsBladeClashAction()) {
        EndAttack();
    }
}

void Enemy::ResolveBladeClash(bool playerWon) {
    if (playerWon) {
        EndAttack();
        hitReactionTimer_ = 0.0f;
        counterRecoilTimer_ = 0.0f;
        stateTimer_ = 0.0f;
        UpdateFacingToPlayer();
        UpdateParts();
        return;
    }

    EndAttack();
    UpdateFacingToPlayer();
    UpdateParts();
}

void Enemy::FinishCounterRecoil() {
    counterRecoilTimer_ = 0.0f;
    if (hitReactionTimer_ > hitReactionDuration_) {
        hitReactionTimer_ = hitReactionDuration_;
    }
    UpdateParts();
}

bool Enemy::ApplyCounterBreakReaction(float vulnerabilityDuration) {
    const bool isCounterBreakableAction =
        action_.kind == ActionKind::Smash || action_.kind == ActionKind::Sweep ||
        action_.kind == ActionKind::BladeClash ||
        action_.kind == ActionKind::ArcaneLaser;
    if (!isCounterBreakableAction) {
        return false;
    }

    EndAttack();
    counterRecoilTimer_ = counterRecoilDuration_;
    hitReactionTimer_ = (std::max)(hitReactionTimer_, vulnerabilityDuration);
    stateTimer_ = 0.0f;

    tactic_ = DecideTactic();

    UpdateFacingToPlayer();
    UpdateParts();
    return true;
}
