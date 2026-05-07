#include "GameScene.h"
#include <array>
#include <cmath>

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

static void TickCooldown(float &cooldown, float deltaTime) {
    if (cooldown <= 0.0f) {
        return;
    }

    cooldown -= deltaTime;
    if (cooldown < 0.0f) {
        cooldown = 0.0f;
    }
}

static CounterAxis ToEnemyCounterAxis(SwordCounterAxis axis) {
    switch (axis) {
    case SwordCounterAxis::Vertical:
        return CounterAxis::Vertical;
    case SwordCounterAxis::Horizontal:
        return CounterAxis::Horizontal;
    default:
        return CounterAxis::None;
    }
}

PlayerCombatObservation GameScene::BuildPlayerCombatObservation() const {
    const auto slashStates = player_.GetSwordSlashStates();

    PlayerCombatObservation observation{};
    observation.position = player_.GetTransform().position;
    observation.velocity = player_.GetVelocity();
    observation.isGuarding = player_.IsGuarding();
    observation.isCounterStance = player_.IsCounterStance();
    observation.justCountered = player_.JustCountered();
    observation.justCounterFailed = player_.JustCounterFailed();
    observation.justCounterEarly = player_.JustCounterEarly();
    observation.justCounterLate = player_.JustCounterLate();
    observation.counterAxis = ToEnemyCounterAxis(player_.GetCounterAxis());

    for (bool isSlashing : slashStates) {
        observation.isAttacking = observation.isAttacking || isSlashing;
    }

    return observation;
}

