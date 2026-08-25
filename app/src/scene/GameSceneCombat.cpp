#include "GameScene.h"
#include "AppSceneServices.h"
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
constexpr float kArcaneProjectileDeflectRange = 5.80f;
constexpr float kCataclysmProjectileDeflectHeight = 13.8f;
constexpr float kArcaneProjectileSlashDot = 0.55f;
constexpr int kArcaneProjectileVolleyRequiredHits = 3;
constexpr int kCataclysmProjectileVolleyRequiredHits = 5;
constexpr float kReflectedProjectileVolleyDamage = 130.0f;
constexpr float kHostileProjectilePlayerDamageScale = 0.55f;
constexpr float kHostileProjectilePlayerKnockbackScale = 0.75f;
constexpr float kNormalSlashRearmDelay = 0.10f;
constexpr float kNormalHitSlashSoundVolume = 0.82f;
constexpr float kNormalHitSlashSoundStartSeconds = 0.18f;
constexpr float kFarSlashCounterFlashDuration = 0.50f;
constexpr float kTripleIaiCounterDamageScale = 1.0f / 3.0f;

CollisionManager::BodyId AddCollisionBody(
    CollisionManager &collisionManager, const OBB &box,
    CollisionManager::LayerMask layer, CollisionManager::LayerMask mask) {
    CollisionManager::BodyDesc desc{};
    desc.shape = CollisionManager::Shape::FromOBB(box);
    desc.filter.layer = layer;
    desc.filter.mask = mask;
    return collisionManager.AddBody(desc);
}

bool IsBasicSlashAction(ActionKind kind) {
    return kind == ActionKind::Smash || kind == ActionKind::Sweep;
}

bool IsCounterPreparationStep(ActionStep step) {
    return step == ActionStep::Charge || step == ActionStep::Hold ||
           step == ActionStep::Active;
}

bool ShouldClearRedPunish(ActionKind kind, ActionStep step) {
    return !IsBasicSlashAction(kind) || step == ActionStep::Recovery ||
           step == ActionStep::None;
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
#ifdef _DEBUG
    if (debugPlayerInvincible_) {
        return 0.0f;
    }
#endif
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
    player_.SetHandPostSlashCooldownEnabled(false);
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
    player_.SetHandPostSlashCooldownEnabled(true);
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

    CombatFrameContext combat{};
    InitializeCombatFrame(gameplayDeltaTime, combat);
    ConfigureCombatWindows(combat);
    HandleBadSlashPunish(combat);
    ProcessSwordAttacks(combat);

    ProcessReflectedProjectileHit(combat, arcaneProjectile_);
    for (ArcaneProjectileState &projectile : cataclysmProjectiles_) {
        if (ProcessReflectedProjectileHit(combat, projectile)) {
            break;
        }
    }
    ProcessHostileProjectileHit(combat, arcaneProjectile_);
    for (ArcaneProjectileState &projectile : cataclysmProjectiles_) {
        if (ProcessHostileProjectileHit(combat, projectile)) {
            break;
        }
    }

    ResolveEnemyMeleeDamage(combat);
    FinishCombatFrame(combat);
}

void GameScene::InitializeCombatFrame(
    float gameplayDeltaTime, CombatFrameContext &combat) {
    collisionManager_.Clear();
    const bool collisionDisabled = enemy_.IsWarpCollisionDisabled();
    const std::array<OBB, 3> hurtBoxes{
        enemy_.GetBodyOBB(), enemy_.GetLeftHandOBB(),
        enemy_.GetRightHandOBB()};
    for (size_t i = 0; i < hurtBoxes.size(); ++i) {
        combat.enemyHurtBodies[i] =
            collisionDisabled
                ? CollisionManager::kInvalidBodyId
                : AddCollisionBody(collisionManager_, hurtBoxes[i],
                                   kLayerEnemy, kLayerPlayerAttack);
    }
    combat.enemyActionKind = enemy_.GetActionKind();
    combat.enemyActionStep = enemy_.GetActionStep();
    combat.swords = player_.GetSwords();
    combat.swordSlashStates = player_.GetSwordSlashStates();
    combat.swordAttackDamages = player_.GetSwordAttackDamages();
    for (size_t i = 0; i < combat.swordSlashStates.size(); ++i) {
        if (combat.swordSlashStates[i]) {
            normalSlashRearmTimers_[i] = 0.0f;
        } else {
            normalSlashRearmTimers_[i] += gameplayDeltaTime;
        }
        if (normalSlashRearmTimers_[i] >= kNormalSlashRearmDelay) {
            normalSlashHitConsumed_[i] = false;
        }
    }
    TickCooldown(enemyHitCooldown_, gameplayDeltaTime);
    TickCooldown(playerHitCooldown_, gameplayDeltaTime);
}

