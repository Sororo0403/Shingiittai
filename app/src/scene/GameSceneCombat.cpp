#include "GameScene.h"
#include "compat/ParticleCompat.h"
#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX;

static constexpr float kMinVectorLength = 0.0001f;

namespace {
constexpr CollisionManager::LayerMask kLayerPlayer = 1u << 0;
constexpr CollisionManager::LayerMask kLayerEnemy = 1u << 1;
constexpr CollisionManager::LayerMask kLayerPlayerAttack = 1u << 2;
constexpr CollisionManager::LayerMask kLayerEnemyAttack = 1u << 3;
constexpr float kReleaseCounterWindowDuration = 0.62f;

CollisionManager::BodyId AddCollisionBody(
    CollisionManager &collisionManager, const OBB &box,
    CollisionManager::LayerMask layer, CollisionManager::LayerMask mask) {
    CollisionManager::BodyDesc desc{};
    desc.shape = CollisionManager::Shape::FromOBB(box);
    desc.filter.layer = layer;
    desc.filter.mask = mask;
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

static SwordCounterAxis RequiredCounterAxisForAction(ActionKind kind) {
    switch (kind) {
    case ActionKind::Smash:
        return SwordCounterAxis::Vertical;
    case ActionKind::Sweep:
        return SwordCounterAxis::Horizontal;
    default:
        return SwordCounterAxis::None;
    }
}

static bool IsSlashAxisMatched(const Sword &sword,
                               SwordCounterAxis requiredAxis) {
    if (requiredAxis == SwordCounterAxis::None) {
        return sword.CanSlashCounter();
    }
    return sword.CanSlashCounter() && sword.GetSlashAxis() == requiredAxis;
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

    for (bool isSlashing : slashStates) {
        observation.isAttacking = observation.isAttacking || isSlashing;
    }

    return observation;
}

float GameScene::ApplyEnemyDamage(float damage, bool deferTransitions) {
    if (damage <= 0.0f || enemy_.GetHP() <= 0.0f) {
        return 0.0f;
    }

    const float actualDamage =
        deferTransitions ? enemy_.TakeDamageDeferTransitions(damage)
                         : enemy_.TakeDamage(damage);
    return actualDamage;
}

void GameScene::UpdateCombat(float gameplayDeltaTime) {
    collisionManager_.Clear();

    const auto playerBox = player_.GetOBB();
    const auto enemyBodyBox = enemy_.GetBodyOBB();
    const auto enemyLeftHandBox = enemy_.GetLeftHandOBB();
    const auto enemyRightHandBox = enemy_.GetRightHandOBB();
    AddCollisionBody(collisionManager_, playerBox, kLayerPlayer,
                     kLayerEnemyAttack);
    const bool enemyCollisionDisabled = enemy_.IsWarpCollisionDisabled();
    const CollisionManager::BodyId enemyBody =
        enemyCollisionDisabled
            ? CollisionManager::kInvalidBodyId
            : AddCollisionBody(collisionManager_, enemyBodyBox, kLayerEnemy,
                               kLayerPlayerAttack);
    const CollisionManager::BodyId enemyLeftHandBody =
        enemyCollisionDisabled
            ? CollisionManager::kInvalidBodyId
            : AddCollisionBody(collisionManager_, enemyLeftHandBox,
                               kLayerEnemy, kLayerPlayerAttack);
    const CollisionManager::BodyId enemyRightHandBody =
        enemyCollisionDisabled
            ? CollisionManager::kInvalidBodyId
            : AddCollisionBody(collisionManager_, enemyRightHandBox,
                               kLayerEnemy, kLayerPlayerAttack);
    const std::array<CollisionManager::BodyId, 3> enemyHurtBodies = {
        enemyBody, enemyLeftHandBody, enemyRightHandBody};
    const ActionKind enemyActionKind = enemy_.GetActionKind();
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
                       130.0f);
        const float vulnerabilityDuration =
            player_.GetCounterVulnerabilityDuration();
        if (enemy_.NotifyCountered(vulnerabilityDuration)) {
            forceSyncEnemyAnimationThisFrame = true;
        }
        const float appliedDamage = ApplyEnemyDamage(counterDamage);
        CombatFeedbackEvent feedback{};
        feedback.type = CombatFeedbackEventType::CounterSuccess;
        feedback.position = enemy_.GetTransform().position;
        feedback.position.y += 1.0f;
        feedback.direction =
            DirectionFromTo(player_.GetTransform().position,
                            enemy_.GetTransform().position);
        feedback.power = appliedDamage / 10.0f;
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
    if (enemyRedPunishUncounterable_ &&
        (!(enemyActionKind == ActionKind::Smash ||
           enemyActionKind == ActionKind::Sweep) ||
         enemyActionStep == ActionStep::Recovery ||
         enemyActionStep == ActionStep::None)) {
        enemyRedPunishUncounterable_ = false;
    }
    const bool isEnemyMeleePreparationOrRelease =
        (enemyActionKind == ActionKind::Smash ||
         enemyActionKind == ActionKind::Sweep) &&
        (enemyActionStep == ActionStep::Charge ||
         enemyActionStep == ActionStep::Hold ||
         enemyActionStep == ActionStep::Active);
    const bool isPreReleaseCounterWindow =
        (enemyActionKind == ActionKind::Smash ||
         enemyActionKind == ActionKind::Sweep) &&
        isEnemyMeleePreparationOrRelease &&
        enemyActionStep != ActionStep::Active &&
        enemy_.GetReleaseAnticipationRatio() > 0.0f;
    const bool isReleaseCounterWindow =
        !enemyRedPunishUncounterable_ &&
        (isPreReleaseCounterWindow ||
         (isEnemyMeleeCommitted &&
          enemy_.GetActionTimerForPresentation() <=
              kReleaseCounterWindowDuration));
    const bool suppressNormalSlashHitDuringEnemyMelee =
        isEnemyMeleePreparationOrRelease;

    const float enemyAttackDamage = enemy_.GetCurrentAttackDamage();
    const float enemyAttackKnockback = enemy_.GetCurrentAttackKnockback();
    const bool isEnemyCounterWindow = isReleaseCounterWindow;
    OBB enemyAttackBox{};
    CollisionManager::BodyId enemyAttackBody =
        CollisionManager::kInvalidBodyId;
    if (isEnemyMeleeCommitted || isPreReleaseCounterWindow) {
        enemyAttackBox = enemy_.GetAttackOBB();
        enemyAttackBody =
            AddCollisionBody(collisionManager_, enemyAttackBox,
                             kLayerEnemyAttack,
                             kLayerPlayer | kLayerPlayerAttack);
    }

    const bool isBadSlashPunishWindow =
        isEnemyMeleePreparationOrRelease &&
        !isReleaseCounterWindow &&
        !enemyRedPunishUncounterable_;
    if (isBadSlashPunishWindow && playerHitCooldown_ <= 0.0f) {
        for (size_t i = 0; i < swordSlashStates.size(); ++i) {
            if (!swordSlashStates[i] || previousCombatSlashStates_[i]) {
                continue;
            }

            enemy_.ForcePunishRelease();
            enemyRedPunishUncounterable_ = true;
            forceSyncEnemyAnimationThisFrame = true;

            const XMFLOAT2 knockbackDir = NormalizeXZ(
                player_.GetTransform().position.x -
                    enemy_.GetTransform().position.x,
                player_.GetTransform().position.z -
                    enemy_.GetTransform().position.z);
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
            feedback.power = enemyAttackDamage / 8.0f;
            DispatchCombatFeedback(feedback);
            playerHitCooldown_ = 0.45f;
            break;
        }
    }

    bool counterTriggeredThisFrame = false;
    for (size_t i = 0; i < swords.size(); ++i) {
        const Sword *sword = swords[i];
        if (sword == nullptr || !swordSlashStates[i]) {
            continue;
        }
        const auto swordHitBox = sword->GetOBB();
        const CollisionManager::BodyId swordHitBody =
            AddCollisionBody(collisionManager_, swordHitBox,
                             kLayerPlayerAttack,
                             kLayerEnemy | kLayerEnemyAttack);
        const bool hitBody = isEnemyHurtBodyHit(swordHitBody);
        const bool canSlashCounter =
            isReleaseCounterWindow &&
            playerHitCooldown_ <= 0.0f && isEnemyCounterWindow &&
            enemyAttackBody != CollisionManager::kInvalidBodyId &&
            IsNearXZ(player_.GetTransform().position, enemyAttackBox.center,
                     GetReadableMeleeRadius(enemyAttackBox) + 0.85f) &&
            IsSlashAxisMatched(*sword,
                               RequiredCounterAxisForAction(enemyActionKind));

        if (canSlashCounter) {
            triggerSuccessfulCounter(i, enemyAttackDamage, 0.2f);
            counterTriggeredThisFrame = true;
            break;
        }

        if (enemyHitCooldown_ <= 0.0f &&
            !suppressNormalSlashHitDuringEnemyMelee) {
            if (hitBody) {
                const float swordDamage = swordAttackDamages[i];
                const float appliedDamage = ApplyEnemyDamage(swordDamage);
                if (appliedDamage <= 0.0f) {
                    break;
                }
                CombatFeedbackEvent feedback{};
                feedback.type = CombatFeedbackEventType::PlayerSlashHit;
                feedback.position = swordHitBox.center;
                feedback.direction =
                    DirectionFromTo(player_.GetTransform().position,
                                    enemy_.GetTransform().position);
                feedback.power = appliedDamage / 10.0f;
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

    previousCombatSlashStates_ = swordSlashStates;

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
