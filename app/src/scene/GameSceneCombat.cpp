#include "GameScene.h"
#include "CollisionUtil.h"
#include <cmath>

using namespace DirectX;

static constexpr float kMinVectorLength = 0.0001f;

static XMFLOAT2 NormalizeXZ(float x, float z) {
    float length = std::sqrt(x * x + z * z);
    if (length < kMinVectorLength) {
        length = 1.0f;
    }

    return {x / length, z / length};
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

void GameScene::UpdateCombat(float gameplayDeltaTime) {
    auto counterBox = player_.GetSword().GetCounterOBB();
    auto playerBox = player_.GetOBB();
    const bool isPlayerGuarding = player_.IsGuarding();
    const bool isPlayerCountering = player_.GetSword().IsSlashMode();
    const auto enemyBodyBox = enemy_.GetBodyOBB();
    const ActionKind enemyActionKind = enemy_.GetActionKind();
    const ActionStep enemyActionStep = enemy_.GetActionStep();
    bool startCounterCinematicThisFrame = false;
    bool stopCounterCinematicThisFrame = false;
    bool forceSyncEnemyAnimationThisFrame = false;
    auto triggerSuccessfulCounter = [&](float enemyDamage, float hitCooldown) {
        player_.NotifyCounterSuccess();
        if (enemy_.NotifyCountered()) {
            forceSyncEnemyAnimationThisFrame = true;
        }
        enemy_.TakeDamage(enemyDamage);
        playerHitCooldown_ = hitCooldown;
        startCounterCinematicThisFrame = true;
    };

    TickCooldown(enemyHitCooldown_, gameplayDeltaTime);
    TickCooldown(playerHitCooldown_, gameplayDeltaTime);

    const auto swords = player_.GetSwords();
    const auto swordSlashStates = player_.GetSwordSlashStates();

    for (size_t i = 0; i < swords.size(); ++i) {
        const Sword *sword = swords[i];
        if (sword == nullptr || !swordSlashStates[i]) {
            continue;
        }

        auto swordHitBox = sword->GetOBB();
        auto bodyBox = enemy_.GetBodyOBB();

        const bool hitBody = CollisionUtil::CheckOBB(swordHitBox, bodyBox);

        if (enemyHitCooldown_ <= 0.0f) {
            if (hitBody) {
                enemy_.TakeDamage(10.0f);
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

    const bool isEnemyCounterWindow =
        (enemyActionKind == ActionKind::Melee &&
         enemyActionStep == ActionStep::Active);
    const bool isEnemyMeleeActive =
        (enemyActionKind == ActionKind::Melee &&
         (enemyActionStep == ActionStep::Active ||
          enemyActionStep == ActionStep::Recovery));

    const float enemyAttackDamage = enemy_.GetCurrentAttackDamage();
    const float enemyAttackKnockback = enemy_.GetCurrentAttackKnockback();
    const bool isCounterAxisMatch =
        isEnemyCounterWindow &&
        (player_.GetCounterAxis() == SwordCounterAxis::Vertical ||
         player_.GetCounterAxis() == SwordCounterAxis::Horizontal);
    const bool canCounterThisHit =
        player_.IsCounterStance() && isCounterAxisMatch;

    if (isEnemyMeleeActive) {
        auto enemyAttackBox = enemy_.GetAttackOBB();
        const bool bossHitPlayer =
            CollisionUtil::CheckOBB(enemyAttackBox, playerBox);

        if (bossHitPlayer && playerHitCooldown_ <= 0.0f) {
            const XMFLOAT2 knockbackDir = NormalizeXZ(
                player_.GetTransform().position.x -
                    enemy_.GetTransform().position.x,
                player_.GetTransform().position.z -
                    enemy_.GetTransform().position.z);

            if (canCounterThisHit) {
                triggerSuccessfulCounter(enemyAttackDamage, 0.2f);
            } else if (isPlayerGuarding) {
                player_.TakeDamage(enemyAttackDamage * kGuardDamageMultiplier);
                player_.AddKnockback(
                    {knockbackDir.x * (enemyAttackKnockback * 0.5f), 0.0f,
                     knockbackDir.y * (enemyAttackKnockback * 0.5f)});
                playerHitCooldown_ = 0.2f;
            } else {
                player_.TakeDamage(enemyAttackDamage);
                player_.AddKnockback(
                    {knockbackDir.x * enemyAttackKnockback, 0.0f,
                     knockbackDir.y * enemyAttackKnockback});
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

        if (!bullet.isReflected && isPlayerCountering &&
            CollisionUtil::CheckOBB(bulletBox, counterBox)) {
            enemy_.ReflectBullet(i, enemy_.GetTransform().position);
            continue;
        }

        if (bullet.isReflected) {
            if (CollisionUtil::CheckOBB(bulletBox, enemyBodyBox) &&
                enemyHitCooldown_ <= 0.0f) {
                const float damage = enemy_.GetBulletDamage() * damageMultiplier_;
                enemy_.TakeDamage(damage);
                enemy_.DestroyBullet(i);
                enemyHitCooldown_ = 0.2f;
            }
            continue;
        }

        if (CollisionUtil::CheckOBB(bulletBox, playerBox)) {
            if (playerHitCooldown_ <= 0.0f) {
                const XMFLOAT2 hitDir =
                    NormalizeXZ(bullet.velocity.x, bullet.velocity.z);

                if (player_.IsCounterStance()) {
                    triggerSuccessfulCounter(enemy_.GetBulletDamage() * 2.0f,
                                             0.12f);
                } else if (isPlayerGuarding) {
                    player_.TakeDamage(enemy_.GetBulletDamage() *
                                       kGuardDamageMultiplier);
                    player_.AddKnockback(
                        {hitDir.x * (enemy_.GetBulletKnockback() * 0.5f),
                         0.0f,
                         hitDir.y * (enemy_.GetBulletKnockback() * 0.5f)});
                    enemy_.DestroyBullet(i);
                    playerHitCooldown_ = 0.15f;
                } else {
                    player_.TakeDamage(enemy_.GetBulletDamage());
                    player_.AddKnockback(
                        {hitDir.x * enemy_.GetBulletKnockback(), 0.0f,
                         hitDir.y * enemy_.GetBulletKnockback()});
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

        if (!wave.isReflected && isPlayerCountering &&
            CollisionUtil::CheckOBB(waveBox, counterBox)) {
            enemy_.ReflectWave(i, enemy_.GetTransform().position);
            continue;
        }

        if (wave.isReflected) {
            if (CollisionUtil::CheckOBB(waveBox, enemyBodyBox) &&
                enemyHitCooldown_ <= 0.0f) {
                const float damage = enemy_.GetWaveDamage() * damageMultiplier_;
                enemy_.TakeDamage(damage);
                enemy_.DestroyWave(i);
                enemyHitCooldown_ = 0.2f;
            }
            continue;
        }

        if (CollisionUtil::CheckOBB(waveBox, playerBox)) {
            if (playerHitCooldown_ <= 0.0f) {
                const XMFLOAT2 hitDir =
                    NormalizeXZ(wave.direction.x, wave.direction.z);

                if (player_.IsCounterStance()) {
                    triggerSuccessfulCounter(enemy_.GetWaveDamage() * 2.0f,
                                             0.12f);
                } else if (isPlayerGuarding) {
                    player_.TakeDamage(enemy_.GetWaveDamage() *
                                       kGuardDamageMultiplier);
                    player_.AddKnockback(
                        {hitDir.x * (enemy_.GetWaveKnockback() * 0.5f), 0.0f,
                         hitDir.y * (enemy_.GetWaveKnockback() * 0.5f)});
                    enemy_.DestroyWave(i);
                    playerHitCooldown_ = 0.15f;
                } else {
                    player_.TakeDamage(enemy_.GetWaveDamage());
                    player_.AddKnockback(
                        {hitDir.x * enemy_.GetWaveKnockback(), 0.0f,
                         hitDir.y * enemy_.GetWaveKnockback()});
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