void GameScene::ConfigureCombatWindows(CombatFrameContext &combat) {
    ConfigureMeleeCombatWindows(combat);
    const bool preReleaseCounterWindow =
        ConfigureCounterCombatWindows(combat);
    const bool laserCommitted =
        combat.enemyActionKind == ActionKind::ArcaneLaser &&
        combat.enemyActionStep == ActionStep::Active;
    ConfigureEnemyAttackCollision(
        combat, combat.enemyAttackCommitted ||
                    preReleaseCounterWindow || laserCommitted);
}

void GameScene::ConfigureMeleeCombatWindows(
    CombatFrameContext &combat) {
    const bool smashCommitted =
        combat.enemyActionKind == ActionKind::Smash &&
        combat.enemyActionStep == ActionStep::Active;
    const bool sweepCommitted =
        combat.enemyActionKind == ActionKind::Sweep &&
        combat.enemyActionStep == ActionStep::Active;
    const bool bladeClashCommitted =
        combat.enemyActionKind == ActionKind::BladeClash &&
        (combat.enemyActionStep == ActionStep::Charge ||
         combat.enemyActionStep == ActionStep::Active);
    combat.enemyAttackCommitted =
        smashCommitted || sweepCommitted || bladeClashCommitted;
    combat.enemyBladeClashCounterWindow = bladeClashCommitted;
    const bool bladeClashWindow =
        combat.enemyActionKind == ActionKind::BladeClash &&
        combat.enemyActionStep == ActionStep::Active &&
        enemy_.IsBladeClashWindow();
    combat.enemyMeleeActive =
        (smashCommitted && enemy_.IsAttackActive()) ||
        (sweepCommitted && enemy_.IsAttackActive()) ||
        bladeClashWindow;
    if (!combat.enemyMeleeActive) {
        enemyMeleeHitConsumed_ = false;
    }
    enemyLaserHitConsumed_ = false;
}

bool GameScene::ConfigureCounterCombatWindows(
    CombatFrameContext &combat) {
    if (enemyRedPunishUncounterable_ &&
        ShouldClearRedPunish(combat.enemyActionKind,
                             combat.enemyActionStep)) {
        enemyRedPunishUncounterable_ = false;
    }
    const bool meleePreparationOrRelease =
        IsBasicSlashAction(combat.enemyActionKind) &&
        IsCounterPreparationStep(combat.enemyActionStep);
    const bool preReleaseCounterWindow =
        meleePreparationOrRelease && !enemy_.IsFarWarpSlashActive() &&
        combat.enemyActionStep != ActionStep::Active &&
        enemy_.GetReleaseAnticipationRatio() > 0.0f;
    const bool farWarpDashCounterWindow =
        !enemyRedPunishUncounterable_ && enemy_.IsFarWarpSlashActive() &&
        IsBasicSlashAction(combat.enemyActionKind) &&
        combat.enemyActionStep == ActionStep::Active &&
        enemy_.GetActionTimerForPresentation() <=
            kFarSlashCounterFlashDuration;
    combat.enemyCounterWindow =
        (!enemyRedPunishUncounterable_ && preReleaseCounterWindow) ||
        farWarpDashCounterWindow;
    return preReleaseCounterWindow;
}

