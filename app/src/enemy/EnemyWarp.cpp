#include "Enemy.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
float Random01() {
    return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
}

constexpr float kFarSlashCounterFlashDuration = 0.50f;
constexpr float kTwoPi = 6.28318530f;
constexpr float kWarpPlayerClearance = 3.05f;

float PhantomWarpMoveTime(bool finalWarp) { return finalWarp ? 0.24f : 0.20f; }

float PhantomWarpEndTime(bool finalWarp) { return finalWarp ? 0.18f : 0.42f; }
} // namespace

void Enemy::UpdateWarpByStep(float deltaTime) {
    switch (action_.step) {
    case ActionStep::Start:
        UpdateWarpStart(deltaTime);
        break;
    case ActionStep::Move:
        UpdateWarpMove(deltaTime);
        break;
    case ActionStep::End:
        UpdateWarpEnd(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
}

bool Enemy::DecideWarpTargetNearPlayer(DirectX::XMFLOAT3 &outTarget) {
    float forwardX = playerPos_.x - tf_.position.x;
    float forwardZ = playerPos_.z - tf_.position.z;
    float forwardLength = std::sqrt(forwardX * forwardX + forwardZ * forwardZ);
    if (forwardLength <= 0.0001f) {
        forwardX = std::sin(facingYaw_);
        forwardZ = std::cos(facingYaw_);
        forwardLength = 1.0f;
    }
    forwardX /= forwardLength;
    forwardZ /= forwardLength;

    if (warp_.approachSlot == WarpApproachSlot::None) {
        warp_.approachSlot = (std::rand() % 100 < 42) ? WarpApproachSlot::Front
                                                      : WarpApproachSlot::Back;
    }

    outTarget = playerPos_;
    if (warp_.approachSlot == WarpApproachSlot::Front) {
        const float distance =
            std::max(warpApproachFrontDistance_, kWarpPlayerClearance);
        outTarget.x -= forwardX * distance;
        outTarget.z -= forwardZ * distance;
    } else {
        const float distance =
            std::max(warpApproachBackDistance_, kWarpPlayerClearance);
        outTarget.x += forwardX * distance;
        outTarget.z += forwardZ * distance;
    }
    outTarget.y = tf_.position.y;
    FinalizeWarpTargetFacing(outTarget);
    return true;
}

bool Enemy::DecideWarpTargetFarSlash(DirectX::XMFLOAT3 &outTarget) {
    float forwardX = playerPos_.x - tf_.position.x;
    float forwardZ = playerPos_.z - tf_.position.z;
    float forwardLength = std::sqrt(forwardX * forwardX + forwardZ * forwardZ);
    if (forwardLength <= 0.0001f) {
        forwardX = std::sin(facingYaw_);
        forwardZ = std::cos(facingYaw_);
        forwardLength = 1.0f;
    }
    forwardX /= forwardLength;
    forwardZ /= forwardLength;

    const float sideSign = (std::rand() % 2 == 0) ? -1.0f : 1.0f;
    const float sideT = Random01();
    const float rightX = forwardZ;
    const float rightZ = -forwardX;

    outTarget = playerPos_;
    outTarget.x -= forwardX * farWarpSlashDistance_;
    outTarget.z -= forwardZ * farWarpSlashDistance_;
    outTarget.x += rightX * sideSign * (1.6f + 2.4f * sideT);
    outTarget.z += rightZ * sideSign * (1.6f + 2.4f * sideT);
    outTarget.y = tf_.position.y;
    FinalizeWarpTargetFacing(outTarget);
    return true;
}

bool Enemy::DecideWarpTargetTripleIaiSlash(DirectX::XMFLOAT3 &outTarget,
                                           int slashIndex) {
    float forwardX = playerPos_.x - tf_.position.x;
    float forwardZ = playerPos_.z - tf_.position.z;
    float forwardLength = std::sqrt(forwardX * forwardX + forwardZ * forwardZ);
    if (forwardLength <= 0.0001f) {
        forwardX = std::sin(facingYaw_);
        forwardZ = std::cos(facingYaw_);
        forwardLength = 1.0f;
    }
    forwardX /= forwardLength;
    forwardZ /= forwardLength;

    const int clampedIndex =
        std::clamp(slashIndex, 0, kTripleIaiCloneCount_ - 1);
    const float baseAngle = std::atan2f(-forwardX, -forwardZ);
    const float ringAngle =
        baseAngle +
        (static_cast<float>(clampedIndex) *
         (kTwoPi / static_cast<float>(kTripleIaiCloneCount_))) +
        (Random01() - 0.5f) * 0.18f;
    const float ringRadius = 13.2f + 2.8f * Random01();
    const float radialX = std::sinf(ringAngle);
    const float radialZ = std::cosf(ringAngle);
    const float tangentX = radialZ;
    const float tangentZ = -radialX;
    const float tangentJitter = (Random01() - 0.5f) * 1.6f;

    outTarget = playerPos_;
    outTarget.x += radialX * ringRadius + tangentX * tangentJitter;
    outTarget.z += radialZ * ringRadius + tangentZ * tangentJitter;
    outTarget.y = tf_.position.y;
    FinalizeWarpTargetFacing(outTarget);
    return true;
}

bool Enemy::DecideWarpTargetArcaneLaser(DirectX::XMFLOAT3 &outTarget) {
    float awayX = tf_.position.x - playerPos_.x;
    float awayZ = tf_.position.z - playerPos_.z;
    float awayLength = std::sqrt(awayX * awayX + awayZ * awayZ);
    if (awayLength <= 0.0001f) {
        awayX = -std::sin(facingYaw_);
        awayZ = -std::cos(facingYaw_);
        awayLength = 1.0f;
    }
    awayX /= awayLength;
    awayZ /= awayLength;

    const float sideSign = (std::rand() % 2 == 0) ? -1.0f : 1.0f;
    const float rightX = awayZ;
    const float rightZ = -awayX;
    const float sideOffset = sideSign * (0.65f + 0.75f * Random01());

    outTarget = playerPos_;
    outTarget.x += awayX * arcaneLaserWarpDistance_ + rightX * sideOffset;
    outTarget.z += awayZ * arcaneLaserWarpDistance_ + rightZ * sideOffset;
    outTarget.y = tf_.position.y;
    FinalizeWarpTargetFacing(outTarget);
    return true;
}

bool Enemy::DecideWarpTargetCataclysmLaser(DirectX::XMFLOAT3 &outTarget) {
    float awayX = tf_.position.x - playerPos_.x;
    float awayZ = tf_.position.z - playerPos_.z;
    float awayLength = std::sqrt(awayX * awayX + awayZ * awayZ);
    if (awayLength <= 0.0001f) {
        awayX = -std::sin(facingYaw_);
        awayZ = -std::cos(facingYaw_);
        awayLength = 1.0f;
    }
    awayX /= awayLength;
    awayZ /= awayLength;

    const float rightX = awayZ;
    const float rightZ = -awayX;
    const float sideSign = (std::rand() % 2 == 0) ? -1.0f : 1.0f;
    const float sideOffset = sideSign * (0.40f + 0.65f * Random01());

    outTarget = playerPos_;
    outTarget.x += awayX * cataclysmLaserWarpDistance_ + rightX * sideOffset;
    outTarget.z += awayZ * cataclysmLaserWarpDistance_ + rightZ * sideOffset;
    outTarget.y = playerPos_.y;
    FinalizeWarpTargetFacing(outTarget);
    return true;
}

bool Enemy::DecideWarpTargetInPlayerView(DirectX::XMFLOAT3 &outTarget) {
    float forwardX = playerPos_.x - tf_.position.x;
    float forwardZ = playerPos_.z - tf_.position.z;
    float forwardLength = std::sqrt(forwardX * forwardX + forwardZ * forwardZ);
    if (forwardLength <= 0.0001f) {
        forwardX = std::sin(facingYaw_);
        forwardZ = std::cos(facingYaw_);
        forwardLength = 1.0f;
    }
    forwardX /= forwardLength;
    forwardZ /= forwardLength;
    const float rightX = forwardZ;
    const float rightZ = -forwardX;

    const float sideSign = (std::rand() % 2 == 0) ? -1.0f : 1.0f;
    const float laneT = Random01();
    const float sideOffset = sideSign * (2.2f + 4.2f * Random01());
    const float forwardOffset = 2.0f + 5.8f * laneT;

    outTarget = playerPos_;
    outTarget.x += forwardX * forwardOffset + rightX * sideOffset;
    outTarget.z += forwardZ * forwardOffset + rightZ * sideOffset;
    outTarget.y = tf_.position.y;
    FinalizeWarpTargetFacing(outTarget);
    return true;
}

bool Enemy::DecideWarpTargetBehindPlayer(DirectX::XMFLOAT3 &outTarget) {
    float forwardX = playerPos_.x - tf_.position.x;
    float forwardZ = playerPos_.z - tf_.position.z;
    float forwardLength = std::sqrt(forwardX * forwardX + forwardZ * forwardZ);
    if (forwardLength <= 0.0001f) {
        forwardX = std::sin(facingYaw_);
        forwardZ = std::cos(facingYaw_);
        forwardLength = 1.0f;
    }
    forwardX /= forwardLength;
    forwardZ /= forwardLength;

    outTarget = playerPos_;
    const float distance =
        std::max(warpApproachBackDistance_, kWarpPlayerClearance);
    outTarget.x += forwardX * distance;
    outTarget.z += forwardZ * distance;
    outTarget.y = tf_.position.y;
    FinalizeWarpTargetFacing(outTarget);
    return true;
}

bool Enemy::RefreshLiveBehindWarpTarget() {
    const bool shouldRefresh =
        (warp_.feintFollowup && warp_.approachSlot == WarpApproachSlot::Back) ||
        (warp_.phantomChain && warp_.phantomFinal);
    if (!shouldRefresh) {
        return false;
    }

    return DecideWarpTargetBehindPlayer(warp_.targetPos);
}

void Enemy::FinalizeWarpTargetFacing(DirectX::XMFLOAT3 &target) {
    const float dx = playerPos_.x - target.x;
    const float dz = playerPos_.z - target.z;
    if (dx * dx + dz * dz > 0.0001f) {
        warp_.targetYaw = NormalizeAngle(std::atan2(dx, dz));
    } else {
        warp_.targetYaw = NormalizeAngle(facingYaw_);
    }
    warp_.hasTargetYaw = true;
}

void Enemy::ConfigureFarSlashLungeTarget() {
    if (!farSlashActive_ || (action_.kind != ActionKind::Smash &&
                             action_.kind != ActionKind::Sweep)) {
        hasFarSlashLungeTarget_ = false;
        farSlashLungeDuration_ = 0.0f;
        return;
    }

    farSlashLungeStartPos_ = tf_.position;

    float forwardX = std::sin(lockedAttackYaw_);
    float forwardZ = std::cos(lockedAttackYaw_);

    float toPlayerX = playerPos_.x - tf_.position.x;
    float toPlayerZ = playerPos_.z - tf_.position.z;
    const float toPlayerLength =
        std::sqrt(toPlayerX * toPlayerX + toPlayerZ * toPlayerZ);
    if (toPlayerLength > 0.0001f) {
        forwardX = toPlayerX / toPlayerLength;
        forwardZ = toPlayerZ / toPlayerLength;
    }

    float approachOffset =
        std::clamp(config_.core.nearAttackDistance * 0.42f, 1.1f, 2.1f);
    if (toPlayerLength > 0.0001f) {
        approachOffset = toPlayerLength > 0.45f
                             ? std::clamp(approachOffset, 0.45f, toPlayerLength)
                             : 0.0f;
    }

    const float pierceThroughDistance =
        approachOffset +
        std::clamp(config_.core.nearAttackDistance * 0.88f, 2.4f, 4.2f);
    farSlashLungeTargetPos_.x = playerPos_.x + forwardX * pierceThroughDistance;
    farSlashLungeTargetPos_.z = playerPos_.z + forwardZ * pierceThroughDistance;
    farSlashLungeTargetPos_.y = tf_.position.y;

    float toTargetX = farSlashLungeTargetPos_.x - farSlashLungeStartPos_.x;
    float toTargetZ = farSlashLungeTargetPos_.z - farSlashLungeStartPos_.z;
    const float targetDistance =
        std::sqrt(toTargetX * toTargetX + toTargetZ * toTargetZ);
    const float lungeSpeed =
        farSlashLungeSpeed_ *
        (tripleIaiSlashActive_ ? tripleIaiSlashSpeedScale_ : 1.0f);
    const float travelDuration = targetDistance / std::max(1.0f, lungeSpeed);
    farSlashLungeDuration_ = std::max(travelDuration, 0.06f);
    hasFarSlashLungeTarget_ = true;
}

void Enemy::UpdateFarSlashLunge(float deltaTime) {
    (void)deltaTime;
    if (!hasFarSlashLungeTarget_ || farSlashLungeDuration_ <= 0.0001f) {
        return;
    }

    const float lungeTimer =
        std::max(0.0f, stateTimer_ - kFarSlashCounterFlashDuration);
    const float t = std::clamp(lungeTimer / farSlashLungeDuration_, 0.0f, 1.0f);
    const float eased = 1.0f - std::pow(1.0f - t, 2.3f);

    tf_.position.x =
        farSlashLungeStartPos_.x +
        (farSlashLungeTargetPos_.x - farSlashLungeStartPos_.x) * eased;
    tf_.position.z =
        farSlashLungeStartPos_.z +
        (farSlashLungeTargetPos_.z - farSlashLungeStartPos_.z) * eased;
    tf_.position.y =
        farSlashLungeStartPos_.y +
        (farSlashLungeTargetPos_.y - farSlashLungeStartPos_.y) * eased;

    if (t >= 1.0f) {
        tf_.position = farSlashLungeTargetPos_;
        hasFarSlashLungeTarget_ = false;
    }
}

bool Enemy::PrepareWarpContext() {
    ResetWarpContext();

    const float distance = GetDistanceToPlayer();
    warp_.isCutIn = distance >= warpCutInDistance_;
    if (warp_.isCutIn) {
        warp_.approachSlot = WarpApproachSlot::Back;
    }

    if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
        ResetWarpContext();
        return false;
    }

    warp_.hasValidTarget = true;

    float feintChance = warpFeintChance_;
    if (warp_.isCutIn) {
        feintChance *= 0.35f;
    }
    if (phase_ == BossPhase::Phase2) {
        feintChance *= 0.25f;
    }
    if (phase_ == BossPhase::Phase3) {
        feintChance = 0.0f;
    }
    warp_.isFeint = Random01() < std::clamp(feintChance, 0.0f, 0.65f);

    if (!warp_.isFeint) {
        warp_.followupKind = SelectNearPressureAction();
        warp_.followupStep = ActionStep::Charge;
    }
    return true;
}

void Enemy::ResetWarpContext() { warp_ = WarpContext{}; }

float Enemy::GetCurrentWarpStartTime() const {
    float startTime = config_.warp.startTime;
    if (warp_.phantomChain) {
        startTime = warp_.phantomFinal ? 0.13f : 0.11f;
    } else if (warp_.followupKind == ActionKind::ArcaneLaser ||
               warp_.followupKind == ActionKind::CataclysmLaser) {
        startTime *= 0.56f;
    } else if (warp_.feintFollowup) {
        startTime *= 0.55f;
    } else if (warp_.farSlashFollowup && tripleIaiSlashActive_) {
        startTime *= 0.12f;
    } else if (warp_.farSlashFollowup) {
        startTime *= 0.44f;
    }
    return startTime;
}

float Enemy::GetWarpVisualAlpha() const {
    if (action_.kind != ActionKind::Warp) {
        return 1.0f;
    }
    if (action_.step == ActionStep::Move) {
        return 0.0f;
    }
    if (action_.step != ActionStep::Start) {
        return 1.0f;
    }

    const float startTime = GetCurrentWarpStartTime();
    if (startTime <= 0.0001f) {
        return 0.0f;
    }

    const float t = std::clamp(stateTimer_ / startTime, 0.0f, 1.0f);
    const float holdRatio = warp_.phantomChain ? 0.08f : 0.18f;
    const float fadeT =
        std::clamp((t - holdRatio) / (1.0f - holdRatio), 0.0f, 1.0f);
    const float snapFade = std::pow(fadeT, 2.35f);
    return std::pow(1.0f - snapFade, 2.80f);
}

void Enemy::UpdateWarpStart(float deltaTime) {
    if (!warp_.hasDeparturePos) {
        warp_.departurePos = tf_.position;
        warp_.hasDeparturePos = true;
    }

    UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * 0.45f);
    isVisible_ = true;
    warp_.collisionDisabled = false;

    const float startTime = GetCurrentWarpStartTime();

    if (stateTimer_ >= startTime) {
        isVisible_ = false;
        warp_.collisionDisabled = true;
        EmitWarpTrailGhost(warp_.departurePos, warpTrailScaleMax_);
        ChangeActionStep(ActionStep::Move);
    }
}

