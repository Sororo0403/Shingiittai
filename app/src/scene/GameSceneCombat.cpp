#include "GameScene.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

using namespace DirectX;

static constexpr float kMinVectorLength = 0.0001f;

namespace {
constexpr CollisionManager::LayerMask kLayerPlayer = 1u << 0;
constexpr CollisionManager::LayerMask kLayerEnemy = 1u << 1;
constexpr CollisionManager::LayerMask kLayerPlayerAttack = 1u << 2;
constexpr CollisionManager::LayerMask kLayerEnemyAttack = 1u << 3;
constexpr CollisionManager::LayerMask kLayerPlayerCounter = 1u << 4;
constexpr CollisionManager::LayerMask kLayerEnemyProjectile = 1u << 5;
constexpr CollisionManager::LayerMask kLayerReflectedProjectile = 1u << 6;

CollisionManager::BodyId AddCollisionBody(
    CollisionManager &collisionManager, const OBB &box,
    CollisionManager::LayerMask layer, CollisionManager::LayerMask mask) {
    CollisionManager::BodyDesc desc{};
    desc.box = box;
    desc.layer = layer;
    desc.mask = mask;
    return collisionManager.AddBody(desc);
}
} // namespace

static XMFLOAT2 NormalizeXZ(float x, float z) {
    float length = std::sqrt(x * x + z * z);
    if (length < kMinVectorLength) {
        length = 1.0f;
    }

    return {x / length, z / length};
}

static XMFLOAT3 DirectionFromTo(const XMFLOAT3 &from, const XMFLOAT3 &to) {
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float dz = to.z - from.z;
    float length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (length < kMinVectorLength) {
        length = 1.0f;
    }

    return {dx / length, dy / length, dz / length};
}

static float DistanceSqXZ(const XMFLOAT3 &a, const XMFLOAT3 &b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz;
}

static bool IsNearXZ(const XMFLOAT3 &a, const XMFLOAT3 &b, float radius) {
    return DistanceSqXZ(a, b) <= radius * radius;
}

static bool IsChargeWeakPointWindow(ActionKind kind, ActionId id,
                                    ActionStep step) {
    (void)id;
    if (!(kind == ActionKind::Smash || kind == ActionKind::Sweep)) {
        return false;
    }
    return step == ActionStep::Charge || step == ActionStep::Hold;
}

static bool IsReadableChargeWeakPointHit(const OBB &swordHitBox,
                                         const XMFLOAT3 &enemyPosition) {
    XMFLOAT3 weakPoint = enemyPosition;
    weakPoint.y += 1.55f;
    const float dx = swordHitBox.center.x - weakPoint.x;
    const float dy = swordHitBox.center.y - weakPoint.y;
    const float dz = swordHitBox.center.z - weakPoint.z;
    return (dx * dx + dy * dy + dz * dz) <= (2.6f * 2.6f);
}

static bool IsSlashTowardPoint(const Sword &sword, const XMFLOAT3 &target,
                               float minDot = 0.10f) {
    if (!sword.CanSlashCounter()) {
        return false;
    }

    const XMFLOAT2 slashDir = sword.GetSlashDirection();
    float slashLenSq = slashDir.x * slashDir.x + slashDir.y * slashDir.y;
    if (slashLenSq < 0.01f) {
        return false;
    }

    const XMFLOAT3 &swordPos = sword.GetTransform().position;
    float targetX = target.x - swordPos.x;
    float targetY = target.y - swordPos.y;
    float targetLenSq = targetX * targetX + targetY * targetY;
    if (targetLenSq < 0.01f) {
        return true;
    }

    const float invSlashLen = 1.0f / std::sqrt(slashLenSq);
    const float invTargetLen = 1.0f / std::sqrt(targetLenSq);
    const float dot = (slashDir.x * invSlashLen) * (targetX * invTargetLen) +
                      (slashDir.y * invSlashLen) * (targetY * invTargetLen);
    return dot >= minDot;
}