void GameScene::ConfigureEnemyAttackCollision(
    CombatFrameContext &combat, bool attackCommitted) {
    combat.enemyAttackDamage = enemy_.GetCurrentAttackDamage();
    combat.enemyAttackKnockback = enemy_.GetCurrentAttackKnockback();
    if (attackCommitted) {
        combat.enemyAttackBox = enemy_.GetAttackOBB();
        combat.enemyAttackBody = AddCollisionBody(
            collisionManager_, combat.enemyAttackBox, kLayerEnemyAttack,
            kLayerPlayer | kLayerPlayerAttack);
    }
    combat.enemyMeleeDamagePending =
        combat.enemyMeleeActive &&
        combat.enemyAttackBody != CollisionManager::kInvalidBodyId &&
        IsNearXZ(player_.GetTransform().position,
                 combat.enemyAttackBox.center,
                 GetReadableMeleeRadius(combat.enemyAttackBox)) &&
        playerHitCooldown_ <= 0.0f && !enemyMeleeHitConsumed_ &&
        !enemyRedPunishUncounterable_;
}

bool GameScene::IsEnemyHurtBodyHit(
    const CombatFrameContext &combat,
    CollisionManager::BodyId attackBody) const {
    return std::ranges::any_of(
        combat.enemyHurtBodies,
        [&](CollisionManager::BodyId targetBody) {
            return targetBody != CollisionManager::kInvalidBodyId &&
                   collisionManager_.Test(attackBody, targetBody);
        });
}

bool GameScene::IsProjectileInDeflectRange(
    const ArcaneProjectileState &projectile) const {
    if (!projectile.active || projectile.reflected ||
        (projectile.waitingToFire && !projectile.cataclysm)) {
        return false;
    }
    const XMFLOAT3 playerPos = player_.GetTransform().position;
    if (projectile.fromAbove &&
        projectile.position.y >
            playerPos.y + kCataclysmProjectileDeflectHeight) {
        return false;
    }
    return DistanceSqXZ(projectile.position, playerPos) <=
           kArcaneProjectileDeflectRange * kArcaneProjectileDeflectRange;
}

bool GameScene::IsProjectileSlashAligned(
    const ArcaneProjectileState &projectile, const Sword &sword) const {
    if (!sword.CanSlashCounter()) {
        return false;
    }
    const XMFLOAT2 slashDir = sword.GetSlashDirection();
    const float slashLenSq =
        slashDir.x * slashDir.x + slashDir.y * slashDir.y;
    if (slashLenSq < 0.010f) {
        return false;
    }
    const float invSlashLen = 1.0f / std::sqrt(slashLenSq);
    const float dot = (slashDir.x * invSlashLen) * projectile.cueDirection.x +
                      (slashDir.y * invSlashLen) * projectile.cueDirection.y;
    return projectile.cataclysm ? dot >= kArcaneProjectileSlashDot
                                : std::fabs(dot) >=
                                      kArcaneProjectileSlashDot;
}