void Enemy::UpdateWarpMove(float deltaTime) {
    (void)deltaTime;

    if (!warp_.hasValidTarget) {
        EndAttack();
        return;
    }
    RefreshLiveBehindWarpTarget();

    float t = 1.0f;
    const float moveTime =
        warp_.phantomChain ? PhantomWarpMoveTime(warp_.phantomFinal)
        : warp_.farSlashFollowup && tripleIaiSlashActive_
            ? config_.warp.moveTime * 0.34f
        : warp_.farSlashFollowup ? config_.warp.moveTime * 1.80f
                                 : config_.warp.moveTime;
    if (moveTime > 0.0001f) {
        t = stateTimer_ / moveTime;
    }
    t = std::clamp(t, 0.0f, 1.0f);

    const float eased = 1.0f - std::pow(1.0f - t, 2.6f);
    tf_.position.x = warp_.departurePos.x +
                     (warp_.targetPos.x - warp_.departurePos.x) * eased;
    tf_.position.y = warp_.departurePos.y +
                     (warp_.targetPos.y - warp_.departurePos.y) * eased;
    tf_.position.z = warp_.departurePos.z +
                     (warp_.targetPos.z - warp_.departurePos.z) * eased;

    if (warp_.hasTargetYaw) {
        facingYaw_ = NormalizeAngle(warp_.targetYaw);
        LockCurrentFacing();
    }

    if (stateTimer_ >= moveTime) {
        tf_.position = warp_.targetPos;
        if ((warp_.faceLivePlayerOnEnd ||
             warp_.followupKind == ActionKind::Smash ||
             warp_.followupKind == ActionKind::Sweep ||
             warp_.followupKind == ActionKind::ArcaneLaser ||
             warp_.followupKind == ActionKind::CataclysmLaser)) {
            UpdateFacingToPlayer();
            LockCurrentFacing();
        } else if (warp_.hasTargetYaw) {
            facingYaw_ = NormalizeAngle(warp_.targetYaw);
            LockCurrentFacing();
        }
        ResetWarpTrails();
        EmitWarpTrailGhost(warp_.targetPos, warpTrailScaleMax_ * 1.14f);
        ChangeActionStep(ActionStep::End);
    }
}