static XMFLOAT2 MakeRandomChargeWeakPointDirection(
    const XMFLOAT2 *previousDirection = nullptr) {
    constexpr float kDiagonal = 0.70710678f;
    static const std::array<XMFLOAT2, 4> kDirections = {
        XMFLOAT2{1.0f, 0.0f}, XMFLOAT2{kDiagonal, kDiagonal},
        XMFLOAT2{0.0f, 1.0f}, XMFLOAT2{kDiagonal, -kDiagonal}};

    int directionIndex = std::rand() % static_cast<int>(kDirections.size());
    if (previousDirection == nullptr) {
        return kDirections[static_cast<size_t>(directionIndex)];
    }

    for (int attempt = 0; attempt < 6; ++attempt) {
        const XMFLOAT2 candidate = kDirections[static_cast<size_t>(directionIndex)];
        const float dot = candidate.x * previousDirection->x +
                          candidate.y * previousDirection->y;
        if (std::fabs(dot) < 0.98f) {
            return candidate;
        }
        directionIndex = std::rand() % static_cast<int>(kDirections.size());
    }

    return kDirections[static_cast<size_t>(
        (directionIndex + 1) % static_cast<int>(kDirections.size()))];
}

static bool IsSlashAlongDirection(const Sword &sword,
                                  const XMFLOAT2 &requiredDirection,
                                  float minAbsDot = 0.72f) {
    if (!sword.CanSlashCounter()) {
        return false;
    }

    XMFLOAT2 slashDir = sword.GetSlashDirection();
    float slashLenSq = slashDir.x * slashDir.x + slashDir.y * slashDir.y;
    float requiredLenSq = requiredDirection.x * requiredDirection.x +
                          requiredDirection.y * requiredDirection.y;
    if (slashLenSq < 0.01f || requiredLenSq < 0.01f) {
        return false;
    }

    const float invSlashLen = 1.0f / std::sqrt(slashLenSq);
    const float invRequiredLen = 1.0f / std::sqrt(requiredLenSq);
    const float dot = slashDir.x * invSlashLen *
                          requiredDirection.x * invRequiredLen +
                      slashDir.y * invSlashLen *
                          requiredDirection.y * invRequiredLen;
    return std::fabs(dot) >= minAbsDot;
}

static bool FindSlashTowardPoint(
    const std::array<const Sword *, Player::kSwordCount> &swords,
    const std::array<bool, Player::kSwordCount> &slashStates,
    const XMFLOAT3 &target, size_t &outSwordIndex, float minDot = 0.10f) {
    for (size_t i = 0; i < swords.size(); ++i) {
        const Sword *sword = swords[i];
        if (sword == nullptr || !slashStates[i]) {
            continue;
        }
        if (IsSlashTowardPoint(*sword, target, minDot)) {
            outSwordIndex = i;
            return true;
        }
    }

    return false;
}

static float GetReadableMeleeRadius(const OBB &box) {
    return (std::max)(box.size.x, box.size.z) * 0.55f + 0.75f;
}

static float GetReadableProjectileRadius(const XMFLOAT3 &size) {
    return (std::max)(size.x, size.z) * 0.70f + 0.45f;
}

static XMFLOAT4 MakeYawRotation(float yaw) {
    XMFLOAT4 rotation{};
    XMStoreFloat4(&rotation,
                  XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), yaw));
    return rotation;
}

static void TickCooldown(float &cooldown, float deltaTime) {
    if (cooldown <= 0.0f) {
        return;
    }

    cooldown -= deltaTime;
    if (cooldown < 0.0f) {
        cooldown = 0.0f;
    }
}

PlayerCombatObservation GameScene::BuildPlayerCombatObservation() const {
    const auto slashStates = player_.GetSwordSlashStates();

    PlayerCombatObservation observation{};
    observation.position = player_.GetTransform().position;
    observation.velocity = player_.GetVelocity();
    observation.isGuarding = false;
    observation.isCounterStance = false;
    observation.justCountered = player_.JustCountered();
    observation.justCounterFailed = player_.JustCounterFailed();
    observation.justCounterEarly = player_.JustCounterEarly();
    observation.justCounterLate = player_.JustCounterLate();
    observation.counterAxis = CounterAxis::None;

    for (bool isSlashing : slashStates) {
        observation.isAttacking = observation.isAttacking || isSlashing;
    }

    return observation;
}