void GameScene::TriggerSuccessfulCounter(
    CombatFrameContext &combat, size_t swordIndex, float enemyDamage,
    float hitCooldown) {
    const bool tripleIaiCounter =
        enemy_.IsTripleIaiSlashActive() && enemy_.IsFarWarpSlashActive() &&
        IsBasicSlashAction(combat.enemyActionKind);
    const float counterDamage =
        (std::max)(enemyDamage * player_.GetCounterDamageMultiplier(),
                   130.0f) *
        (tripleIaiCounter ? kTripleIaiCounterDamageScale : 1.0f);
    const float vulnerabilityDuration = GetCounterVulnerabilityDuration();
    const bool suppressCounterStagger = enemy_.ShouldSuppressCounterStagger();
    if (enemy_.IsFarWarpSlashActive() && !suppressCounterStagger) {
        const XMFLOAT3 start = enemy_.GetTransform().position;
        const XMFLOAT3 playerPos = player_.GetTransform().position;
        const XMFLOAT2 rushDir =
            NormalizeXZ(playerPos.x - start.x, playerPos.z - start.z);
        const float stopDistance = 1.45f;
        const XMFLOAT3 target{playerPos.x - rushDir.x * stopDistance,
                              playerPos.y,
                              playerPos.z - rushDir.y * stopDistance};
        const float rushYaw = std::atan2(rushDir.x, rushDir.y);
        XMFLOAT3 mid = Lerp3(start, target, 0.50f);
        mid.y += 1.00f;
        const XMFLOAT3 rushParticleDir{rushDir.x, 0.06f, rushDir.y};
        for (int p = 0; p < 4; ++p) {
            const float t = static_cast<float>(p + 1) / 5.0f;
            XMFLOAT3 trail = Lerp3(start, target, t);
            trail.y += 1.0f;
            EmitParticleBurst(sparkParticles_, trail, 34, 0.16f,
                              AppParticleBurstStyle::SlashLine,
                              {1.0f, 0.94f, 0.62f, 0.78f}, rushParticleDir,
                              1.15f + 0.25f * static_cast<float>(p));
            EmitParticleBurst(smokeParticles_, trail, 14, 0.22f,
                              AppParticleBurstStyle::SpiritSparkle,
                              {1.0f, 0.90f, 0.56f, 0.38f}, rushParticleDir,
                              0.72f);
        }
        EmitParticleBurst(swordFlashParticles_, mid, 18, 0.20f,
                          AppParticleBurstStyle::Flash,
                          {1.0f, 0.98f, 0.72f, 0.74f}, rushParticleDir,
                          0.45f);
        enemy_.SetCinematicTransform(target, rushYaw);
    }
    if (enemy_.NotifyCountered(vulnerabilityDuration)) {
        combat.forceSyncEnemyAnimation = true;
    }
    const float appliedDamage =
        ApplyEnemyDamage(counterDamage, false, !suppressCounterStagger);
    CombatFeedbackEvent feedback{};
    feedback.type = CombatFeedbackEventType::CounterSuccess;
    feedback.position = enemy_.GetTransform().position;
    feedback.position.y += 1.0f;
    feedback.direction = DirectionFromTo(player_.GetTransform().position,
                                         enemy_.GetTransform().position);
    feedback.power = appliedDamage / 10.0f;
    feedback.swordIndex = swordIndex;
    DispatchCombatFeedback(feedback);
    playerHitCooldown_ = GetCounterPlayerHitCooldown(hitCooldown);
    combat.startCounterCinematic = true;
    counterCinematicTimer_ = GetCounterCinematicDuration();
}

void GameScene::HandleBadSlashPunish(CombatFrameContext &combat) {
    const bool meleePreparation =
        IsBasicSlashAction(combat.enemyActionKind) &&
        (combat.enemyActionStep == ActionStep::Charge ||
         combat.enemyActionStep == ActionStep::Hold);
    const bool badSlashWindow =
        meleePreparation && !enemy_.IsFarWarpSlashActive() &&
        !combat.enemyCounterWindow && !enemyRedPunishUncounterable_;
    if (!badSlashWindow || playerHitCooldown_ > 0.0f) {
        return;
    }
    for (size_t i = 0; i < combat.swordSlashStates.size(); ++i) {
        if (!combat.swordSlashStates[i] || previousCombatSlashStates_[i]) {
            continue;
        }
        const Sword *sword = combat.swords[i];
        if (sword == nullptr || !sword->CanSlashCounter()) {
            continue;
        }
        enemy_.ForcePunishRelease();
        enemyRedPunishUncounterable_ = true;
        combat.forceSyncEnemyAnimation = true;
        const XMFLOAT2 knockbackDir = NormalizeXZ(
            player_.GetTransform().position.x -
                enemy_.GetTransform().position.x,
            player_.GetTransform().position.z -
                enemy_.GetTransform().position.z);
        player_.AddKnockback(
            {knockbackDir.x * combat.enemyAttackKnockback, 0.0f,
             knockbackDir.y * combat.enemyAttackKnockback});
        const float appliedDamage =
            ApplyPlayerDamage(combat.enemyAttackDamage);
        CombatFeedbackEvent feedback{};
        feedback.type = CombatFeedbackEventType::MistimedCounterSlash;
        feedback.position = sword->GetOBB().center;
        feedback.direction = DirectionFromTo(player_.GetTransform().position,
                                             enemy_.GetTransform().position);
        feedback.power = (std::max)(appliedDamage / 8.0f,
                                    combat.enemyAttackDamage / 8.0f);
        feedback.swordIndex = i;
        mistimedCounterSlashThisFrame_ = true;
        normalSlashHitConsumed_[i] = true;
        DispatchCombatFeedback(feedback);
        playerHitCooldown_ = 0.45f;
        break;
    }
}