void Enemy::UpdateWarpEnd(float deltaTime) {
    isVisible_ = true;
    warp_.collisionDisabled = false;
    RefreshLiveBehindWarpTarget();
    UpdateWarpEndFacing(deltaTime);

    if (stateTimer_ < GetWarpEndDuration()) {
        return;
    }

    const ActionKind followupKind = warp_.followupKind;
    const ActionStep followupStep = warp_.followupStep;
    const bool isFeint = warp_.isFeint;
    const bool feintFollowup = warp_.feintFollowup;
    const bool immediateFollowup = warp_.immediateFollowup;
    const bool farSlashFollowup = warp_.farSlashFollowup;
    const bool phantomChain = warp_.phantomChain;
    const bool phantomFinal = warp_.phantomFinal;
    const int phantomRemaining = warp_.phantomViewWarpsRemaining;
    EndAttack();
    if (phantomChain && !phantomFinal &&
        ContinuePhantomWarp(phantomRemaining)) {
        return;
    }
    if (isFeint) {
        BeginStalkAction();
        return;
    }
    if (BeginWarpMeleeFollowup(followupKind, followupStep, immediateFollowup,
                               farSlashFollowup, feintFollowup, phantomChain,
                               phantomFinal)) {
        return;
    }
    if (followupKind == ActionKind::ArcaneLaser ||
        followupKind == ActionKind::CataclysmLaser) {
        UpdateFacingToPlayer();
        LockCurrentFacing();
        BeginAction(followupKind, ActionStep::Charge);
        return;
    }
    BeginChaseAction();
}

