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
constexpr float kReleaseCounterWindowDuration = 0.62f;
constexpr float kBladeClashWinGuardBreakLead = 0.52f;
constexpr float kBladeClashWinActionSlow = 0.95f;
constexpr float kBladeClashWinNormalDuration = 1.58f;
constexpr float kBladeClashWinFinalDuration = 2.35f;

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

static XMFLOAT3 Lerp3(const XMFLOAT3 &from, const XMFLOAT3 &to, float t) {
    return {from.x + (to.x - from.x) * t,
            from.y + (to.y - from.y) * t,
            from.z + (to.z - from.z) * t};
}

static float SmoothStep01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static bool IsNearXZ(const XMFLOAT3 &a, const XMFLOAT3 &b, float radius) {
    return DistanceSqXZ(a, b) <= radius * radius;
}

static bool IsChargeWeakPointWindow(ActionKind kind, ActionId id,
                                    ActionStep step) {
    (void)kind;
    (void)id;
    (void)step;
    return false;
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
                                  float minAbsDot = 0.50f) {
    if (!sword.CanSlashCounter()) {
        return false;
    }

    XMFLOAT2 slashDir = sword.GetSlashDirection();
    float slashLenSq = slashDir.x * slashDir.x + slashDir.y * slashDir.y;
    float requiredLenSq = requiredDirection.x * requiredDirection.x +
                          requiredDirection.y * requiredDirection.y;
    if (slashLenSq < 0.0025f || requiredLenSq < 0.01f) {
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

static SwordCounterAxis RequiredCounterAxisForAction(ActionKind kind,
                                                     ActionKind farFollowupKind) {
    switch (kind) {
    case ActionKind::Smash:
        return SwordCounterAxis::Vertical;
    case ActionKind::Sweep:
        return SwordCounterAxis::Horizontal;
    case ActionKind::Laser:
        return farFollowupKind == ActionKind::Sweep ? SwordCounterAxis::Horizontal
                                                    : SwordCounterAxis::Vertical;
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
    observation.facingYaw = player_.GetYaw();
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

float GameScene::ApplyEnemyDamage(float damage, bool deferTransitions,
                                  bool allowLastStand) {
    if (damage <= 0.0f || enemy_.GetHP() <= 0.0f) {
        return 0.0f;
    }

    float appliedDamage = damage;
    const float currentHp = enemy_.GetHP();
    if (allowLastStand && enemyLastStandPrimed_ && currentHp <= 1.0f) {
        return 0.0f;
    }

    if (allowLastStand && !enemyLastStandPrimed_ && currentHp > 1.0f &&
        currentHp - damage <= 0.0f) {
        enemyLastStandPrimed_ = true;
        appliedDamage = currentHp - 1.0f;

        XMFLOAT3 standCenter = enemy_.GetTransform().position;
        standCenter.y += 1.34f;
        swordFlashParticles_.EmitBurst(
            standCenter, 12, 0.72f, GPUParticleSystem::BurstStyle::Flash,
            {1.0f, 0.98f, 0.86f, 0.82f}, {0.0f, 1.0f, 0.0f}, 0.42f);
        explosionParticles_.EmitBurst(
            standCenter, 116, 1.38f,
            GPUParticleSystem::BurstStyle::SpiritSparkle,
            {1.0f, 1.0f, 0.96f, 0.72f}, {0.0f, 1.0f, 0.0f}, 1.52f);
        smokeParticles_.EmitBurst(
            standCenter, 34, 0.86f, GPUParticleSystem::BurstStyle::Smoke,
            {0.42f, 0.34f, 0.28f, 0.40f}, {0.0f, 1.0f, 0.0f}, 0.62f);

        CombatFeedbackEvent feedback{};
        feedback.type = CombatFeedbackEventType::CounterSuccess;
        feedback.position = standCenter;
        feedback.direction = DirectionFromTo(player_.GetTransform().position,
                                             enemy_.GetTransform().position);
        feedback.power = 9.5f;
        DispatchCombatFeedback(feedback);
    }

    if (appliedDamage <= 0.0f) {
        return 0.0f;
    }

    const float actualDamage =
        deferTransitions ? enemy_.TakeDamageDeferTransitions(appliedDamage)
                         : enemy_.TakeDamage(appliedDamage);
    return actualDamage;
}

bool GameScene::TryBeginFinalBladeClash(size_t swordIndex,
                                        const XMFLOAT3 &hitPosition) {
    if (!enemyLastStandPrimed_ || enemy_.GetHP() > 1.0f ||
        bladeClashActive_ || bladeClashFinishActive_ ||
        victorySequenceActive_ || defeatSequenceActive_) {
        return false;
    }

    XMFLOAT3 cue = hitPosition;
    cue.y += 0.22f;
    swordFlashParticles_.EmitBurst(
        cue, 16, 0.58f, GPUParticleSystem::BurstStyle::Flash,
        {1.0f, 0.94f, 0.74f, 0.86f},
        DirectionFromTo(player_.GetTransform().position,
                        enemy_.GetTransform().position),
        0.36f);
    sparkParticles_.EmitBurst(
        cue, 120, 0.72f, GPUParticleSystem::BurstStyle::Sparks,
        {1.0f, 0.62f, 0.18f, 0.86f},
        DirectionFromTo(player_.GetTransform().position,
                        enemy_.GetTransform().position),
        2.20f);
    explosionParticles_.EmitBurst(
        cue, 92, 1.18f, GPUParticleSystem::BurstStyle::SpiritSparkle,
        {1.0f, 1.0f, 0.96f, 0.70f}, {0.0f, 1.0f, 0.0f}, 1.20f);

    BeginBladeClash(swordIndex, true);
    return true;
}

void GameScene::ApplyEnemyCageConstraint(float deltaTime) {
    (void)deltaTime;

    const EnemyCage &cage = enemy_.GetCage();
    const auto slashStates = player_.GetSwordSlashStates();

    auto emitCageBreak = [&](const XMFLOAT3 &position) {
        XMFLOAT3 burstPos = position;
        burstPos.y += 0.95f;
        sparkParticles_.EmitBurst(
            burstPos, 112, 0.26f, GPUParticleSystem::BurstStyle::Sparks,
            {1.0f, 0.88f, 0.38f, 0.92f}, {0.0f, 1.0f, 0.0f}, 1.90f);
        explosionParticles_.EmitBurst(
            burstPos, 80, 0.42f, GPUParticleSystem::BurstStyle::Explosion,
            {0.60f, 1.0f, 0.92f, 0.72f}, {0.0f, 1.0f, 0.0f}, 1.20f);
        smokeParticles_.EmitBurst(
            burstPos, 24, 0.45f, GPUParticleSystem::BurstStyle::Smoke,
            {0.34f, 0.42f, 0.40f, 0.42f}, {0.0f, 1.0f, 0.0f}, 0.58f);
    };

    if (!cage.isActive) {
        if (enemy_.ConsumeCageBreakFlash()) {
            emitCageBreak(cage.center);
        }
        cageSlashPreviousStates_.fill(false);
        return;
    }

    const auto swords = player_.GetSwords();
    const auto swordDamages = player_.GetSwordAttackDamages();
    const float seamX = cage.center.x + std::sin(cage.seamAngle) * cage.radius;
    const float seamZ = cage.center.z + std::cos(cage.seamAngle) * cage.radius;
    XMFLOAT3 seamPoint = {seamX, cage.center.y + cage.height * 0.62f, seamZ};
    bool brokeCage = false;

    for (size_t i = 0; i < slashStates.size(); ++i) {
        const bool slashStarted = slashStates[i] && !cageSlashPreviousStates_[i];
        cageSlashPreviousStates_[i] = slashStates[i];
        if (!slashStarted || swords[i] == nullptr) {
            continue;
        }

        const Sword &sword = *swords[i];
        const XMFLOAT3 &swordPos = sword.GetTransform().position;
        const float swordRadial =
            std::sqrt(DistanceSqXZ(swordPos, cage.center));
        const bool nearBars =
            std::fabs(swordRadial - cage.radius) <= 1.15f ||
            swordRadial >= cage.radius * 0.55f;
        const bool hitSeam =
            IsSlashTowardPoint(sword, seamPoint, -0.04f) &&
            DistanceSqXZ(swordPos, seamPoint) <= 4.2f * 4.2f;

        if (!hitSeam && !nearBars) {
            continue;
        }

        const float cageDamage = hitSeam ? 1.10f : 0.42f;
        brokeCage = enemy_.DamageCage(cageDamage, hitSeam);
        player_.NotifyAttackHit(i, 0.0f);

        CombatFeedbackEvent feedback{};
        feedback.type = hitSeam ? CombatFeedbackEventType::CounterSuccess
                                : CombatFeedbackEventType::PlayerSlashHit;
        feedback.position = hitSeam ? seamPoint : sword.GetOBB().center;
        feedback.direction =
            DirectionFromTo(player_.GetTransform().position, seamPoint);
        feedback.power = hitSeam ? 3.6f : 1.1f;
        feedback.swordIndex = i;
        DispatchCombatFeedback(feedback);

        if (brokeCage) {
            emitCageBreak(cage.center);
            enemy_.ConsumeCageBreakFlash();
            cageSlashPreviousStates_.fill(false);
            break;
        }
    }

    const XMFLOAT3 playerPos = player_.GetTransform().position;
    XMFLOAT2 fromCenter =
        NormalizeXZ(playerPos.x - cage.center.x, playerPos.z - cage.center.z);
    const float distanceFromCenter =
        std::sqrt(DistanceSqXZ(playerPos, cage.center));
    const float maxRadius = (std::max)(0.44f, cage.radius - 0.30f);

    if (distanceFromCenter > maxRadius) {
        XMFLOAT3 clamped = playerPos;
        clamped.x = cage.center.x + fromCenter.x * maxRadius;
        clamped.z = cage.center.z + fromCenter.y * maxRadius;
        player_.SetPosition(clamped);
    }

    if (enemy_.ConsumeCagePulse()) {
        XMFLOAT3 pulsePos = cage.center;
        pulsePos.y += 0.28f;
        explosionParticles_.EmitBurst(
            pulsePos, 44, 0.28f, GPUParticleSystem::BurstStyle::SpiritSparkle,
            {0.40f, 1.0f, 0.92f, 0.48f}, {0.0f, 1.0f, 0.0f}, 0.95f);
    }
}

void GameScene::BeginBladeClash(size_t swordIndex, bool finalClash) {
    if (bladeClashActive_) {
        return;
    }

    const XMFLOAT3 playerPos = player_.GetTransform().position;
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    XMFLOAT2 toEnemy =
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
    bladeClashFinal_ = finalClash;
    bladeClashGauge_ = finalClash ? -0.38f : 0.0f;
    bladeClashTimer_ = finalClash ? 6.15f : bladeClashDuration_;
    bladeClashCameraPush_ = 0.0f;
    bladeClashImpactPulse_ = finalClash ? 0.72f : 0.18f;
    bladeClashEnemySurgeTimer_ = 0.0f;
    bladeClashChainTimer_ = 0.0f;
    bladeClashSlashChain_ = 0;
    bladeClashFinalBarrageStep_ = 0;
    bladeClashPreviousSlashStates_ = player_.GetSwordSlashStates();
    bladeClashActive_ = true;
    enemy_.SetBladeClashPresentationTime(sceneLightTime_);
    player_.LockPosition(bladeClashPlayerFixedPos_);
    player_.SetBladeClashPose(true, 0.5f);
    counterCinematicActive_ = false;
    counterCinematicTimer_ = 0.0f;
    SetEnemyAnimationFrozen(false);
    if (finalClash) {
        enemy_.ForceBladeClash();
    }
    SyncEnemyAnimation();
    player_.NotifyCounterSuccess(swordIndex);

    sparkParticles_.EmitBurst(
        bladeClashCenter_, finalClash ? 90 : 22, finalClash ? 0.64f : 0.16f,
        GPUParticleSystem::BurstStyle::Sparks,
        finalClash ? XMFLOAT4{1.0f, 0.54f, 0.16f, 0.86f}
                   : XMFLOAT4{0.90f, 0.98f, 1.0f, 0.60f},
        bladeClashDirection_, finalClash ? 2.35f : 1.05f);
    if (finalClash) {
        explosionParticles_.EmitBurst(
            bladeClashCenter_, 130, 1.40f,
            GPUParticleSystem::BurstStyle::SpiritSparkle,
            {1.0f, 1.0f, 0.92f, 0.76f}, {0.0f, 1.0f, 0.0f}, 1.75f);
        smokeParticles_.EmitBurst(
            bladeClashCenter_, 44, 1.05f, GPUParticleSystem::BurstStyle::Smoke,
            {0.48f, 0.36f, 0.28f, 0.42f}, {0.0f, 1.0f, 0.0f}, 0.82f);
    }
}

void GameScene::UpdateBladeClash(float deltaTime) {
    if (!bladeClashActive_) {
        return;
    }

    const auto slashStates = player_.GetSwordSlashStates();
    const auto swords = player_.GetSwords();
    bool slashLanded = false;
    for (size_t i = 0; i < slashStates.size(); ++i) {
        const bool slashStarted = slashStates[i] && !bladeClashPreviousSlashStates_[i];
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
            (bladeClashFinal_ ? 0.030f : 0.020f);
        bladeClashGauge_ +=
            (bladeClashFinal_ ? 0.245f : bladeClashSlashPush_) + chainBonus;
        bladeClashCameraPush_ =
            (std::min)(bladeClashCameraPush_ + 0.64f, 1.0f);
        bladeClashImpactPulse_ = 1.0f;
        player_.NotifyAttackHit(i, 0.0f);

        XMFLOAT3 sparkPos = bladeClashCenter_;
        sparkPos.x += bladeClashDirection_.x * 0.10f;
        sparkPos.y += 0.14f;
        sparkPos.z += bladeClashDirection_.z * 0.10f;
        sparkParticles_.EmitBurst(sparkPos, 16, 0.11f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  bladeClashFinal_
                                      ? XMFLOAT4{1.0f, 0.92f, 0.62f, 0.82f}
                                      : XMFLOAT4{0.90f, 0.98f, 1.0f, 0.62f},
                                  bladeClashDirection_,
                                  bladeClashFinal_ ? 1.72f : 1.08f);
    }

    if (bladeClashChainTimer_ > 0.0f) {
        bladeClashChainTimer_ = (std::max)(0.0f, bladeClashChainTimer_ - deltaTime);
    } else {
        bladeClashSlashChain_ = 0;
    }

    bladeClashEnemySurgeTimer_ += deltaTime;
    enemy_.SetBladeClashPresentationTime(sceneLightTime_ + bladeClashEnemySurgeTimer_);
    if (bladeClashEnemySurgeTimer_ >= 0.62f) {
        bladeClashEnemySurgeTimer_ -= 0.62f;
        bladeClashImpactPulse_ = (std::max)(bladeClashImpactPulse_, 0.48f);
        XMFLOAT3 surgeSpark = bladeClashCenter_;
        surgeSpark.x -= bladeClashDirection_.x * 0.12f;
        surgeSpark.y += 0.10f;
        surgeSpark.z -= bladeClashDirection_.z * 0.12f;
        sparkParticles_.EmitBurst(surgeSpark, 8, 0.08f,
                                  GPUParticleSystem::BurstStyle::Sparks,
                                  bladeClashFinal_
                                      ? XMFLOAT4{1.0f, 0.34f, 0.10f, 0.70f}
                                      : XMFLOAT4{1.0f, 0.48f, 0.18f, 0.42f},
                                  {-bladeClashDirection_.x, 0.0f,
                                   -bladeClashDirection_.z},
                                  bladeClashFinal_ ? 1.35f : 0.64f);
    }

    const float elapsedRatio =
        bladeClashDuration_ > 0.0001f
            ? std::clamp(1.0f - bladeClashTimer_ / bladeClashDuration_, 0.0f, 1.0f)
            : 1.0f;
    const float enemySurge =
        (bladeClashFinal_ ? 0.92f : 0.78f) +
        (bladeClashFinal_ ? 0.32f : 0.22f) *
            std::sinf(bladeClashEnemySurgeTimer_ * 10.1f);
    const float lowTimePressure =
        bladeClashTimer_ < 1.35f
            ? (1.35f - bladeClashTimer_) * (bladeClashFinal_ ? 0.42f : 0.26f)
            : 0.0f;
    bladeClashGauge_ -=
        ((bladeClashFinal_ ? 0.345f : bladeClashEnemyPushSpeed_) +
         elapsedRatio * (bladeClashFinal_ ? 0.15f : 0.08f) +
         lowTimePressure) *
        enemySurge * deltaTime;
    bladeClashGauge_ = std::clamp(bladeClashGauge_, -1.1f, 1.1f);
    bladeClashCameraPush_ =
        (std::max)(0.0f, bladeClashCameraPush_ - deltaTime * 3.1f);
    bladeClashImpactPulse_ =
        (std::max)(0.0f, bladeClashImpactPulse_ - deltaTime * (slashLanded ? 2.2f : 3.0f));
    bladeClashTimer_ -= deltaTime;

    const float progress = SmoothStep01((bladeClashGauge_ + 1.0f) * 0.5f);
    bladeClashPlayerFixedPos_ =
        Lerp3(bladeClashPlayerLosePos_, bladeClashPlayerWinPos_, progress);
    bladeClashCenter_ = {
        bladeClashPlayerFixedPos_.x + bladeClashDirection_.x *
                                        (0.74f + 0.22f * progress),
        bladeClashPlayerFixedPos_.y + 1.10f + 0.04f * bladeClashImpactPulse_,
        bladeClashPlayerFixedPos_.z + bladeClashDirection_.z *
                                        (0.74f + 0.22f * progress)};
    player_.LockPosition(bladeClashPlayerFixedPos_);
    player_.SetBladeClashPose(true, progress);

    if (bladeClashGauge_ >= 1.0f) {
        ResolveBladeClash(true);
    } else if (bladeClashGauge_ <= -1.0f || bladeClashTimer_ <= 0.0f) {
        ResolveBladeClash(false);
    }
}

void GameScene::ResolveBladeClash(bool playerWon) {
    if (!bladeClashActive_) {
        return;
    }

    bladeClashActive_ = false;
    bladeClashPreviousSlashStates_.fill(false);
    bladeClashCameraPush_ = 0.0f;
    bladeClashImpactPulse_ = 0.0f;
    bladeClashEnemySurgeTimer_ = 0.0f;
    bladeClashChainTimer_ = 0.0f;
    bladeClashSlashChain_ = 0;
    const bool wasFinalClash = bladeClashFinal_;
    bladeClashFinishActive_ = true;
    bladeClashFinishPlayerWon_ = playerWon;
    bladeClashFinishImpactEmitted_ = false;
    bladeClashFinishGuardBreakEmitted_ = false;
    bladeClashFinishSkidEmitted_ = false;
    bladeClashFinishWallImpactEmitted_ = false;
    bladeClashFinishPendingEnemyTransition_ = false;
    bladeClashFinishTimer_ = 0.0f;
    bladeClashFinalBarrageStep_ = 0;
    const float winFinishBaseDuration =
        wasFinalClash ? kBladeClashWinFinalDuration
                      : kBladeClashWinNormalDuration;
    bladeClashFinishDuration_ =
        playerWon
            ? winFinishBaseDuration * kBladeClashWinActionSlow +
                  kBladeClashWinGuardBreakLead
            : 2.38f;
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
    bladeClashFinishEnemyEnd_ = {
        bladeClashFinishEnemyStart_.x + bladeClashDirection_.x * 20.80f,
        bladeClashFinishEnemyStart_.y,
        bladeClashFinishEnemyStart_.z + bladeClashDirection_.z * 20.80f};
    bladeClashFinishPlayerEnd_ =
        playerWon ? XMFLOAT3{enemy_.GetTransform().position.x +
                                 bladeClashDirection_.x * 24.80f,
                             player_.GetTransform().position.y,
                             enemy_.GetTransform().position.z +
                                 bladeClashDirection_.z * 24.80f}
                  : XMFLOAT3{player_.GetTransform().position.x -
                                 bladeClashDirection_.x * 15.80f,
                             player_.GetTransform().position.y,
                             player_.GetTransform().position.z -
                                 bladeClashDirection_.z * 15.80f};
    player_.SetBladeClashPose(!playerWon, 0.0f);
    SetEnemyAnimationFrozen(playerWon);
    enemy_.ResolveBladeClash(playerWon);

    const XMFLOAT3 playerPos = player_.GetTransform().position;
    const XMFLOAT3 enemyPos = enemy_.GetTransform().position;
    CombatFeedbackEvent feedback{};
    feedback.position = bladeClashCenter_;
    feedback.direction =
        playerWon ? DirectionFromTo(playerPos, enemyPos)
                  : DirectionFromTo(enemyPos, playerPos);

    if (playerWon) {
        const float damage = wasFinalClash ? enemy_.GetHP() + 10000.0f
                                           : 170.0f;
        const float appliedDamage = ApplyEnemyDamage(damage, true);
        player_.NotifyAttackHit(appliedDamage);
        bladeClashFinishPendingEnemyTransition_ = appliedDamage > 0.0f;
        if (appliedDamage > 0.0f) {
            feedback.type = CombatFeedbackEventType::CounterSuccess;
            feedback.power = wasFinalClash ? 18.0f : appliedDamage / 18.0f;
            DispatchCombatFeedback(feedback);
        }
        bladeClashFinal_ = false;
        return;
    }

    XMFLOAT3 warningCenter = playerPos;
    warningCenter.y += 1.16f;
    sparkParticles_.EmitBurst(
        warningCenter, 18, 0.24f, GPUParticleSystem::BurstStyle::Sparks,
        {1.0f, 0.50f, 0.18f, 0.42f},
        {bladeClashDirection_.z, 0.04f, -bladeClashDirection_.x}, 0.66f);
}

void GameScene::UpdateCombat(float gameplayDeltaTime) {
    collisionManager_.Clear();

    if (bladeClashActive_) {
        UpdateBladeClash(gameplayDeltaTime);
        previousChargeWeakPointSlashStates_ = player_.GetSwordSlashStates();
        return;
    }

    const auto playerBox = player_.GetOBB();
    // const float playerRecoveryDamageScale =
    //     player_.IsAttackRecovery() ? 1.65f : 1.0f;
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
    const uint32_t enemyActionSerial = enemy_.GetActionSerial();
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
        player_.NotifyCounterSuccess(swordIndex);
        if (enemy_.NotifyCountered(vulnerabilityDuration)) {
            forceSyncEnemyAnimationThisFrame = true;
        }
        const float appliedDamage = ApplyEnemyDamage(counterDamage);
        player_.NotifyAttackHit(swordIndex, appliedDamage);
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
    const bool isEnemyFarLaserCounterApproach =
        enemyActionKind == ActionKind::Laser &&
        enemyActionStep == ActionStep::Active;
    const bool isEnemySmashMeleeWindow =
        isEnemySmashCommitted && enemy_.IsAttackActive();
    const bool isEnemySweepMeleeWindow =
        isEnemySweepCommitted && enemy_.IsAttackActive();
    const bool isEnemyMeleeActive =
        isEnemySmashMeleeWindow || isEnemySweepMeleeWindow;
    const bool isEnemyMeleeCommitted =
        isEnemySmashCommitted || isEnemySweepCommitted ||
        isEnemyFarLaserCounterApproach;
    const bool isEnemyBladeClashStandby = enemy_.IsPhase2BladeClashStandby();
    const bool isEnemyBladeClashCommitted =
        enemyActionKind == ActionKind::BladeClash &&
        (enemyActionStep == ActionStep::Active || isEnemyBladeClashStandby);
    const bool isEnemyBladeClashCounterWindow =
        isEnemyBladeClashCommitted &&
        (isEnemyBladeClashStandby || enemy_.IsBladeClashWindow());
    const bool isEnemyBladeClashStrikeActive =
        isEnemyBladeClashCommitted && enemy_.IsAttackActive();
    if (enemyRedPunishUncounterable_ &&
        (!(enemyActionKind == ActionKind::Smash ||
           enemyActionKind == ActionKind::Sweep) ||
         enemyActionStep == ActionStep::Recovery ||
         enemyActionStep == ActionStep::None)) {
        enemyRedPunishUncounterable_ = false;
    }
    const bool isEnemyChargeWeakPointWindow =
        IsChargeWeakPointWindow(enemyActionKind, enemyActionId,
                                enemyActionStep);
    const int requiredChargeWeakPointSlashCount =
        static_cast<int>(chargeWeakPointRequiredDirections_.size());
    if (chargeWeakPointFailedThisAction_ &&
        (enemyActionSerial != failedChargeWeakPointActionSerial_ ||
         enemyActionStep == ActionStep::Recovery ||
         enemyActionStep == ActionStep::None)) {
        chargeWeakPointFailedThisAction_ = false;
        failedChargeWeakPointActionKind_ = ActionKind::None;
        failedChargeWeakPointActionSerial_ = 0;
    }
    if (isEnemyChargeWeakPointWindow) {
        if (chargeWeakPointActionSerial_ != enemyActionSerial) {
            chargeWeakPointActionKind_ = enemyActionKind;
            chargeWeakPointActionSerial_ = enemyActionSerial;
            chargeWeakPointBroken_ = false;
            chargeWeakPointFailedThisAction_ = false;
            failedChargeWeakPointActionKind_ = ActionKind::None;
            failedChargeWeakPointActionSerial_ = 0;
            chargeWeakPointSlashCount_ = 0;
            chargeWeakPointRequiredDirections_[0] =
                MakeRandomChargeWeakPointDirection();
            for (size_t directionIndex = 1;
                 directionIndex < chargeWeakPointRequiredDirections_.size();
                 ++directionIndex) {
                const size_t previousDirectionIndex = directionIndex - 1;
                const auto *previousDirection =
                    &chargeWeakPointRequiredDirections_[previousDirectionIndex];
                chargeWeakPointRequiredDirections_[directionIndex] =
                    MakeRandomChargeWeakPointDirection(previousDirection);
            }
            previousChargeWeakPointSlashStates_.fill(false);
        }
    } else {
        if (chargeWeakPointActionKind_ != ActionKind::None &&
            !chargeWeakPointBroken_ &&
            chargeWeakPointSlashCount_ < requiredChargeWeakPointSlashCount) {
            chargeWeakPointFailedThisAction_ = true;
            failedChargeWeakPointActionKind_ = chargeWeakPointActionKind_;
            failedChargeWeakPointActionSerial_ = chargeWeakPointActionSerial_;
        }
        chargeWeakPointActionKind_ = ActionKind::None;
        chargeWeakPointActionSerial_ = 0;
        chargeWeakPointBroken_ = false;
        chargeWeakPointSlashCount_ = 0;
        previousChargeWeakPointSlashStates_.fill(false);
    }
    const bool isFailedChargeWeakPointRelease =
        chargeWeakPointFailedThisAction_ &&
        enemyActionSerial == failedChargeWeakPointActionSerial_ &&
        enemyActionStep == ActionStep::Active;
    const bool isEnemyMeleePreparationOrRelease =
        (enemyActionKind == ActionKind::Smash ||
         enemyActionKind == ActionKind::Sweep ||
         enemyActionKind == ActionKind::Laser ||
         enemyActionKind == ActionKind::BladeClash) &&
        (enemyActionStep == ActionStep::Charge ||
         enemyActionStep == ActionStep::Hold ||
         enemyActionStep == ActionStep::Active);
    const bool isPreReleaseCounterWindow =
        (enemyActionKind == ActionKind::Smash ||
         enemyActionKind == ActionKind::Sweep ||
         enemyActionKind == ActionKind::Laser) &&
        isEnemyMeleePreparationOrRelease && !isEnemyChargeWeakPointWindow &&
        !chargeWeakPointFailedThisAction_ &&
        enemyActionStep != ActionStep::Active &&
        enemy_.GetReleaseAnticipationRatio() > 0.0f;
    const bool isPhase3GuardCounterWindow =
        enemy_.IsPhase3GuardCounterActive() &&
        (enemyActionKind == ActionKind::Smash ||
         enemyActionKind == ActionKind::Sweep) &&
        (enemyActionStep == ActionStep::Charge ||
         enemyActionStep == ActionStep::Active);
    const bool isReleaseCounterWindow =
        !enemyRedPunishUncounterable_ &&
        (isPhase3GuardCounterWindow || isPreReleaseCounterWindow ||
         isEnemyFarLaserCounterApproach ||
         (isEnemyMeleeCommitted &&
          enemy_.GetActionTimerForPresentation() <=
              kReleaseCounterWindowDuration) ||
         isEnemyBladeClashCounterWindow);
    const bool suppressNormalSlashHitDuringEnemyMelee =
        isEnemyMeleePreparationOrRelease;

    const float enemyAttackDamage = enemy_.GetCurrentAttackDamage();
    const float enemyAttackKnockback = enemy_.GetCurrentAttackKnockback();
    const bool isEnemyCounterWindow = isReleaseCounterWindow;
    OBB enemyAttackBox{};
    CollisionManager::BodyId enemyAttackBody =
        CollisionManager::kInvalidBodyId;
    if (isEnemyMeleeCommitted || isPreReleaseCounterWindow ||
        isPhase3GuardCounterWindow || isEnemyBladeClashCommitted) {
        enemyAttackBox = enemy_.GetAttackOBB();
        enemyAttackBody =
            AddCollisionBody(collisionManager_, enemyAttackBox,
                             kLayerEnemyAttack,
                             kLayerPlayer | kLayerPlayerAttack);
    }

    const bool isBadSlashPunishWindow =
        isEnemyMeleePreparationOrRelease && !isEnemyChargeWeakPointWindow &&
        enemyActionKind != ActionKind::BladeClash && !isReleaseCounterWindow &&
        !enemyRedPunishUncounterable_ &&
        !(enemyLastStandPrimed_ && enemy_.GetHP() <= 1.0f);
    if (isBadSlashPunishWindow && playerHitCooldown_ <= 0.0f) {
        for (size_t i = 0; i < swordSlashStates.size(); ++i) {
            if (!swordSlashStates[i] || previousChargeWeakPointSlashStates_[i]) {
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
            player_.TakeDamage(0.0f);
            // player_.TakeDamage(enemyAttackDamage * 1.25f *
            //                    playerRecoveryDamageScale);
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
            if (enemyActionKind == ActionKind::BladeClash) {
                enemy_.NotifyBladeClashLanded();
            } else {
                enemy_.NotifyAttackConnected();
            }
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
            !isFailedChargeWeakPointRelease &&
            enemyActionKind != ActionKind::BladeClash &&
            isReleaseCounterWindow &&
            playerHitCooldown_ <= 0.0f && isEnemyCounterWindow &&
            enemyAttackBody != CollisionManager::kInvalidBodyId &&
            IsNearXZ(player_.GetTransform().position, enemyAttackBox.center,
                     GetReadableMeleeRadius(enemyAttackBox) + 0.85f) &&
            IsSlashAxisMatched(*sword,
                               RequiredCounterAxisForAction(
                                   enemyActionKind,
                                   enemy_.GetFarLaserFollowupKind()));
        const bool canBladeClashCounter =
            enemyActionKind == ActionKind::BladeClash &&
            isEnemyBladeClashCounterWindow &&
            playerHitCooldown_ <= 0.0f &&
            enemyAttackBody != CollisionManager::kInvalidBodyId &&
            IsNearXZ(player_.GetTransform().position, enemyAttackBox.center,
                     GetReadableMeleeRadius(enemyAttackBox) + 0.95f) &&
            isFreshChargeWeakPointSlash && sword->CanSlashCounter();

        if (canSlashCounter) {
            triggerSuccessfulCounter(i, enemyAttackDamage, 0.2f);
            counterTriggeredThisFrame = true;
            break;
        }

        if (canBladeClashCounter) {
            BeginBladeClash(i);
            counterTriggeredThisFrame = true;
            break;
        }

        if (canMatchChargeWeakPoint) {
            ++chargeWeakPointSlashCount_;
            if (chargeWeakPointSlashCount_ <
                requiredChargeWeakPointSlashCount) {
                const size_t nextDirectionIndex =
                    static_cast<size_t>(chargeWeakPointSlashCount_);
                const size_t previousDirectionIndex = nextDirectionIndex - 1;
                const auto *previousDirection =
                    &chargeWeakPointRequiredDirections_[previousDirectionIndex];
                chargeWeakPointRequiredDirections_[nextDirectionIndex] =
                    MakeRandomChargeWeakPointDirection(previousDirection);

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
            chargeWeakPointActionSerial_ = 0;
            const float breakDamage = 78.0f + swordAttackDamages[i] * 1.25f;
            if (enemy_.NotifyCountered(0.95f)) {
                forceSyncEnemyAnimationThisFrame = true;
            }
            const float appliedDamage = ApplyEnemyDamage(breakDamage);
            player_.NotifyAttackHit(i, appliedDamage);

            CombatFeedbackEvent feedback{};
            feedback.type = CombatFeedbackEventType::CounterSuccess;
            feedback.position = enemy_.GetTransform().position;
            feedback.position.y += 1.55f;
            feedback.direction =
                DirectionFromTo(player_.GetTransform().position,
                                enemy_.GetTransform().position);
            feedback.power = appliedDamage / 12.0f;
            feedback.swordIndex = i;
            DispatchCombatFeedback(feedback);
            enemyHitCooldown_ = 0.18f;
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
                const bool beganPhase3GuardCounter =
                    enemy_.TryBeginPhase3GuardCounter();
                if (beganPhase3GuardCounter) {
                    forceSyncEnemyAnimationThisFrame = true;
                }
                player_.NotifyAttackHit(i, appliedDamage);
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

    if ((isEnemyMeleeActive || isEnemyBladeClashStrikeActive) &&
        !counterTriggeredThisFrame) {
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

            if (isEnemyBladeClashStrikeActive) {
                enemy_.NotifyBladeClashLanded();
            } else {
                enemy_.NotifyAttackConnected();
            }
            player_.TakeDamage(0.0f);
            // player_.TakeDamage(enemyAttackDamage * playerRecoveryDamageScale);
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

    const auto &waves = enemy_.GetWaves();
    for (size_t i = 0; i < waves.size(); ++i) {
        const auto &wave = waves[i];
        if (!wave.isAlive) {
            continue;
        }

        OBB waveBox{};
        waveBox.center = wave.position;
        waveBox.size = wave.hitBoxSize;
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
            feedback.power = wave.damage / 5.0f;
            DispatchCombatFeedback(feedback);
            continue;
        }

        if (wave.isReflected) {
            if (isEnemyHurtBodyHit(waveBody) &&
                enemyHitCooldown_ <= 0.0f) {
                const float damage = wave.damage * damageMultiplier_;
                const float appliedDamage = ApplyEnemyDamage(damage);
                if (appliedDamage <= 0.0f) {
                    enemy_.DestroyWave(i);
                    continue;
                }
                player_.NotifyAttackHit(appliedDamage);
                enemy_.DestroyWave(i);
                CombatFeedbackEvent feedback{};
                feedback.type = CombatFeedbackEventType::ProjectileReflect;
                feedback.position = wave.position;
                feedback.direction = DirectionFromTo(player_.GetTransform().position,
                                                    enemy_.GetTransform().position);
                feedback.power = appliedDamage / 10.0f;
                DispatchCombatFeedback(feedback);
                enemyHitCooldown_ = 0.2f;
            }
            continue;
        }

        if (IsNearXZ(wave.position, player_.GetTransform().position,
                     waveThreatRadius)) {
            if (playerHitCooldown_ <= 0.0f) {
                const XMFLOAT2 hitDir =
                    NormalizeXZ(wave.direction.x, wave.direction.z);

                size_t counterSwordIndex = swords.size();
                if (FindSlashTowardPoint(swords, swordSlashStates,
                                         wave.position, counterSwordIndex,
                                         0.00f)) {
                    triggerSuccessfulCounter(counterSwordIndex,
                                             wave.damage * 2.0f,
                                             0.12f);
                } else {
                    player_.TakeDamage(0.0f);
                    // player_.TakeDamage(wave.damage * playerRecoveryDamageScale);
                    player_.AddKnockback(
                        {hitDir.x * wave.knockback, 0.0f,
                         hitDir.y * wave.knockback});
                    CombatFeedbackEvent feedback{};
                    feedback.type = CombatFeedbackEventType::PlayerDamaged;
                    feedback.position = wave.position;
                    feedback.direction = {hitDir.x, 0.0f, hitDir.y};
                    feedback.power = wave.damage / 5.0f;
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