void GameScene::ProcessSwordAttacks(CombatFrameContext &combat) {
    for (size_t i = 0; i < combat.swords.size(); ++i) {
        if (ProcessSwordAttack(combat, i)) {
            break;
        }
    }
}

bool GameScene::ProcessSwordAttack(
    CombatFrameContext &combat, size_t swordIndex) {
    const Sword *sword = combat.swords[swordIndex];
    if (sword == nullptr || !combat.swordSlashStates[swordIndex]) {
        return false;
    }
    bool hitBody = false;
    for (const OBB &sampleBox : sword->GetOBBSamples()) {
        const CollisionManager::BodyId swordHitBody = AddCollisionBody(
            collisionManager_, sampleBox, kLayerPlayerAttack,
            kLayerEnemy | kLayerEnemyAttack);
        if (IsEnemyHurtBodyHit(combat, swordHitBody)) {
            hitBody = true;
            break;
        }
    }
    if (TrySwordCounter(combat, swordIndex, *sword) ||
        TryReflectProjectiles(combat, swordIndex, *sword)) {
        return true;
    }
    if (TryNormalSwordHit(combat, swordIndex, *sword, hitBody)) {
        return true;
    }
    return enemyHitCooldown_ > 0.0f;
}

bool GameScene::TrySwordCounter(
    CombatFrameContext &combat, size_t swordIndex, const Sword &sword) {
    const float counterDistanceBonus =
        enemy_.IsFarWarpSlashActive() ? 100.0f : 0.0f;
    const bool attackBodyValid =
        combat.enemyAttackBody != CollisionManager::kInvalidBodyId;
    const bool axisMatched = IsSlashAxisMatched(
        sword, RequiredCounterAxisForAction(combat.enemyActionKind));
    const bool canSlashCounter =
        combat.enemyCounterWindow && playerHitCooldown_ <= 0.0f &&
        attackBodyValid &&
        IsNearXZ(player_.GetTransform().position,
                 combat.enemyAttackBox.center,
                 GetReadableMeleeRadius(combat.enemyAttackBox) + 0.85f +
                     counterDistanceBonus) &&
        axisMatched;
    const bool bladeClashInRange =
        IsNearXZ(player_.GetTransform().position,
                 combat.enemyAttackBox.center,
                 GetReadableMeleeRadius(combat.enemyAttackBox) + 1.75f) ||
        IsNearXZ(player_.GetTransform().position,
                 enemy_.GetTransform().position, 4.15f);
    const bool canBladeClashCounter =
        combat.enemyBladeClashCounterWindow &&
        playerHitCooldown_ <= 0.0f && attackBodyValid &&
        bladeClashInRange && axisMatched;
    if (canBladeClashCounter) {
        BeginBladeClash(swordIndex);
        combat.counterTriggered = true;
        return true;
    }
    if (canSlashCounter) {
        TriggerSuccessfulCounter(combat, swordIndex,
                                 combat.enemyAttackDamage, 0.2f);
        combat.counterTriggered = true;
        return true;
    }
    return false;
}

bool GameScene::TryReflectProjectiles(
    CombatFrameContext &combat, size_t swordIndex, const Sword &sword) {
    const bool slashStarted = combat.swordSlashStates[swordIndex] &&
                              !previousCombatSlashStates_[swordIndex];
    if (TryReflectProjectile(combat, swordIndex, sword,
                             arcaneProjectile_, slashStarted)) {
        return true;
    }
    for (ArcaneProjectileState &projectile : cataclysmProjectiles_) {
        if (TryReflectProjectile(combat, swordIndex, sword,
                                 projectile, slashStarted)) {
            return true;
        }
    }
    return false;
}