void Enemy::UpdateWarpEndFacing(float deltaTime) {
    if ((warp_.faceLivePlayerOnEnd || warp_.followupKind == ActionKind::Smash ||
         warp_.followupKind == ActionKind::Sweep ||
         warp_.followupKind == ActionKind::ArcaneLaser ||
         warp_.followupKind == ActionKind::CataclysmLaser)) {
        UpdateFacingToPlayer();
        LockCurrentFacing();
    } else if (warp_.hasTargetYaw) {
        facingYaw_ = NormalizeAngle(warp_.targetYaw);
        LockCurrentFacing();
    } else {
        UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_ * 0.75f);
    }
}

float Enemy::GetWarpEndDuration() const {
    float endTime = config_.warp.endTime;
    if (warp_.phantomChain) {
        endTime = PhantomWarpEndTime(warp_.phantomFinal);
    } else if (warp_.isFeint) {
        endTime = config_.warp.endTime * warpFeintEndTimeScale_;
    } else if (warp_.followupKind == ActionKind::ArcaneLaser ||
               warp_.followupKind == ActionKind::CataclysmLaser) {
        endTime = config_.warp.endTime * 0.42f;
    } else if (warp_.farSlashFollowup && tripleIaiSlashActive_) {
        endTime = config_.warp.endTime * 0.08f;
    } else if (warp_.farSlashFollowup) {
        endTime = config_.warp.endTime * 0.38f;
    }
    return endTime;
}

