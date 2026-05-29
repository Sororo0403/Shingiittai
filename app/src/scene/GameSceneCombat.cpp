#include "GameScene.h"
#include "BladeClashCinematic.h"
#include "compat/ParticleCompat.h"
#include <algorithm>
#include <array>
#include <cmath>

using namespace DirectX;

static constexpr float kMinVectorLength = 0.0001f;

namespace {
namespace Clash = BladeClashCinematic;

constexpr CollisionManager::LayerMask kLayerPlayer = 1u << 0;
constexpr CollisionManager::LayerMask kLayerEnemy = 1u << 1;
constexpr CollisionManager::LayerMask kLayerPlayerAttack = 1u << 2;
constexpr CollisionManager::LayerMask kLayerEnemyAttack = 1u << 3;
constexpr float kArcaneProjectilePlayerHitRange = 0.82f;
constexpr float kArcaneProjectileEnemyHitRange = 1.45f;
constexpr int kArcaneProjectileVolleyRequiredHits = 1;
constexpr float kNormalSlashRearmDelay = 0.10f;

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

static float DistancePointToSegmentSqXZ(const XMFLOAT3 &point,
                                        const XMFLOAT3 &segmentStart,
                                        const XMFLOAT3 &segmentEnd) {
    const float sx = segmentEnd.x - segmentStart.x;
    const float sz = segmentEnd.z - segmentStart.z;
    const float lenSq = sx * sx + sz * sz;
    if (lenSq < kMinVectorLength) {
        return DistanceSqXZ(point, segmentStart);
    }

    const float px = point.x - segmentStart.x;
    const float pz = point.z - segmentStart.z;
    const float t = std::clamp((px * sx + pz * sz) / lenSq, 0.0f, 1.0f);
    const XMFLOAT3 closest{segmentStart.x + sx * t, point.y,
                           segmentStart.z + sz * t};
    return DistanceSqXZ(point, closest);
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
    case ActionKind::BladeClash:
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

static XMFLOAT3 Lerp3(const XMFLOAT3 &from, const XMFLOAT3 &to, float alpha) {
    return {from.x + (to.x - from.x) * alpha,
            from.y + (to.y - from.y) * alpha,
            from.z + (to.z - from.z) * alpha};
}

static float SmoothStep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
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

float GameScene::ApplyEnemyDamage(float damage, bool deferTransitions,
                                  bool triggerHitReaction) {
    if (damage <= 0.0f || enemy_.GetHP() <= 0.0f) {
        return 0.0f;
    }

    const float actualDamage = triggerHitReaction
                                   ? (deferTransitions
                                          ? enemy_.TakeDamageDeferTransitions(
                                                damage)
                                          : enemy_.TakeDamage(damage))
                                   : (deferTransitions
                                          ? enemy_
                                                .TakeDamageDeferTransitionsNoReaction(
                                                    damage)
                                          : enemy_.TakeDamageNoReaction(damage));
    return actualDamage;
}

float GameScene::ApplyPlayerDamage(float enemyAttackDamage) {
    if (enemyAttackDamage <= 0.0f || player_.GetHP() <= 0.0f) {
        return 0.0f;
    }

    const float difficultyRatio = GetDifficultyRatio();
    const float lethalityScale = 0.45f + 1.55f * difficultyRatio;
    return player_.TakeDamage(enemyAttackDamage * lethalityScale);
}

void GameScene::BeginBladeClash(size_t swordIndex) {
    if (bladeClashActive_) {
        return;
    }

    const XMFLOAT3 playerPos = player_.GetTransform().position;
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const XMFLOAT2 toEnemy =
        NormalizeXZ(enemyPos.x - playerPos.x, enemyPos.z - playerPos.z);
    bladeClashDirection_ = {toEnemy.x, 0.0f, toEnemy.y};
    bladeClashPlayerWinPos_ = {enemyPos.x - toEnemy.x * 1.25f, playerPos.y,
                               enemyPos.z - toEnemy.y * 1.25f};
    bladeClashPlayerLosePos_ = {enemyPos.x - toEnemy.x * 2.36f, playerPos.y,
                                enemyPos.z - toEnemy.y * 2.36f};
    bladeClashPlayerFixedPos_ = {enemyPos.x - toEnemy.x * 1.86f, playerPos.y,
                                 enemyPos.z - toEnemy.y * 1.86f};
    bladeClashCenter_ = {enemyPos.x - toEnemy.x * 1.42f,
                         playerPos.y + 1.10f,
                         enemyPos.z - toEnemy.y * 1.42f};
    bladeClashGauge_ = 0.0f;
    bladeClashTimer_ = bladeClashDuration_;
    const float difficultyRatio = GetDifficultyRatio();
    bladeClashEnemyPushSpeed_ = 0.16f + 0.76f * difficultyRatio;
    bladeClashCameraPush_ = 0.0f;
    bladeClashImpactPulse_ = 0.18f;
    bladeClashEnemySurgeTimer_ = 0.0f;
    bladeClashChainTimer_ = 0.0f;
    bladeClashSlashChain_ = 0;
    bladeClashPreviousSlashStates_ = player_.GetSwordSlashStates();
    bladeClashActive_ = true;
    player_.LockPosition(bladeClashPlayerFixedPos_);
    player_.SetBladeClashPose(true, 0.5f);
    counterCinematicActive_ = false;
    counterCinematicTimer_ = 0.0f;
    SetEnemyAnimationFrozen(false);
    SyncEnemyAnimation();

    CombatFeedbackEvent feedback{};
    feedback.type = CombatFeedbackEventType::CounterSuccess;
    feedback.position = bladeClashCenter_;
    feedback.direction = bladeClashDirection_;
    feedback.power = 7.0f;
    feedback.swordIndex = swordIndex;
    DispatchCombatFeedback(feedback);

    EmitParticleBurst(sparkParticles_, bladeClashCenter_, 22, 0.16f,
                      AppParticleBurstStyle::Sparks,
                      {0.90f, 0.98f, 1.0f, 0.60f}, bladeClashDirection_,
                      1.05f);
}

void GameScene::UpdateBladeClash(float gameplayDeltaTime) {
    if (!bladeClashActive_) {
        return;
    }

    const auto swords = player_.GetSwords();
    const auto slashStates = player_.GetSwordSlashStates();
    bool slashLanded = false;
    for (size_t i = 0; i < slashStates.size(); ++i) {
        const bool slashStarted =
            slashStates[i] && !bladeClashPreviousSlashStates_[i];
        bladeClashPreviousSlashStates_[i] = slashStates[i];
        if (!slashStarted || swords[i] == nullptr) {
            continue;
        }

        slashLanded = true;
        if (bladeClashChainTimer_ > 0.0f) {
            bladeClashSlashChain_ = (std::min)(bladeClashSlashChain_ + 1, 5);
        } else {
            bladeClashSlashChain_ = 1;
        }
        bladeClashChainTimer_ = 0.42f;
        const float chainBonus =
            static_cast<float>((std::max)(bladeClashSlashChain_ - 1, 0)) *
            0.020f;
        bladeClashGauge_ += bladeClashSlashPush_ + chainBonus;
        bladeClashCameraPush_ =
            (std::min)(bladeClashCameraPush_ + 0.64f, 1.0f);
        bladeClashImpactPulse_ = 1.0f;

        CombatFeedbackEvent feedback{};
        feedback.type = CombatFeedbackEventType::PlayerSlashHit;
        feedback.position = bladeClashCenter_;
        feedback.position.y += 0.14f;
        feedback.direction = bladeClashDirection_;
        feedback.power = 5.0f + bladeClashGauge_ * 4.0f;
        feedback.swordIndex = i;
        DispatchCombatFeedback(feedback);

        XMFLOAT3 sparkPos = bladeClashCenter_;
        sparkPos.x += bladeClashDirection_.x * 0.10f;
        sparkPos.y += 0.14f;
        sparkPos.z += bladeClashDirection_.z * 0.10f;
        EmitParticleBurst(sparkParticles_, sparkPos, 16, 0.11f,
                          AppParticleBurstStyle::Sparks,
                          {0.90f, 0.98f, 1.0f, 0.62f},
                          bladeClashDirection_, 1.08f);
    }

    if (bladeClashChainTimer_ > 0.0f) {
        bladeClashChainTimer_ =
            (std::max)(0.0f, bladeClashChainTimer_ - gameplayDeltaTime);
    } else {
        bladeClashSlashChain_ = 0;
    }

    bladeClashEnemySurgeTimer_ += gameplayDeltaTime;
    if (bladeClashEnemySurgeTimer_ >= 0.62f) {
        bladeClashEnemySurgeTimer_ -= 0.62f;
        bladeClashImpactPulse_ = (std::max)(bladeClashImpactPulse_, 0.48f);
        XMFLOAT3 surgeSpark = bladeClashCenter_;
        surgeSpark.x -= bladeClashDirection_.x * 0.12f;
        surgeSpark.y += 0.10f;
        surgeSpark.z -= bladeClashDirection_.z * 0.12f;
        EmitParticleBurst(sparkParticles_, surgeSpark, 8, 0.08f,
                          AppParticleBurstStyle::Sparks,
                          {1.0f, 0.48f, 0.18f, 0.42f},
                          {-bladeClashDirection_.x, 0.0f,
                           -bladeClashDirection_.z},
                          0.64f);
    }

    const float elapsedRatio =
        bladeClashDuration_ > 0.0001f
            ? std::clamp(1.0f - bladeClashTimer_ / bladeClashDuration_, 0.0f,
                         1.0f)
            : 1.0f;
    const float enemySurge =
        0.78f + 0.22f * std::sinf(bladeClashEnemySurgeTimer_ * 10.1f);
    const float lowTimePressure =
        bladeClashTimer_ < 1.35f ? (1.35f - bladeClashTimer_) * 0.26f : 0.0f;
    bladeClashGauge_ -=
        (bladeClashEnemyPushSpeed_ + elapsedRatio * 0.08f + lowTimePressure) *
        enemySurge * gameplayDeltaTime;
    bladeClashGauge_ = std::clamp(bladeClashGauge_, -1.1f, 1.1f);
    bladeClashCameraPush_ =
        (std::max)(0.0f, bladeClashCameraPush_ - gameplayDeltaTime * 3.1f);
    bladeClashImpactPulse_ =
        (std::max)(0.0f, bladeClashImpactPulse_ -
                             gameplayDeltaTime * (slashLanded ? 2.2f : 3.0f));
    bladeClashTimer_ -= gameplayDeltaTime;

    const float progress = SmoothStep01((bladeClashGauge_ + 1.0f) * 0.5f);
    bladeClashPlayerFixedPos_ =
        Lerp3(bladeClashPlayerLosePos_, bladeClashPlayerWinPos_, progress);
    bladeClashCenter_ = {
        bladeClashPlayerFixedPos_.x +
            bladeClashDirection_.x * (0.74f + 0.22f * progress),
        bladeClashPlayerFixedPos_.y + 1.10f + 0.04f * bladeClashImpactPulse_,
        bladeClashPlayerFixedPos_.z +
            bladeClashDirection_.z * (0.74f + 0.22f * progress)};
    player_.LockPosition(bladeClashPlayerFixedPos_);
    player_.SetBladeClashPose(true, progress);

    if (bladeClashGauge_ >= 1.0f) {
        FinishBladeClash(true);
        return;
    }
    if (bladeClashGauge_ <= -1.0f || bladeClashTimer_ <= 0.0f) {
        FinishBladeClash(false);
    }
}

void GameScene::FinishBladeClash(bool playerWon) {
    if (!bladeClashActive_) {
        return;
    }

    bladeClashActive_ = false;
    bladeClashTimer_ = 0.0f;
    bladeClashPreviousSlashStates_.fill(false);
    bladeClashCameraPush_ = 0.0f;
    bladeClashImpactPulse_ = 0.0f;
    bladeClashEnemySurgeTimer_ = 0.0f;
    bladeClashChainTimer_ = 0.0f;
    bladeClashSlashChain_ = 0;
    bladeClashFinishActive_ = true;
    bladeClashFinishPlayerWon_ = playerWon;
    bladeClashFinishImpactEmitted_ = false;
    bladeClashFinishSkidEmitted_ = false;
    bladeClashFinishGuardBreakEmitted_ = false;
    bladeClashFinishWallImpactEmitted_ = false;
    bladeClashFinishPendingEnemyTransition_ = false;
    bladeClashFinishTimer_ = 0.0f;
    bladeClashFinishDuration_ = playerWon ? 2.05f : 2.38f;
    bladeClashFinishCenter_ =
        playerWon ? XMFLOAT3{enemy_.GetTransform().position.x,
                             enemy_.GetTransform().position.y + 1.22f,
                             enemy_.GetTransform().position.z}
                  : XMFLOAT3{(player_.GetTransform().position.x +
                              enemy_.GetTransform().position.x) *
                                 0.5f,
                             player_.GetTransform().position.y + 1.10f,
                             (player_.GetTransform().position.z +
                              enemy_.GetTransform().position.z) *
                                 0.5f};
    bladeClashFinishPlayerStart_ = player_.GetTransform().position;
    bladeClashFinishEnemyStart_ = enemy_.GetTransform().position;
    bladeClashFinishPlayerEnd_ =
        playerWon ? XMFLOAT3{enemy_.GetTransform().position.x +
                                 bladeClashDirection_.x * 8.45f,
                             player_.GetTransform().position.y,
                             enemy_.GetTransform().position.z +
                                 bladeClashDirection_.z * 8.45f}
                  : XMFLOAT3{player_.GetTransform().position.x -
                                 bladeClashDirection_.x *
                                     Clash::kLossTotalRetreat,
                             player_.GetTransform().position.y,
                             player_.GetTransform().position.z -
                                 bladeClashDirection_.z *
                                     Clash::kLossTotalRetreat};
    player_.SetBladeClashPose(!playerWon, 0.0f);
    enemy_.ResolveBladeClash(playerWon);
    SetEnemyAnimationFrozen(false);

    if (playerWon) {
        const float appliedDamage = ApplyEnemyDamage(300.0f, true);
        bladeClashFinishPendingEnemyTransition_ = appliedDamage > 0.0f;
        CombatFeedbackEvent feedback{};
        feedback.type = CombatFeedbackEventType::CounterSuccess;
        feedback.position = enemy_.GetTransform().position;
        feedback.position.y += 1.05f;
        feedback.direction = bladeClashDirection_;
        feedback.power = (std::max)(appliedDamage / 10.0f, 10.0f);
        DispatchCombatFeedback(feedback);
        enemyHitCooldown_ = 0.24f;
        playerHitCooldown_ = 0.25f;
        return;
    }

    enemy_.NotifyBladeClashLanded();
    const float bladeClashDamage =
        ApplyPlayerDamage(12.0f + 4.0f * GetDifficultyRatio());
    XMFLOAT3 warningCenter = player_.GetTransform().position;
    warningCenter.y += 1.16f;
    EmitParticleBurst(sparkParticles_, warningCenter, 18, 0.24f,
                      AppParticleBurstStyle::Sparks,
                      {1.0f, 0.50f, 0.18f, 0.42f},
                      {bladeClashDirection_.z, 0.04f,
                       -bladeClashDirection_.x},
                      0.66f);
    CombatFeedbackEvent feedback{};
    feedback.type = CombatFeedbackEventType::PlayerDamaged;
    feedback.position = player_.GetTransform().position;
    feedback.position.y += 1.0f;
    feedback.direction = {-bladeClashDirection_.x, 0.0f,
                          -bladeClashDirection_.z};
    feedback.power = (std::max)(bladeClashDamage / 8.0f, 7.5f);
    DispatchCombatFeedback(feedback);
    playerHitCooldown_ = 0.55f;
}

void GameScene::UpdateCombat(float gameplayDeltaTime) {
    if (bladeClashActive_) {
        UpdateBladeClash(gameplayDeltaTime);
        previousCombatSlashStates_ = player_.GetSwordSlashStates();
        return;
    }

    collisionManager_.Clear();

    const auto playerBox = player_.GetOBB();
    const auto enemyBodyBox = enemy_.GetBodyOBB();
    const auto enemyLeftHandBox = enemy_.GetLeftHandOBB();
    const auto enemyRightHandBox = enemy_.GetRightHandOBB();
    const CollisionManager::BodyId playerBody =
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
    for (size_t i = 0; i < swordSlashStates.size(); ++i) {
        if (swordSlashStates[i]) {
            normalSlashRearmTimers_[i] = 0.0f;
        } else {
            normalSlashRearmTimers_[i] += gameplayDeltaTime;
        }
        if (normalSlashRearmTimers_[i] >= kNormalSlashRearmDelay) {
            normalSlashHitConsumed_[i] = false;
        }
    }
    bool startCounterCinematicThisFrame = false;
    bool stopCounterCinematicThisFrame = false;
    bool forceSyncEnemyAnimationThisFrame = false;
    auto triggerSuccessfulCounter = [&](size_t swordIndex, float enemyDamage,
                                        float hitCooldown) {
        const float counterDamage =
            (std::max)(enemyDamage * player_.GetCounterDamageMultiplier(),
                       130.0f);
        const float vulnerabilityDuration = GetCounterVulnerabilityDuration();
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
        playerHitCooldown_ = GetCounterPlayerHitCooldown(hitCooldown);
        startCounterCinematicThisFrame = true;
        counterCinematicTimer_ = GetCounterCinematicDuration();
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
    const bool isEnemyBladeClashCommitted =
        (enemyActionKind == ActionKind::BladeClash &&
         (enemyActionStep == ActionStep::Charge ||
          enemyActionStep == ActionStep::Active));
    const bool isEnemySmashMeleeWindow =
        isEnemySmashCommitted && enemy_.IsAttackActive();
    const bool isEnemySweepMeleeWindow =
        isEnemySweepCommitted && enemy_.IsAttackActive();
    const bool isEnemyBladeClashMeleeWindow =
        enemyActionKind == ActionKind::BladeClash &&
        enemyActionStep == ActionStep::Active && enemy_.IsBladeClashWindow();
    const bool isEnemyBladeClashCounterWindow =
        isEnemyBladeClashCommitted;
    const bool isEnemyMeleeActive =
        isEnemySmashMeleeWindow || isEnemySweepMeleeWindow ||
        isEnemyBladeClashMeleeWindow;
    if (!isEnemyMeleeActive) {
        enemyMeleeHitConsumed_ = false;
    }
    const bool isEnemyMeleeCommitted =
        isEnemySmashCommitted || isEnemySweepCommitted ||
        isEnemyBladeClashCommitted;
    const bool isEnemyLaserCommitted =
        (enemyActionKind == ActionKind::ArcaneLaser ||
         enemyActionKind == ActionKind::CataclysmLaser) &&
        enemyActionStep == ActionStep::Active;
    const bool isEnemyBeamActive =
        enemyActionKind == ActionKind::CataclysmLaser &&
        enemyActionStep == ActionStep::Active && enemy_.IsAttackActive();
    if (!isEnemyBeamActive) {
        enemyLaserHitConsumed_ = false;
    }
    if (enemyRedPunishUncounterable_ &&
        (!(enemyActionKind == ActionKind::Smash ||
           enemyActionKind == ActionKind::Sweep) ||
         enemyActionStep == ActionStep::Recovery ||
         enemyActionStep == ActionStep::None)) {
        enemyRedPunishUncounterable_ = false;
    }
    const bool isFarWarpSlashCommit = enemy_.IsFarWarpSlashActive();
    const bool isEnemyMeleePreparationOrRelease =
        (enemyActionKind == ActionKind::Smash ||
         enemyActionKind == ActionKind::Sweep) &&
        (enemyActionStep == ActionStep::Charge ||
         enemyActionStep == ActionStep::Hold ||
         enemyActionStep == ActionStep::Active);
    const bool isPreReleaseCounterWindow =
        !isFarWarpSlashCommit &&
        (enemyActionKind == ActionKind::Smash ||
         enemyActionKind == ActionKind::Sweep) &&
        isEnemyMeleePreparationOrRelease &&
        enemyActionStep != ActionStep::Active &&
        enemy_.GetReleaseAnticipationRatio() > 0.0f;
    const bool isReleaseCounterWindow =
        !enemyRedPunishUncounterable_ && isPreReleaseCounterWindow;
    const float enemyAttackDamage = enemy_.GetCurrentAttackDamage();
    const float enemyAttackKnockback = enemy_.GetCurrentAttackKnockback();
    const bool isEnemyCounterWindow = isReleaseCounterWindow;
    OBB enemyAttackBox{};
    CollisionManager::BodyId enemyAttackBody =
        CollisionManager::kInvalidBodyId;
    if (isEnemyMeleeCommitted || isPreReleaseCounterWindow ||
        isEnemyLaserCommitted) {
        enemyAttackBox = enemy_.GetAttackOBB();
        enemyAttackBody =
            AddCollisionBody(collisionManager_, enemyAttackBox,
                             kLayerEnemyAttack,
                             kLayerPlayer | kLayerPlayerAttack);
    }

    const bool enemyMeleeDamagePending =
        isEnemyMeleeActive &&
        enemyAttackBody != CollisionManager::kInvalidBodyId &&
        IsNearXZ(player_.GetTransform().position, enemyAttackBox.center,
                 GetReadableMeleeRadius(enemyAttackBox)) &&
        playerHitCooldown_ <= 0.0f && !enemyMeleeHitConsumed_ &&
        !enemyRedPunishUncounterable_;
    const bool enemyLaserDamagePending =
        isEnemyBeamActive &&
        enemyAttackBody != CollisionManager::kInvalidBodyId &&
        playerBody != CollisionManager::kInvalidBodyId &&
        collisionManager_.Test(enemyAttackBody, playerBody) &&
        playerHitCooldown_ <= 0.0f && !enemyLaserHitConsumed_;

    const bool isEnemyMeleePreparation =
        (enemyActionKind == ActionKind::Smash ||
         enemyActionKind == ActionKind::Sweep) &&
        (enemyActionStep == ActionStep::Charge ||
         enemyActionStep == ActionStep::Hold);
    const bool isBadSlashPunishWindow =
        isEnemyMeleePreparation &&
        !isFarWarpSlashCommit &&
        !isReleaseCounterWindow &&
        !enemyRedPunishUncounterable_;
    if (isBadSlashPunishWindow && playerHitCooldown_ <= 0.0f) {
        for (size_t i = 0; i < swordSlashStates.size(); ++i) {
            if (!swordSlashStates[i] || previousCombatSlashStates_[i]) {
                continue;
            }
            const Sword *sword = swords[i];
            if (sword == nullptr || !sword->CanSlashCounter()) {
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
            const float appliedDamage = ApplyPlayerDamage(enemyAttackDamage);

            CombatFeedbackEvent feedback{};
            feedback.type = CombatFeedbackEventType::PlayerDamaged;
            feedback.position = player_.GetTransform().position;
            feedback.position.y += 1.0f;
            feedback.direction =
                DirectionFromTo(enemy_.GetTransform().position,
                                player_.GetTransform().position);
            feedback.power = (std::max)(appliedDamage / 8.0f,
                                        enemyAttackDamage / 8.0f);
            DispatchCombatFeedback(feedback);
            playerHitCooldown_ = 0.45f;
            break;
        }
    }

    bool counterTriggeredThisFrame = false;
    bool arcaneProjectileReflectedThisFrame = false;
    for (size_t i = 0; i < swords.size(); ++i) {
        const Sword *sword = swords[i];
        if (sword == nullptr || !swordSlashStates[i]) {
            continue;
        }
        const auto swordHitBox = sword->GetOBB();
        const auto swordHitSamples = sword->GetOBBSamples();
        bool hitBody = false;
        for (const OBB &sampleBox : swordHitSamples) {
            const CollisionManager::BodyId swordHitBody =
                AddCollisionBody(collisionManager_, sampleBox,
                                 kLayerPlayerAttack,
                                 kLayerEnemy | kLayerEnemyAttack);
            hitBody = isEnemyHurtBodyHit(swordHitBody);
            if (hitBody) {
                break;
            }
        }
        const bool canSlashCounter =
            isReleaseCounterWindow &&
            playerHitCooldown_ <= 0.0f && isEnemyCounterWindow &&
            enemyAttackBody != CollisionManager::kInvalidBodyId &&
            IsNearXZ(player_.GetTransform().position, enemyAttackBox.center,
                     GetReadableMeleeRadius(enemyAttackBox) + 0.85f) &&
            IsSlashAxisMatched(*sword,
                               RequiredCounterAxisForAction(enemyActionKind));
        const bool canBladeClashCounter =
            isEnemyBladeClashCounterWindow && playerHitCooldown_ <= 0.0f &&
            enemyAttackBody != CollisionManager::kInvalidBodyId &&
            (IsNearXZ(player_.GetTransform().position, enemyAttackBox.center,
                      GetReadableMeleeRadius(enemyAttackBox) + 1.75f) ||
             IsNearXZ(player_.GetTransform().position,
                      enemy_.GetTransform().position, 4.15f)) &&
            IsSlashAxisMatched(*sword,
                               RequiredCounterAxisForAction(enemyActionKind));

        if (canBladeClashCounter) {
            BeginBladeClash(i);
            counterTriggeredThisFrame = true;
            break;
        }

        if (canSlashCounter) {
            triggerSuccessfulCounter(i, enemyAttackDamage, 0.2f);
            counterTriggeredThisFrame = true;
            break;
        }

        const bool projectileSlashStarted =
            swordSlashStates[i] && !previousCombatSlashStates_[i];
        const bool canReflectArcaneProjectile =
            arcaneProjectile_.active && !arcaneProjectile_.reflected &&
            projectileSlashStarted && playerHitCooldown_ <= 0.0f &&
            IsArcaneProjectileInDeflectRange() &&
            IsArcaneProjectileSlashAligned(*sword);
        if (canReflectArcaneProjectile) {
            ReflectArcaneProjectile(i);
            playerHitCooldown_ = 0.14f;
            arcaneProjectileReflectedThisFrame = true;
            break;
        }

        if (enemyHitCooldown_ <= 0.0f && !normalSlashHitConsumed_[i]) {
            if (hitBody) {
                const float swordDamage = swordAttackDamages[i];
                const float appliedDamage =
                    ApplyEnemyDamage(swordDamage, false, false);
                if (appliedDamage <= 0.0f) {
                    break;
                }
                normalSlashHitConsumed_[i] = true;
                CombatFeedbackEvent feedback{};
                feedback.type = CombatFeedbackEventType::PlayerSlashHit;
                feedback.position = swordHitBox.center;
                feedback.direction =
                    DirectionFromTo(player_.GetTransform().position,
                                    enemy_.GetTransform().position);
                feedback.power = appliedDamage / 10.0f;
                feedback.swordIndex = i;
                DispatchCombatFeedback(feedback);
                enemyHitCooldown_ = GetEnemyNormalHitCooldown();
                if (counterCinematicActive_) {
                    stopCounterCinematicThisFrame = true;
                }
            }
        }

        if (enemyHitCooldown_ > 0.0f) {
            break;
        }
    }

    if (arcaneProjectile_.active && arcaneProjectile_.reflected &&
        enemyHitCooldown_ <= 0.0f) {
        const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
        if (DistanceSqXZ(arcaneProjectile_.position, enemyPos) <=
            kArcaneProjectileEnemyHitRange * kArcaneProjectileEnemyHitRange) {
            const XMFLOAT3 impact = arcaneProjectile_.position;
            const size_t swordIndex = arcaneProjectile_.reflectedBySwordIndex;
            const float damage = arcaneProjectile_.damage;
            ++arcaneProjectileVolleyReflectedHits_;
            const bool volleyComplete = arcaneProjectileVolleyReflectedHits_ >=
                                        kArcaneProjectileVolleyRequiredHits;
            ResetArcaneProjectile();
            if (volleyComplete) {
                triggerSuccessfulCounter(swordIndex, damage * 1.45f, 0.22f);
                arcaneProjectileVolleyActive_ = false;
                counterTriggeredThisFrame = true;
            } else {
                const float appliedDamage =
                    ApplyEnemyDamage((std::max)(damage * 0.75f, 28.0f));
                CombatFeedbackEvent feedback{};
                feedback.type = CombatFeedbackEventType::CounterSuccess;
                feedback.position = enemy_.GetTransform().position;
                feedback.position.y += 1.0f;
                feedback.direction =
                    DirectionFromTo(impact, enemy_.GetTransform().position);
                feedback.power = (std::max)(appliedDamage / 10.0f, 4.0f);
                feedback.swordIndex = swordIndex;
                DispatchCombatFeedback(feedback);
                enemyHitCooldown_ = 0.10f;
            }
            EmitParticleBurst(explosionParticles_, impact, 190, 0.62f,
                              AppParticleBurstStyle::Explosion,
                              {0.24f, 1.0f, 0.68f, 0.90f},
                              DirectionFromTo(impact,
                                              enemy_.GetTransform().position),
                              2.65f);
        }
    }

    if (arcaneProjectile_.active && !arcaneProjectile_.reflected &&
        !arcaneProjectileReflectedThisFrame &&
        playerHitCooldown_ <= 0.0f) {
        const bool playerHitByProjectile =
            DistanceSqXZ(player_.GetTransform().position,
                         arcaneProjectile_.position) <=
            kArcaneProjectilePlayerHitRange * kArcaneProjectilePlayerHitRange;
        if (playerHitByProjectile) {
            const XMFLOAT3 projectileVelocity = arcaneProjectile_.velocity;
            const float projectileDamage = arcaneProjectile_.damage;
            const float projectileKnockback = arcaneProjectile_.knockback;
            ResetArcaneProjectile();
            const XMFLOAT2 knockbackDir =
                NormalizeXZ(projectileVelocity.x, projectileVelocity.z);
            player_.AddKnockback({knockbackDir.x * projectileKnockback, 0.0f,
                                  knockbackDir.y * projectileKnockback});
            const float appliedDamage = ApplyPlayerDamage(projectileDamage);
            CombatFeedbackEvent feedback{};
            feedback.type = CombatFeedbackEventType::PlayerDamaged;
            feedback.position = player_.GetTransform().position;
            feedback.position.y += 1.0f;
            feedback.direction = {projectileVelocity.x, 0.0f,
                                  projectileVelocity.z};
            feedback.power = (std::max)(appliedDamage / 8.0f,
                                        projectileDamage / 8.0f);
            DispatchCombatFeedback(feedback);
            playerHitCooldown_ = 0.52f;
        }
    }

    if (isEnemyMeleeActive && !counterTriggeredThisFrame) {
        if (enemyMeleeDamagePending) {
            enemyMeleeHitConsumed_ = true;
            const XMFLOAT2 knockbackDir = NormalizeXZ(
                player_.GetTransform().position.x -
                    enemy_.GetTransform().position.x,
                player_.GetTransform().position.z -
                    enemy_.GetTransform().position.z);

            player_.AddKnockback(
                {knockbackDir.x * enemyAttackKnockback, 0.0f,
                 knockbackDir.y * enemyAttackKnockback});
            const float appliedDamage = ApplyPlayerDamage(enemyAttackDamage);
            CombatFeedbackEvent feedback{};
            feedback.type = CombatFeedbackEventType::PlayerDamaged;
            feedback.position = player_.GetTransform().position;
            feedback.position.y += 1.0f;
            feedback.direction =
                DirectionFromTo(enemy_.GetTransform().position,
                                player_.GetTransform().position);
            feedback.power = (std::max)(appliedDamage / 10.0f,
                                        enemyAttackDamage / 10.0f);
            DispatchCombatFeedback(feedback);
            playerHitCooldown_ = 0.4f;
        }
    }

    if (enemyLaserDamagePending && !counterTriggeredThisFrame) {
        enemyLaserHitConsumed_ = true;
        const XMFLOAT3 beamDir = enemy_.GetCataclysmLaserDirection();
        const XMFLOAT2 knockbackDir = NormalizeXZ(beamDir.x, beamDir.z);
        player_.AddKnockback(
            {knockbackDir.x * enemyAttackKnockback, 0.0f,
             knockbackDir.y * enemyAttackKnockback});
        const float appliedDamage = ApplyPlayerDamage(enemyAttackDamage);
        CombatFeedbackEvent feedback{};
        feedback.type = CombatFeedbackEventType::PlayerDamaged;
        feedback.position = player_.GetTransform().position;
        feedback.position.y += 1.0f;
        feedback.direction = beamDir;
        feedback.power =
            (std::max)(appliedDamage / 8.0f, enemyAttackDamage / 7.0f);
        DispatchCombatFeedback(feedback);
        playerHitCooldown_ = 0.72f;
    }

    previousCombatSlashStates_ = swordSlashStates;

    if (startCounterCinematicThisFrame) {
        counterCinematicActive_ = true;
        counterCinematicTimer_ = GetCounterCinematicDuration();
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