bool GameScene::TryReflectProjectile(
    CombatFrameContext &combat, size_t swordIndex, const Sword &sword,
    ArcaneProjectileState &projectile, bool slashStarted) {
    const bool cueSlash = combat.swordSlashStates[swordIndex] &&
                          IsProjectileSlashAligned(projectile, sword);
    const bool canReflect =
        projectile.active && !projectile.reflected &&
        (slashStarted || cueSlash) && playerHitCooldown_ <= 0.0f &&
        IsProjectileInDeflectRange(projectile) &&
        IsProjectileSlashAligned(projectile, sword);
    if (!canReflect) {
        return false;
    }
    ReflectArcaneProjectile(projectile, swordIndex);
    playerHitCooldown_ = 0.14f;
    combat.projectileReflected = true;
    return true;
}

bool GameScene::TryNormalSwordHit(
    CombatFrameContext &combat, size_t swordIndex, const Sword &sword,
    bool hitBody) {
    const bool canHit = !mistimedCounterSlashThisFrame_ &&
                        !counterSuccessSlashThisFrame_ &&
                        enemyHitCooldown_ <= 0.0f &&
                        !normalSlashHitConsumed_[swordIndex] && hitBody;
    if (!canHit) {
        return false;
    }
    const float appliedDamage = ApplyEnemyDamage(
        combat.swordAttackDamages[swordIndex], false, false);
    if (appliedDamage <= 0.0f) {
        return true;
    }
    normalSlashHitConsumed_[swordIndex] = true;
    CombatFeedbackEvent feedback{};
    feedback.type = CombatFeedbackEventType::PlayerSlashHit;
    feedback.position = sword.GetOBB().center;
    feedback.direction = DirectionFromTo(player_.GetTransform().position,
                                         enemy_.GetTransform().position);
    feedback.power = appliedDamage / 10.0f;
    feedback.swordIndex = swordIndex;
    DispatchCombatFeedback(feedback);
    if (soundsLoaded_ && ctx_ != nullptr &&
        ctx_->systems.sound != nullptr) {
        ctx_->systems.sound->PlayFrom(
            normalHitSlashSoundId_, kNormalHitSlashSoundStartSeconds,
            kNormalHitSlashSoundVolume * AppSceneServices::GetSeVolume());
    }
    enemyHitCooldown_ = GetEnemyNormalHitCooldown();
    if (counterCinematicActive_) {
        combat.stopCounterCinematic = true;
    }
    return true;
}

bool GameScene::ProcessReflectedProjectileHit(
    CombatFrameContext &combat, ArcaneProjectileState &projectile) {
    if (!projectile.active || !projectile.reflected ||
        enemyHitCooldown_ > 0.0f) {
        return false;
    }
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    const float hitRangeSq =
        kArcaneProjectileEnemyHitRange * kArcaneProjectileEnemyHitRange;
    const bool hit =
        DistanceSqXZ(projectile.position, enemyPos) <= hitRangeSq ||
        DistancePointToSegmentSqXZ(enemyPos, projectile.previousPosition,
                                   projectile.position) <= hitRangeSq;
    if (!hit) {
        return false;
    }
    ++arcaneProjectileVolleyReflectedHits_;
    const int requiredHits =
        arcaneProjectileVolleyCataclysm_
            ? kCataclysmProjectileVolleyRequiredHits
            : kArcaneProjectileVolleyRequiredHits;
    const bool volleyComplete =
        arcaneProjectileVolleyReflectedHits_ >= requiredHits;
    projectile = {};
    const float reflectedDamage =
        kReflectedProjectileVolleyDamage /
        static_cast<float>((std::max)(requiredHits, 1));
    ApplyEnemyDamage(reflectedDamage);
    if (volleyComplete) {
        if (enemy_.NotifyCountered(GetCounterVulnerabilityDuration())) {
            combat.forceSyncEnemyAnimation = true;
        }
        arcaneProjectileVolleyActive_ = false;
        arcaneProjectileVolleyCataclysm_ = false;
        combat.counterTriggered = true;
        playerHitCooldown_ = GetCounterPlayerHitCooldown(0.22f);
    } else {
        enemyHitCooldown_ = 0.10f;
    }
    return true;
}