void GameScene::UpdateCombat(float gameplayDeltaTime) {
    collisionManager_.Clear();

    const auto playerBox = player_.GetOBB();
    const bool isPlayerGuarding = player_.IsGuarding();
    const bool isPlayerCountering = player_.IsCounterStance();
    const float guardDamageMultiplier = player_.GetGuardDamageMultiplier();
    const auto enemyBodyBox = enemy_.GetBodyOBB();
    const CollisionManager::BodyId playerBody =
        AddCollisionBody(collisionManager_, playerBox, kLayerPlayer,
                         kLayerEnemyAttack | kLayerEnemyProjectile);
    const CollisionManager::BodyId enemyBody =
        AddCollisionBody(collisionManager_, enemyBodyBox, kLayerEnemy,
                         kLayerPlayerAttack | kLayerReflectedProjectile);
    const ActionKind enemyActionKind = enemy_.GetActionKind();
    const ActionStep enemyActionStep = enemy_.GetActionStep();
    const auto swords = player_.GetSwords();
    const auto swordSlashStates = player_.GetSwordSlashStates();
    const auto swordAttackDamages = player_.GetSwordAttackDamages();
    std::array<CollisionManager::BodyId, Player::kSwordCount> counterBodies{};
    counterBodies.fill(CollisionManager::kInvalidBodyId);
    for (size_t i = 0; i < swords.size(); ++i) {
        const Sword *sword = swords[i];
        if (sword == nullptr || !sword->IsCounterStance()) {
            continue;
        }
        counterBodies[i] = AddCollisionBody(
            collisionManager_, sword->GetCounterOBB(), kLayerPlayerCounter,
            kLayerEnemyAttack | kLayerEnemyProjectile);
    }

    bool startCounterCinematicThisFrame = false;
    bool stopCounterCinematicThisFrame = false;
    bool forceSyncEnemyAnimationThisFrame = false;
    auto triggerSuccessfulCounter = [&](size_t swordIndex, float enemyDamage,
                                        float hitCooldown) {
        const float counterDamage =
            enemyDamage * player_.GetCounterDamageMultiplier();
        const float vulnerabilityDuration =
            player_.GetCounterVulnerabilityDuration();
        player_.NotifyCounterSuccess(swordIndex);
        if (enemy_.NotifyCountered(vulnerabilityDuration)) {
            forceSyncEnemyAnimationThisFrame = true;
        }
        enemy_.TakeDamage(counterDamage);
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
    };
    auto isCounterBodyHit = [&](CollisionManager::BodyId targetBody) {
        for (CollisionManager::BodyId counterBody : counterBodies) {
            if (counterBody == CollisionManager::kInvalidBodyId) {
                continue;
            }
            if (collisionManager_.Test(targetBody, counterBody)) {
                return true;
            }
        }
        return false;
    };

    TickCooldown(enemyHitCooldown_, gameplayDeltaTime);
    TickCooldown(playerHitCooldown_, gameplayDeltaTime);

    const bool isEnemySmashCounterWindow =
        (enemyActionKind == ActionKind::Smash &&
         enemyActionStep == ActionStep::Active);
    const bool isEnemySweepCounterWindow =
        (enemyActionKind == ActionKind::Sweep &&
         enemyActionStep == ActionStep::Active);
    const bool isEnemySmashMeleeWindow =
        (enemyActionKind == ActionKind::Smash &&
         (enemyActionStep == ActionStep::Active ||
          enemyActionStep == ActionStep::Recovery));
    const bool isEnemySweepMeleeWindow =
        (enemyActionKind == ActionKind::Sweep &&
         (enemyActionStep == ActionStep::Active ||
          enemyActionStep == ActionStep::Recovery));
    const bool isEnemyMeleeActive =
        isEnemySmashMeleeWindow || isEnemySweepMeleeWindow;

    const float enemyAttackDamage = enemy_.GetCurrentAttackDamage();
    const float enemyAttackKnockback = enemy_.GetCurrentAttackKnockback();
    const bool isEnemyCounterWindow =
        isEnemySmashCounterWindow || isEnemySweepCounterWindow;
    OBB enemyAttackBox{};
    CollisionManager::BodyId enemyAttackBody =
        CollisionManager::kInvalidBodyId;
    if (isEnemyMeleeActive) {
        enemyAttackBox = enemy_.GetAttackOBB();
        enemyAttackBody =
            AddCollisionBody(collisionManager_, enemyAttackBox,
                             kLayerEnemyAttack,
                             kLayerPlayer | kLayerPlayerCounter);
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
        const SwordCounterAxis slashCounterAxis = sword->GetSlashCounterAxis();
        const bool slashAxisMatches =
            (isEnemySmashCounterWindow &&
             slashCounterAxis == SwordCounterAxis::Vertical) ||
            (isEnemySweepCounterWindow &&
             slashCounterAxis == SwordCounterAxis::Horizontal);
        const bool canSlashCounter =
            playerHitCooldown_ <= 0.0f && isEnemyCounterWindow &&
            slashAxisMatches &&
            enemyAttackBody != CollisionManager::kInvalidBodyId &&
            collisionManager_.Test(swordHitBody, enemyAttackBody);

        if (canSlashCounter) {
            triggerSuccessfulCounter(i, enemyAttackDamage, 0.2f);
            counterTriggeredThisFrame = true;
            break;
        }

        const bool hitBody = collisionManager_.Test(swordHitBody, enemyBody);

        if (enemyHitCooldown_ <= 0.0f) {
            if (hitBody) {
                const float swordDamage = swordAttackDamages[i];
                enemy_.TakeDamage(swordDamage);
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
            collisionManager_.Test(enemyAttackBody, playerBody);

        if (bossHitPlayer && playerHitCooldown_ <= 0.0f) {
            const XMFLOAT2 knockbackDir = NormalizeXZ(
                player_.GetTransform().position.x -
                    enemy_.GetTransform().position.x,
                player_.GetTransform().position.z -
                    enemy_.GetTransform().position.z);

            if (isPlayerGuarding) {
                enemy_.NotifyAttackGuarded();
                player_.TakeDamage(enemyAttackDamage * guardDamageMultiplier);
                player_.AddKnockback(
                    {knockbackDir.x * (enemyAttackKnockback * 0.5f), 0.0f,
                     knockbackDir.y * (enemyAttackKnockback * 0.5f)});
                CombatFeedbackEvent feedback{};
                feedback.type = CombatFeedbackEventType::PlayerGuard;
                feedback.position = player_.GetTransform().position;
                feedback.position.y += 1.0f;
                feedback.direction =
                    DirectionFromTo(enemy_.GetTransform().position,
                                    player_.GetTransform().position);
                feedback.power = enemyAttackDamage / 10.0f;
                DispatchCombatFeedback(feedback);
                playerHitCooldown_ = 0.2f;
            } else {
                enemy_.NotifyAttackConnected();
                player_.TakeDamage(enemyAttackDamage);
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

    const auto &bullets = enemy_.GetBullets();
    for (size_t i = 0; i < bullets.size(); ++i) {
        const auto &bullet = bullets[i];
        if (!bullet.isAlive) {
            continue;
        }

        OBB bulletBox{};
        bulletBox.center = bullet.position;
        bulletBox.size = enemy_.GetBulletHitBoxSize();
        bulletBox.rotation = player_.GetTransform().rotation;
        const CollisionManager::BodyId bulletBody = AddCollisionBody(
            collisionManager_, bulletBox,
            bullet.isReflected ? kLayerReflectedProjectile
                               : kLayerEnemyProjectile,
            bullet.isReflected ? kLayerEnemy
                               : (kLayerPlayer | kLayerPlayerCounter));

        if (!bullet.isReflected && isPlayerCountering &&
            isCounterBodyHit(bulletBody)) {
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
            if (collisionManager_.Test(bulletBody, enemyBody) &&
                enemyHitCooldown_ <= 0.0f) {
                const float damage = enemy_.GetBulletDamage() * damageMultiplier_;
                enemy_.TakeDamage(damage);
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

        if (collisionManager_.Test(bulletBody, playerBody)) {
            if (playerHitCooldown_ <= 0.0f) {
                const XMFLOAT2 hitDir =
                    NormalizeXZ(bullet.velocity.x, bullet.velocity.z);

                if (player_.IsCounterStance()) {
                    triggerSuccessfulCounter(1, enemy_.GetBulletDamage() * 2.0f,
                                             0.12f);
                } else if (isPlayerGuarding) {
                    player_.TakeDamage(enemy_.GetBulletDamage() *
                                       guardDamageMultiplier);
                    player_.AddKnockback(
                        {hitDir.x * (enemy_.GetBulletKnockback() * 0.5f),
                         0.0f,
                         hitDir.y * (enemy_.GetBulletKnockback() * 0.5f)});
                    CombatFeedbackEvent feedback{};
                    feedback.type = CombatFeedbackEventType::PlayerGuard;
                    feedback.position = bullet.position;
                    feedback.direction = {hitDir.x, 0.0f, hitDir.y};
                    feedback.power = enemy_.GetBulletDamage() / 5.0f;
                    DispatchCombatFeedback(feedback);
                    enemy_.DestroyBullet(i);
                    playerHitCooldown_ = 0.15f;
                } else {
                    player_.TakeDamage(enemy_.GetBulletDamage());
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
        waveBox.rotation = player_.GetTransform().rotation;
        const CollisionManager::BodyId waveBody = AddCollisionBody(
            collisionManager_, waveBox,
            wave.isReflected ? kLayerReflectedProjectile
                             : kLayerEnemyProjectile,
            wave.isReflected ? kLayerEnemy
                             : (kLayerPlayer | kLayerPlayerCounter));

        if (!wave.isReflected && isPlayerCountering &&
            isCounterBodyHit(waveBody)) {
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
            if (collisionManager_.Test(waveBody, enemyBody) &&
                enemyHitCooldown_ <= 0.0f) {
                const float damage = enemy_.GetWaveDamage() * damageMultiplier_;
                enemy_.TakeDamage(damage);
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

        if (collisionManager_.Test(waveBody, playerBody)) {
            if (playerHitCooldown_ <= 0.0f) {
                const XMFLOAT2 hitDir =
                    NormalizeXZ(wave.direction.x, wave.direction.z);

                if (player_.IsCounterStance()) {
                    triggerSuccessfulCounter(1, enemy_.GetWaveDamage() * 2.0f,
                                             0.12f);
                } else if (isPlayerGuarding) {
                    player_.TakeDamage(enemy_.GetWaveDamage() *
                                       guardDamageMultiplier);
                    player_.AddKnockback(
                        {hitDir.x * (enemy_.GetWaveKnockback() * 0.5f), 0.0f,
                         hitDir.y * (enemy_.GetWaveKnockback() * 0.5f)});
                    CombatFeedbackEvent feedback{};
                    feedback.type = CombatFeedbackEventType::PlayerGuard;
                    feedback.position = wave.position;
                    feedback.direction = {hitDir.x, 0.0f, hitDir.y};
                    feedback.power = enemy_.GetWaveDamage() / 5.0f;
                    DispatchCombatFeedback(feedback);
                    enemy_.DestroyWave(i);
                    playerHitCooldown_ = 0.15f;
                } else {
                    player_.TakeDamage(enemy_.GetWaveDamage());
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

    if (startCounterCinematicThisFrame) {
        counterCinematicActive_ = true;
        SetEnemyAnimationFrozen(true);
    }
    if (stopCounterCinematicThisFrame) {
        counterCinematicActive_ = false;
        SetEnemyAnimationFrozen(false);
    }
    if (forceSyncEnemyAnimationThisFrame) {
        SyncEnemyAnimation();
        SetEnemyAnimationFrozen(true);
    }
}