bool Enemy::ContinuePhantomWarp(int phantomRemaining) {
    const float earlyStrikeChance =
        0.18f + 0.42f * TechniqueUnlock(BossPhase::Phase3);
    const bool canCutInEarly = phantomRemaining > 1;
    if (canCutInEarly && Random01() < earlyStrikeChance) {
        const ActionKind finisher =
            (std::rand() % 2 == 0) ? ActionKind::Smash : ActionKind::Sweep;
        BeginPhantomWarpStep(0, true, finisher);
        return true;
    }
    if (phantomRemaining > 1) {
        BeginPhantomWarpStep(phantomRemaining - 1, false, ActionKind::None);
    } else {
        const ActionKind finisher =
            (std::rand() % 2 == 0) ? ActionKind::Smash : ActionKind::Sweep;
        BeginPhantomWarpStep(0, true, finisher);
    }
    return true;
}

bool Enemy::BeginWarpMeleeFollowup(ActionKind followupKind,
                                   ActionStep followupStep,
                                   bool immediateFollowup,
                                   bool farSlashFollowup, bool feintFollowup,
                                   bool phantomChain, bool phantomFinal) {
    if (followupKind != ActionKind::Smash &&
        followupKind != ActionKind::Sweep) {
        return false;
    }
    UpdateFacingToPlayer();
    LockCurrentFacing();
    if (!farSlashFollowup && !IsPlayerInMeleeFront()) {
        BeginChaseAction();
        return true;
    }
    BeginAction(followupKind, followupStep);
    quickSlashActive_ = immediateFollowup || (phantomChain && phantomFinal);
    farSlashActive_ = farSlashFollowup;
    if (farSlashActive_) {
        ConfigureFarSlashLungeTarget();
        const float chargeTime = followupKind == ActionKind::Smash
                                     ? GetCurrentSmashChargeTime()
                                     : GetCurrentSweepChargeTime();
        const float tellTime =
            followupKind == ActionKind::Smash ? smashTellTime_ : sweepTellTime_;
        IssueAttackCue(EnemyAttackCueType::Telegraph, followupKind,
                       chargeTime + tellTime);
    }
    warpFeintFollowupLocked_ = feintFollowup;
    warpFeintImmediate_ = feintFollowup && immediateFollowup;
    if (phantomChain && phantomFinal) {
        LockCurrentFacing();
        phantomFinalLockTimer_ = phantomFinalLockDuration_;
        warpFeintDecisionMade_ = true;
        directionFeintDecisionMade_ = true;
    }
    return true;
}

void Enemy::UpdateWarpTrails(float deltaTime) {
    for (auto &trail : afterimageGhosts_) {
        if (!trail.isActive) {
            continue;
        }

        trail.life -= deltaTime;
        if (trail.life <= 0.0f) {
            trail.life = 0.0f;
            trail.isActive = false;
        }
    }
}

void Enemy::EmitWarpTrailGhost(const DirectX::XMFLOAT3 &position, float scale,
                               float lifeOverride) {
    (void)position;
    (void)scale;
    (void)lifeOverride;
}

void Enemy::ResetWarpTrails() {
    warpTrailEmitTimer_ = 0.0f;
    for (auto &trail : afterimageGhosts_) {
        trail = EnemyAfterimageGhost{};
    }
}