bool GameScene::ProcessHostileProjectileHit(
    CombatFrameContext &combat, ArcaneProjectileState &projectile) {
    if (!projectile.active || projectile.reflected ||
        projectile.waitingToFire || combat.projectileReflected ||
        playerHitCooldown_ > 0.0f) {
        return false;
    }
    const XMFLOAT3 playerPos = player_.GetTransform().position;
    const float horizontalDistanceSq =
        DistanceSqXZ(playerPos, projectile.position);
    const float hitRange = projectile.fromAbove
                               ? kArcaneProjectilePlayerHitRange + 0.22f
                               : kArcaneProjectilePlayerHitRange;
    const bool heightMatched =
        !projectile.fromAbove ||
        std::fabs(projectile.position.y - (playerPos.y + 0.78f)) <= 1.10f;
    if (horizontalDistanceSq > hitRange * hitRange || !heightMatched) {
        return false;
    }
    const XMFLOAT3 impact = projectile.position;
    const XMFLOAT3 velocity = projectile.velocity;
    const XMFLOAT3 impactDirection = NormalizeParticleCompatVec3(
        velocity, {0.0f, 0.0f, 1.0f});
    const float damage =
        projectile.damage * kHostileProjectilePlayerDamageScale;
    const float knockback =
        projectile.knockback * kHostileProjectilePlayerKnockbackScale;
    const ArcaneProjectileState impactProjectile = projectile;
    projectile = {};
    EmitArcaneProjectileExplosion(impactProjectile, impact,
                                  impactDirection, false);
    const XMFLOAT2 knockbackDir = NormalizeXZ(velocity.x, velocity.z);
    player_.AddKnockback({knockbackDir.x * knockback, 0.0f,
                          knockbackDir.y * knockback});
    const float appliedDamage = ApplyPlayerDamage(damage);
    CombatFeedbackEvent feedback{};
    feedback.type = CombatFeedbackEventType::PlayerDamaged;
    feedback.position = playerPos;
    feedback.position.y += 1.0f;
    feedback.direction = {velocity.x, 0.0f, velocity.z};
    feedback.power = (std::max)(appliedDamage / 8.0f, damage / 8.0f);
    DispatchCombatFeedback(feedback);
    playerHitCooldown_ = 0.52f;
    return true;
}

void GameScene::ResolveEnemyMeleeDamage(CombatFrameContext &combat) {
    if (!combat.enemyMeleeActive || combat.counterTriggered ||
        !combat.enemyMeleeDamagePending) {
        return;
    }
    enemyMeleeHitConsumed_ = true;
    const XMFLOAT2 knockbackDir = NormalizeXZ(
        player_.GetTransform().position.x - enemy_.GetTransform().position.x,
        player_.GetTransform().position.z - enemy_.GetTransform().position.z);
    player_.AddKnockback(
        {knockbackDir.x * combat.enemyAttackKnockback, 0.0f,
         knockbackDir.y * combat.enemyAttackKnockback});
    const float appliedDamage = ApplyPlayerDamage(combat.enemyAttackDamage);
    enemy_.NotifyTripleIaiAttackResolvedForCamera();
    CombatFeedbackEvent feedback{};
    feedback.type = CombatFeedbackEventType::PlayerDamaged;
    feedback.position = player_.GetTransform().position;
    feedback.position.y += 1.0f;
    feedback.direction = DirectionFromTo(enemy_.GetTransform().position,
                                         player_.GetTransform().position);
    feedback.power = (std::max)(appliedDamage / 10.0f,
                                combat.enemyAttackDamage / 10.0f);
    DispatchCombatFeedback(feedback);
    playerHitCooldown_ = 0.4f;
}

void GameScene::FinishCombatFrame(CombatFrameContext &combat) {
    previousCombatSlashStates_ = combat.swordSlashStates;
    if (combat.startCounterCinematic) {
        counterCinematicActive_ = true;
        counterCinematicTimer_ = GetCounterCinematicDuration();
        SetEnemyAnimationFrozen(true);
    }
    if (combat.stopCounterCinematic) {
        counterCinematicActive_ = false;
        counterCinematicTimer_ = 0.0f;
        enemy_.FinishCounterRecoil();
        SetEnemyAnimationFrozen(false);
    }
    if (combat.forceSyncEnemyAnimation) {
        SyncEnemyAnimation();
        if (counterCinematicActive_ || combat.startCounterCinematic) {
            SetEnemyAnimationFrozen(true);
        }
    }
}