void GameScene::UpdateCombat(float gameplayDeltaTime) {
    collisionManager_.Clear();

    const auto playerBox = player_.GetOBB();
    const bool isPlayerDodging = player_.IsDamageInvulnerable();
    const float playerRecoveryDamageScale =
        player_.IsAttackRecovery() ? 1.65f : 1.0f;
    const auto enemyBodyBox = enemy_.GetBodyOBB();
    const auto enemyLeftHandBox = enemy_.GetLeftHandOBB();
    const auto enemyRightHandBox = enemy_.GetRightHandOBB();
    AddCollisionBody(collisionManager_, playerBox, kLayerPlayer,
                     kLayerEnemyAttack | kLayerEnemyProjectile);
    const CollisionManager::BodyId enemyBody =
        AddCollisionBody(collisionManager_, enemyBodyBox, kLayerEnemy,
                         kLayerPlayerAttack | kLayerReflectedProjectile);
    const CollisionManager::BodyId enemyLeftHandBody =
        AddCollisionBody(collisionManager_, enemyLeftHandBox, kLayerEnemy,
                         kLayerPlayerAttack | kLayerReflectedProjectile);
    const CollisionManager::BodyId enemyRightHandBody =
        AddCollisionBody(collisionManager_, enemyRightHandBox, kLayerEnemy,
                         kLayerPlayerAttack | kLayerReflectedProjectile);
    const std::array<CollisionManager::BodyId, 3> enemyHurtBodies = {
        enemyBody, enemyLeftHandBody, enemyRightHandBody};
    const ActionKind enemyActionKind = enemy_.GetActionKind();
    const ActionId enemyActionId = enemy_.GetActionId();
    const ActionStep enemyActionStep = enemy_.GetActionStep();
    const auto swords = player_.GetSwords();
    const auto swordSlashStates = player_.GetSwordSlashStates();
    const auto swordAttackDamages = player_.GetSwordAttackDamages();
    bool startCounterCinematicThisFrame = false;
    bool stopCounterCinematicThisFrame = false;
    bool forceSyncEnemyAnimationThisFrame = false;
    auto triggerSuccessfulCounter = [&](size_t swordIndex, float enemyDamage,
                                        float hitCooldown) {
        const float counterDamage =
            (std::max)(enemyDamage * player_.GetCounterDamageMultiplier(),
                       140.0f);
        const float vulnerabilityDuration =
            player_.GetCounterVulnerabilityDuration();
        player_.NotifyCounterSuccess(swordIndex);
        if (enemy_.NotifyCountered(vulnerabilityDuration)) {
            forceSyncEnemyAnimationThisFrame = true;
        }
        enemy_.TakeDamage(counterDamage);
        player_.NotifyAttackHit(swordIndex, counterDamage);
        CombatFeedbackEvent feedback{};
        feedback.type = CombatFeedbackEventType::CounterSuccess;
        feedback.position = enemy_.GetTransform().position;
        feedback.position.y += 1.0f;
        feedback.direction =
            DirectionFromTo(player_.GetTransform().position,
                            enemy_.GetTransform().position);
        feedback.power = counterDamage / 10.0f;
        feedback.swordIndex = swordIndex;
        DispatchCombatFeedback(feedback);
        playerHitCooldown_ = hitCooldown;
        startCounterCinematicThisFrame = true;
        counterCinematicTimer_ = counterCinematicDuration_;
    };
    auto isEnemyHurtBodyHit = [&](CollisionManager::BodyId attackBody) {
        for (CollisionManager::BodyId targetBody : enemyHurtBodies) {
            if (targetBody == CollisionManager::kInvalidBodyId) {
                continue;
            }
            if (collisionManager_.Test(attackBody, targetBody)) {
                return true;
            }
        }
        return false;
    };

    TickCooldown(enemyHitCooldown_, gameplayDeltaTime);
    TickCooldown(playerHitCooldown_, gameplayDeltaTime);

    const bool isEnemySmashCommitted =
        (enemyActionKind == ActionKind::Smash &&
         enemyActionStep == ActionStep::Active);
    const bool isEnemySweepCommitted =
        (enemyActionKind == ActionKind::Sweep &&
         enemyActionStep == ActionStep::Active);
    const bool isEnemySmashMeleeWindow =
        isEnemySmashCommitted && enemy_.IsAttackActive();
    const bool isEnemySweepMeleeWindow =
        isEnemySweepCommitted && enemy_.IsAttackActive();
    const bool isEnemyMeleeActive =
        isEnemySmashMeleeWindow || isEnemySweepMeleeWindow;
    const bool isEnemyMeleeCommitted =
        isEnemySmashCommitted || isEnemySweepCommitted;
    const bool isEnemyChargeWeakPointWindow =
        IsChargeWeakPointWindow(enemyActionKind, enemyActionId,
                                enemyActionStep);
    if (isEnemyChargeWeakPointWindow) {
        if (chargeWeakPointActionKind_ != enemyActionKind) {
            chargeWeakPointActionKind_ = enemyActionKind;
            chargeWeakPointBroken_ = false;
            chargeWeakPointSlashCount_ = 0;
            chargeWeakPointRequiredDirections_[0] =
                MakeRandomChargeWeakPointDirection();
            chargeWeakPointRequiredDirections_[1] =
                MakeRandomChargeWeakPointDirection(
                    &chargeWeakPointRequiredDirections_[0]);
            previousChargeWeakPointSlashStates_.fill(false);
        }
    } else {
        chargeWeakPointActionKind_ = ActionKind::None;
        chargeWeakPointBroken_ = false;
        chargeWeakPointSlashCount_ = 0;
        previousChargeWeakPointSlashStates_.fill(false);
    }

    const float enemyAttackDamage = enemy_.GetCurrentAttackDamage();
    const float enemyAttackKnockback = enemy_.GetCurrentAttackKnockback();
    const bool isEnemyCounterWindow =
        isEnemyMeleeCommitted;
    OBB enemyAttackBox{};
    CollisionManager::BodyId enemyAttackBody =
        CollisionManager::kInvalidBodyId;
    if (isEnemyMeleeCommitted) {
        enemyAttackBox = enemy_.GetAttackOBB();
        enemyAttackBody =
            AddCollisionBody(collisionManager_, enemyAttackBox,
                             kLayerEnemyAttack,
                             kLayerPlayer | kLayerPlayerAttack);
    }

    bool counterTriggeredThisFrame = false;
    for (size_t i = 0; i < swords.size(); ++i) {
        const Sword *sword = swords[i];
        if (sword == nullptr || !swordSlashStates[i]) {
            continue;
        }
        const bool isFreshChargeWeakPointSlash =
            !previousChargeWeakPointSlashStates_[i];

        const auto swordHitBox = sword->GetOBB();
        const CollisionManager::BodyId swordHitBody =
            AddCollisionBody(collisionManager_, swordHitBox,
                             kLayerPlayerAttack,
                             kLayerEnemy | kLayerEnemyAttack);
        const bool hitBody = isEnemyHurtBodyHit(swordHitBody);
        const bool canTouchChargeWeakPoint =
            enemyHitCooldown_ <= 0.0f && isEnemyChargeWeakPointWindow &&
            !chargeWeakPointBroken_ &&
            isFreshChargeWeakPointSlash &&
            IsNearXZ(player_.GetTransform().position,
                     enemy_.GetTransform().position, 5.2f) &&
            (hitBody || IsReadableChargeWeakPointHit(
                            swordHitBox, enemy_.GetTransform().position));
        const size_t requiredDirectionIndex = static_cast<size_t>(std::clamp(
            chargeWeakPointSlashCount_, 0,
            static_cast<int>(chargeWeakPointRequiredDirections_.size() - 1)));
        const XMFLOAT2 requiredChargeSlashDirection =
            chargeWeakPointRequiredDirections_[requiredDirectionIndex];
        const bool canMatchChargeWeakPoint =
            canTouchChargeWeakPoint &&
            IsSlashAlongDirection(*sword, requiredChargeSlashDirection);
        const bool canSlashCounter =
            playerHitCooldown_ <= 0.0f && isEnemyCounterWindow &&
            enemyAttackBody != CollisionManager::kInvalidBodyId &&
            IsNearXZ(player_.GetTransform().position, enemyAttackBox.center,
                     GetReadableMeleeRadius(enemyAttackBox) + 0.85f) &&
            IsSlashTowardPoint(*sword, enemyAttackBox.center, 0.05f);

        if (canSlashCounter) {
            triggerSuccessfulCounter(i, enemyAttackDamage, 0.2f);
            counterTriggeredThisFrame = true;
            break;
        }

        if (canMatchChargeWeakPoint) {
            ++chargeWeakPointSlashCount_;
            if (chargeWeakPointSlashCount_ < 2) {
                chargeWeakPointRequiredDirections_[static_cast<size_t>(
                    chargeWeakPointSlashCount_)] =
                    MakeRandomChargeWeakPointDirection(
                        &chargeWeakPointRequiredDirections_[static_cast<size_t>(
                            chargeWeakPointSlashCount_ - 1)]);

                CombatFeedbackEvent feedback{};
                feedback.type = CombatFeedbackEventType::PlayerSlashHit;
                feedback.position = swordHitBox.center;
                feedback.direction =
                    DirectionFromTo(player_.GetTransform().position,
                                    enemy_.GetTransform().position);
                feedback.power = 3.0f;
                feedback.swordIndex = i;
                DispatchCombatFeedback(feedback);
                player_.NotifyAttackHit(i, 0.0f);
                enemyHitCooldown_ = 0.12f;
                break;
            }

            chargeWeakPointBroken_ = true;
            chargeWeakPointActionKind_ = ActionKind::None;
            const float breakDamage = 78.0f + swordAttackDamages[i] * 1.25f;
            if (enemy_.NotifyCountered(0.95f)) {
                forceSyncEnemyAnimationThisFrame = true;
            }
            enemy_.TakeDamage(breakDamage);
            player_.NotifyAttackHit(i, breakDamage);

            CombatFeedbackEvent feedback{};
            feedback.type = CombatFeedbackEventType::CounterSuccess;
            feedback.position = enemy_.GetTransform().position;
            feedback.position.y += 1.55f;
            feedback.direction =
                DirectionFromTo(player_.GetTransform().position,
                                enemy_.GetTransform().position);
            feedback.power = breakDamage / 12.0f;
            feedback.swordIndex = i;
            DispatchCombatFeedback(feedback);
            enemyHitCooldown_ = 0.18f;
            break;
        }

        if (enemyHitCooldown_ <= 0.0f) {
            if (hitBody) {
                const float swordDamage = swordAttackDamages[i];
                enemy_.TakeDamage(swordDamage);
                player_.NotifyAttackHit(i, swordDamage);
                CombatFeedbackEvent feedback{};
                feedback.type = CombatFeedbackEventType::PlayerSlashHit;
                feedback.position = swordHitBox.center;
                feedback.direction =
                    DirectionFromTo(player_.GetTransform().position,
                                    enemy_.GetTransform().position);
                feedback.power = swordDamage / 10.0f;
                feedback.swordIndex = i;
                DispatchCombatFeedback(feedback);
                enemyHitCooldown_ = 0.2f;
                if (counterCinematicActive_) {
                    stopCounterCinematicThisFrame = true;
                }
            }
        }

        if (enemyHitCooldown_ > 0.0f) {
            break;
        }
    }

    if (isEnemyMeleeActive && !counterTriggeredThisFrame) {
        const bool bossHitPlayer =
            enemyAttackBody != CollisionManager::kInvalidBodyId &&
            IsNearXZ(player_.GetTransform().position, enemyAttackBox.center,
                     GetReadableMeleeRadius(enemyAttackBox));

        if (bossHitPlayer && playerHitCooldown_ <= 0.0f) {
            const XMFLOAT2 knockbackDir = NormalizeXZ(
                player_.GetTransform().position.x -
                    enemy_.GetTransform().position.x,
                player_.GetTransform().position.z -
                    enemy_.GetTransform().position.z);

            if (isPlayerDodging) {
                playerHitCooldown_ = 0.08f;
            } else {
                enemy_.NotifyAttackConnected();
                player_.TakeDamage(enemyAttackDamage * playerRecoveryDamageScale);
                player_.AddKnockback(
                    {knockbackDir.x * enemyAttackKnockback, 0.0f,
                     knockbackDir.y * enemyAttackKnockback});
                CombatFeedbackEvent feedback{};
                feedback.type = CombatFeedbackEventType::PlayerDamaged;
                feedback.position = player_.GetTransform().position;
                feedback.position.y += 1.0f;
                feedback.direction =
                    DirectionFromTo(enemy_.GetTransform().position,
                                    player_.GetTransform().position);
                feedback.power = enemyAttackDamage / 10.0f;
                DispatchCombatFeedback(feedback);
                playerHitCooldown_ = 0.4f;
            }
        }
    }

    if (enemyActionKind == ActionKind::Nova && enemy_.IsNovaImpactWindow() &&
        playerHitCooldown_ <= 0.0f) {
        const XMFLOAT3 &enemyPos = enemy_.GetTransform().position;
        const XMFLOAT3 &playerPos = player_.GetTransform().position;
        const float dx = playerPos.x - enemyPos.x;
        const float dz = playerPos.z - enemyPos.z;
        const float distanceSq = dx * dx + dz * dz;
        const float radius = enemy_.GetNovaImpactRadius();

        if (distanceSq <= radius * radius) {
            const XMFLOAT2 knockbackDir = NormalizeXZ(dx, dz);
            const float novaDamage = enemy_.GetNovaImpactDamage();
            const float novaKnockback = enemy_.GetNovaImpactKnockback();

            if (isPlayerDodging) {
                playerHitCooldown_ = 0.08f;
            } else {
                size_t counterSwordIndex = swords.size();
                if (FindSlashTowardPoint(swords, swordSlashStates,
                                         enemy_.GetTransform().position,
                                         counterSwordIndex, 0.00f)) {
                    triggerSuccessfulCounter(counterSwordIndex, novaDamage * 2.0f,
                                             0.18f);
                } else {
                    enemy_.NotifyAttackConnected();
                    player_.TakeDamage(novaDamage * playerRecoveryDamageScale);
                    player_.AddKnockback(
                        {knockbackDir.x * novaKnockback, 0.0f,
                         knockbackDir.y * novaKnockback});
                    CombatFeedbackEvent feedback{};
                    feedback.type = CombatFeedbackEventType::PlayerDamaged;
                    feedback.position = playerPos;
                    feedback.position.y += 1.0f;
                    feedback.direction = {knockbackDir.x, 0.0f, knockbackDir.y};
                    feedback.power = novaDamage / 8.0f;
                    DispatchCombatFeedback(feedback);
                    playerHitCooldown_ = 0.55f;
                }
            }
        }
    }

    if (enemyActionKind == ActionKind::Nova && enemy_.IsNovaImpactWindow()) {
        OBB novaDebugBox{};
        novaDebugBox.center = enemy_.GetTransform().position;
        novaDebugBox.center.y += 0.06f;
        const float diameter = enemy_.GetNovaImpactRadius() * 2.0f;
        novaDebugBox.size = {diameter, 0.12f, diameter};
        novaDebugBox.rotation = MakeYawRotation(0.0f);
        AddCollisionBody(collisionManager_, novaDebugBox, kLayerEnemyAttack, 0u);
    }

    const auto &bullets = enemy_.GetBullets();
    for (size_t i = 0; i < bullets.size(); ++i) {
        const auto &bullet = bullets[i];
        if (!bullet.isAlive) {
            continue;
        }

        OBB bulletBox{};
        bulletBox.center = bullet.position;
        bulletBox.size = enemy_.GetBulletHitBoxSize();
        bulletBox.rotation =
            MakeYawRotation(std::atan2(bullet.velocity.x, bullet.velocity.z));
        const float bulletThreatRadius =
            GetReadableProjectileRadius(bulletBox.size);
        const CollisionManager::BodyId bulletBody = AddCollisionBody(
            collisionManager_, bulletBox,
            bullet.isReflected ? kLayerReflectedProjectile
                               : kLayerEnemyProjectile,
            bullet.isReflected ? kLayerEnemy
                               : (kLayerPlayer | kLayerPlayerCounter));

        size_t projectileCounterSwordIndex = swords.size();
        if (!bullet.isReflected &&
            IsNearXZ(bullet.position, player_.GetTransform().position,
                     bulletThreatRadius + 0.85f) &&
            FindSlashTowardPoint(swords, swordSlashStates, bullet.position,
                                 projectileCounterSwordIndex, 0.00f)) {
            enemy_.ReflectBullet(i, enemy_.GetTransform().position);
            CombatFeedbackEvent feedback{};
            feedback.type = CombatFeedbackEventType::ProjectileReflect;
            feedback.position = bullet.position;
            feedback.direction = DirectionFromTo(bullet.position,
                                                enemy_.GetTransform().position);
            feedback.power = enemy_.GetBulletDamage() / 5.0f;
            DispatchCombatFeedback(feedback);
            continue;
        }

        if (bullet.isReflected) {
            if (isEnemyHurtBodyHit(bulletBody) &&
                enemyHitCooldown_ <= 0.0f) {
                const float damage = enemy_.GetBulletDamage() * damageMultiplier_;
                enemy_.TakeDamage(damage);
                player_.NotifyAttackHit(damage);
                enemy_.DestroyBullet(i);
                CombatFeedbackEvent feedback{};
                feedback.type = CombatFeedbackEventType::ProjectileReflect;
                feedback.position = bullet.position;
                feedback.direction = DirectionFromTo(player_.GetTransform().position,
                                                    enemy_.GetTransform().position);
                feedback.power = damage / 10.0f;
                DispatchCombatFeedback(feedback);
                enemyHitCooldown_ = 0.2f;
            }
            continue;
        }

        if (IsNearXZ(bullet.position, player_.GetTransform().position,
                     bulletThreatRadius)) {
            if (isPlayerDodging) {
                playerHitCooldown_ = 0.08f;
                continue;
            }

            if (playerHitCooldown_ <= 0.0f) {
                const XMFLOAT2 hitDir =
                    NormalizeXZ(bullet.velocity.x, bullet.velocity.z);

                size_t counterSwordIndex = swords.size();
                if (FindSlashTowardPoint(swords, swordSlashStates,
                                         bullet.position, counterSwordIndex,
                                         0.00f)) {
                    triggerSuccessfulCounter(counterSwordIndex,
                                             enemy_.GetBulletDamage() * 2.0f,
                                             0.12f);
                } else {
                    player_.TakeDamage(enemy_.GetBulletDamage() *
                                       playerRecoveryDamageScale);
                    player_.AddKnockback(
                        {hitDir.x * enemy_.GetBulletKnockback(), 0.0f,
                         hitDir.y * enemy_.GetBulletKnockback()});
                    CombatFeedbackEvent feedback{};
                    feedback.type = CombatFeedbackEventType::PlayerDamaged;
                    feedback.position = bullet.position;
                    feedback.direction = {hitDir.x, 0.0f, hitDir.y};
                    feedback.power = enemy_.GetBulletDamage() / 5.0f;
                    DispatchCombatFeedback(feedback);
                    enemy_.DestroyBullet(i);
                    playerHitCooldown_ = 0.3f;
                }
            }

            enemy_.ConsumeBullet(i);
            break;
        }
    }

    const auto &waves = enemy_.GetWaves();
    for (size_t i = 0; i < waves.size(); ++i) {
        const auto &wave = waves[i];
        if (!wave.isAlive) {
            continue;
        }

        OBB waveBox{};
        waveBox.center = wave.position;
        waveBox.size = enemy_.GetWaveHitBoxSize();
        waveBox.rotation =
            MakeYawRotation(std::atan2(wave.direction.x, wave.direction.z));
        const float waveThreatRadius =
            GetReadableProjectileRadius(waveBox.size);
        const CollisionManager::BodyId waveBody = AddCollisionBody(
            collisionManager_, waveBox,
            wave.isReflected ? kLayerReflectedProjectile
                             : kLayerEnemyProjectile,
            wave.isReflected ? kLayerEnemy
                             : (kLayerPlayer | kLayerPlayerCounter));

        size_t waveCounterSwordIndex = swords.size();
        if (!wave.isReflected &&
            IsNearXZ(wave.position, player_.GetTransform().position,
                     waveThreatRadius + 0.85f) &&
            FindSlashTowardPoint(swords, swordSlashStates, wave.position,
                                 waveCounterSwordIndex, 0.00f)) {
            enemy_.ReflectWave(i, enemy_.GetTransform().position);
            CombatFeedbackEvent feedback{};
            feedback.type = CombatFeedbackEventType::ProjectileReflect;
            feedback.position = wave.position;
            feedback.direction = DirectionFromTo(wave.position,
                                                enemy_.GetTransform().position);
            feedback.power = enemy_.GetWaveDamage() / 5.0f;
            DispatchCombatFeedback(feedback);
            continue;
        }

        if (wave.isReflected) {
            if (isEnemyHurtBodyHit(waveBody) &&
                enemyHitCooldown_ <= 0.0f) {
                const float damage = enemy_.GetWaveDamage() * damageMultiplier_;
                enemy_.TakeDamage(damage);
                player_.NotifyAttackHit(damage);
                enemy_.DestroyWave(i);
                CombatFeedbackEvent feedback{};
                feedback.type = CombatFeedbackEventType::ProjectileReflect;
                feedback.position = wave.position;
                feedback.direction = DirectionFromTo(player_.GetTransform().position,
                                                    enemy_.GetTransform().position);
                feedback.power = damage / 10.0f;
                DispatchCombatFeedback(feedback);
                enemyHitCooldown_ = 0.2f;
            }
            continue;
        }

        if (IsNearXZ(wave.position, player_.GetTransform().position,
                     waveThreatRadius)) {
            if (isPlayerDodging) {
                playerHitCooldown_ = 0.08f;
                continue;
            }

            if (playerHitCooldown_ <= 0.0f) {
                const XMFLOAT2 hitDir =
                    NormalizeXZ(wave.direction.x, wave.direction.z);

                size_t counterSwordIndex = swords.size();
                if (FindSlashTowardPoint(swords, swordSlashStates,
                                         wave.position, counterSwordIndex,
                                         0.00f)) {
                    triggerSuccessfulCounter(counterSwordIndex,
                                             enemy_.GetWaveDamage() * 2.0f,
                                             0.12f);
                } else {
                    player_.TakeDamage(enemy_.GetWaveDamage() *
                                       playerRecoveryDamageScale);
                    player_.AddKnockback(
                        {hitDir.x * enemy_.GetWaveKnockback(), 0.0f,
                         hitDir.y * enemy_.GetWaveKnockback()});
                    CombatFeedbackEvent feedback{};
                    feedback.type = CombatFeedbackEventType::PlayerDamaged;
                    feedback.position = wave.position;
                    feedback.direction = {hitDir.x, 0.0f, hitDir.y};
                    feedback.power = enemy_.GetWaveDamage() / 5.0f;
                    DispatchCombatFeedback(feedback);
                    enemy_.DestroyWave(i);
                    playerHitCooldown_ = 0.35f;
                }
            }

            enemy_.ConsumeWave(i);
            break;
        }
    }

    previousChargeWeakPointSlashStates_ = swordSlashStates;

    if (startCounterCinematicThisFrame) {
        counterCinematicActive_ = true;
        counterCinematicTimer_ = counterCinematicDuration_;
        SetEnemyAnimationFrozen(true);
    }
    if (stopCounterCinematicThisFrame) {
        counterCinematicActive_ = false;
        counterCinematicTimer_ = 0.0f;
        enemy_.FinishCounterRecoil();
        SetEnemyAnimationFrozen(false);
    }
    if (forceSyncEnemyAnimationThisFrame) {
        SyncEnemyAnimation();
        if (counterCinematicActive_ || startCounterCinematicThisFrame) {
            SetEnemyAnimationFrozen(true);
        }
    }
}
