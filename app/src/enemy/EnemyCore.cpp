#include "Enemy.h"

#include <algorithm>
#include <cmath>

namespace {
bool SupportsMeleeAttackCue(ActionKind kind) {
    return kind == ActionKind::Smash || kind == ActionKind::Sweep ||
           kind == ActionKind::BladeClash;
}

bool UsesLockedAttackYaw(ActionKind kind) {
    return SupportsMeleeAttackCue(kind) || kind == ActionKind::ArcaneLaser ||
           kind == ActionKind::CataclysmLaser;
}

void TickNonNegative(float &timer, float deltaTime) {
    timer = (std::max)(0.0f, timer - deltaTime);
}
} // namespace

void Enemy::Initialize(uint32_t modelId) {
    modelId_ = modelId;
    runtime_.hp = config_.core.maxHp;
    runtime_.phase = BossPhase::Phase1;
    runtime_.stateTimer = -0.10f;
    runtime_.phaseTransitionActive = false;
    runtime_.phaseTransitionTimer = 0.0f;
    runtime_.phase2BladeClashPending = false;
    runtime_.pendingAttackCue = {};
    runtime_.attackCueSequence = 0;
    runtime_.tripleIaiSlashCooldown = 0.0f;
    runtime_.tripleIaiSlashActive = false;
    runtime_.tripleIaiSlashesRemaining = 0;
    runtime_.tripleIaiSlashIndex = 0;
    runtime_.tripleIaiIntroActive = false;
    runtime_.tripleIaiIntroTimer = 0.0f;
    ResetTripleIaiSlashClones();
    runtime_.arcaneLaserCooldown = 0.0f;
    runtime_.arcaneLaserDirection = {0.0f, 0.0f, 1.0f};
    runtime_.cataclysmLaserCooldown = 0.0f;
    runtime_.cataclysmLaserDirection = {0.0f, 0.0f, 1.0f};
    runtime_.rangedReengagePending = false;
    runtime_.sharedRangedAttackCooldown = 0.0f;
    runtime_.rangedAttackLockoutTimer = 0.0f;
    runtime_.consecutiveRangedAttackCount = 0;
    tf_.position = {0.0f, 0.0f, 10.0f};
    tf_.scale = {1.0f, 1.0f, 1.0f};
    tf_.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    ResetWarpTrails();
    visualTf_ = tf_;
    UpdateParts();
    ValidateAllTimings();
}

void Enemy::Update(const PlayerCombatObservation &playerObs, float deltaTime) {
    if (deathFinished_) {
        return;
    }

    runtime_.playerObs = playerObs;
    runtime_.playerPos = playerObs.position;
    UpdateBossPhase();
    UpdateWarpTrails(deltaTime);
    UpdateCooldowns(deltaTime);
    if (UpdateDeathSequence(deltaTime) || UpdatePhaseTransition(deltaTime)) {
        return;
    }

    if (action_.kind == ActionKind::None) {
        UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_);
    }

    stateTimer_ += deltaTime;
    isAttackActive_ = false;
    if (UpdateHitReaction(deltaTime)) {
        return;
    }
    if (tripleIaiIntroActive_) {
        UpdateTripleIaiSlashIntro(deltaTime);
        UpdateParts();
        return;
    }
    UpdateByAction(deltaTime);
    UpdateParts();
}

void Enemy::UpdateCooldowns(float deltaTime) {
    float *timers[] = {&phantomWarpCooldown_,      &tripleIaiSlashCooldown_,
                       &phantomFinalLockTimer_,    &arcaneLaserCooldown_,
                       &cataclysmLaserCooldown_,   &sharedRangedAttackCooldown_,
                       &rangedAttackLockoutTimer_, &counterRecoilTimer_,
                       &damageFlashTimer_};
    for (float *timer : timers) {
        TickNonNegative(*timer, deltaTime);
    }
}

bool Enemy::UpdateDeathSequence(float deltaTime) {
    if (!isDying_) {
        return false;
    }
    deathTimer_ += deltaTime;
    const float t = (std::min)(deathTimer_ / deathDuration_, 1.0f);

    tf_.position.y = deathStartY_ - deathSinkDistance_ * t;
    tf_.scale.x = 1.0f - 0.25f * t;
    tf_.scale.y = 1.0f - 0.55f * t;
    tf_.scale.z = 1.0f - 0.25f * t;

    UpdateParts();

    if (deathTimer_ >= deathDuration_) {
        deathFinished_ = true;
    }
    return true;
}

bool Enemy::UpdatePhaseTransition(float deltaTime) {
    if (!phaseTransitionActive_) {
        return false;
    }
    phaseTransitionTimer_ += deltaTime;
    isAttackActive_ = false;

    UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_ * 0.35f);
    UpdateParts();

    if (phaseTransitionTimer_ >= phaseTransitionDuration_) {
        phaseTransitionActive_ = false;
        phaseTransitionTimer_ = 0.0f;
        SetIsPhaseChanging(false);
        stateTimer_ = 0.0f;
        if (phase_ == BossPhase::Phase2 && runtime_.phase2BladeClashPending) {
            runtime_.phase2BladeClashPending = false;
            hitReactionTimer_ = 0.0f;
            counterRecoilTimer_ = 0.0f;
            FaceTargetImmediately(playerPos_);
            BeginAction(ActionKind::BladeClash, ActionStep::Charge);
            LockCurrentFacing();
            UpdateParts();
        } else {
            runtime_.phase2BladeClashPending = false;
        }
    }
    return true;
}

bool Enemy::UpdateHitReaction(float deltaTime) {
    if (hitReactionTimer_ <= 0.0f) {
        return false;
    }
    stateTimer_ -= deltaTime;
    if (stateTimer_ < 0.0f) {
        stateTimer_ = 0.0f;
    }

    hitReactionTimer_ -= deltaTime;
    if (hitReactionTimer_ < 0.0f) {
        hitReactionTimer_ = 0.0f;
    }

    UpdateParts();
    return true;
}

void Enemy::UpdateTutorial(const PlayerCombatObservation &playerObs,
                           float deltaTime) {
    if (deathFinished_) {
        return;
    }

    runtime_.playerObs = playerObs;
    runtime_.playerPos = playerObs.position;
    UpdateWarpTrails(deltaTime);

    if (counterRecoilTimer_ > 0.0f) {
        counterRecoilTimer_ -= deltaTime;
        if (counterRecoilTimer_ < 0.0f) {
            counterRecoilTimer_ = 0.0f;
        }
    }
    if (damageFlashTimer_ > 0.0f) {
        damageFlashTimer_ = (std::max)(0.0f, damageFlashTimer_ - deltaTime);
    }

    if (action_.kind == ActionKind::None) {
        UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_);
        UpdateParts();
        return;
    }

    stateTimer_ += deltaTime;
    isAttackActive_ = false;

    if (hitReactionTimer_ > 0.0f) {
        stateTimer_ -= deltaTime;
        if (stateTimer_ < 0.0f) {
            stateTimer_ = 0.0f;
        }

        hitReactionTimer_ -= deltaTime;
        if (hitReactionTimer_ < 0.0f) {
            hitReactionTimer_ = 0.0f;
        }

        UpdateParts();
        return;
    }

    switch (action_.kind) {
    case ActionKind::Smash:
        UpdateSmashByStep(deltaTime);
        break;
    case ActionKind::Sweep:
        UpdateSweepByStep(deltaTime);
        break;
    default:
        EndAttack();
        break;
    }
    UpdateParts();
}

void Enemy::BeginTutorialAttack(ActionKind kind) {
    if (kind != ActionKind::Smash && kind != ActionKind::Sweep) {
        kind = ActionKind::Smash;
    }
    BeginAction(kind, ActionStep::Charge);
}

bool Enemy::ConsumeAttackCueEvent(EnemyAttackCueEvent &event) {
    if (pendingAttackCue_.type == EnemyAttackCueType::None) {
        return false;
    }

    event = pendingAttackCue_;
    pendingAttackCue_ = {};
    return true;
}

void Enemy::ResetTutorialState() {
    EndAttack();
    runtime_.hp = config_.core.maxHp;
    runtime_.phase = BossPhase::Phase1;
    runtime_.phaseTransitionActive = false;
    runtime_.phaseTransitionTimer = 0.0f;
    runtime_.phase2BladeClashPending = false;
    runtime_.arcaneLaserCooldown = 0.0f;
    runtime_.tripleIaiSlashCooldown = 0.0f;
    runtime_.tripleIaiSlashActive = false;
    runtime_.tripleIaiSlashesRemaining = 0;
    runtime_.tripleIaiSlashIndex = 0;
    runtime_.tripleIaiIntroActive = false;
    runtime_.tripleIaiIntroTimer = 0.0f;
    ResetTripleIaiSlashClones();
    runtime_.arcaneLaserDirection = {0.0f, 0.0f, 1.0f};
    runtime_.cataclysmLaserCooldown = 0.0f;
    runtime_.cataclysmLaserDirection = {0.0f, 0.0f, 1.0f};
    runtime_.sharedRangedAttackCooldown = 0.0f;
    runtime_.rangedAttackLockoutTimer = 0.0f;
    runtime_.consecutiveRangedAttackCount = 0;
    runtime_.hitReactionTimer = 0.0f;
    runtime_.damageFlashTimer = 0.0f;
    runtime_.counterRecoilTimer = 0.0f;
    runtime_.isDying = false;
    runtime_.deathFinished = false;
    runtime_.deathTimer = 0.0f;
    ResetWarpTrails();
    UpdateParts();
}

void Enemy::DebugForceBossPhase(BossPhase phase, bool playTransition) {
    if (deathFinished_ || isDying_ || config_.core.maxHp <= 0.0f) {
        return;
    }

    EndAttack();
    tripleIaiSlashActive_ = false;
    tripleIaiSlashesRemaining_ = 0;
    tripleIaiSlashIndex_ = 0;
    tripleIaiIntroActive_ = false;
    tripleIaiIntroTimer_ = 0.0f;
    ResetTripleIaiSlashClones();
    hitReactionTimer_ = 0.0f;
    damageFlashTimer_ = 0.0f;
    counterRecoilTimer_ = 0.0f;

    float hpRatio = 1.0f;
    switch (phase) {
    case BossPhase::Phase1:
        hpRatio = 1.0f;
        break;
    case BossPhase::Phase2:
        hpRatio = (config_.core.phase2HealthRatioThreshold +
                   config_.core.phase3HealthRatioThreshold) *
                  0.5f;
        break;
    case BossPhase::Phase3:
        hpRatio = config_.core.phase3HealthRatioThreshold * 0.5f;
        break;
    }
    hp_ = (std::max)(1.0f, config_.core.maxHp * hpRatio);

    phase_ = phase;
    phaseTransitionActive_ = playTransition;
    phaseTransitionTimer_ = 0.0f;
    runtime_.phase2BladeClashPending = false;
    SetIsPhaseChanging(playTransition);
    stateTimer_ = 0.0f;
    UpdateFacingToPlayer();
    UpdateParts();
}

void Enemy::SetTutorialPosition(const DirectX::XMFLOAT3 &position) {
    tf_.position = position;
    visualTf_ = tf_;
    UpdateParts();
}

void Enemy::SetCinematicTransform(const DirectX::XMFLOAT3 &position,
                                  float yaw) {
    SetCinematicTransform(position, yaw, 0.0f, 0.0f);
}

void Enemy::SetCinematicTransform(const DirectX::XMFLOAT3 &position, float yaw,
                                  float pitch, float roll) {
    tf_.position = position;
    facingYaw_ = yaw;
    lockedAttackYaw_ = yaw;
    cinematicPitch_ = pitch;
    cinematicRoll_ = roll;
    DirectX::XMVECTOR rot =
        DirectX::XMQuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f);
    DirectX::XMStoreFloat4(&tf_.rotation, rot);
    UpdateParts();
}

void Enemy::UpdateByAction(float deltaTime) {
    if (action_.kind == ActionKind::None) {
        UpdateIdle(deltaTime);
        return;
    }

    switch (action_.kind) {
    case ActionKind::Smash:
        UpdateSmashByStep(deltaTime);
        break;
    case ActionKind::Sweep:
        UpdateSweepByStep(deltaTime);
        break;
    case ActionKind::BladeClash:
        UpdateBladeClashByStep(deltaTime);
        break;
    case ActionKind::Warp:
        UpdateWarpByStep(deltaTime);
        break;
    case ActionKind::Stalk:
        UpdateStalkByStep(deltaTime);
        break;
    case ActionKind::ArcaneLaser:
        UpdateArcaneLaserByStep(deltaTime);
        break;
    case ActionKind::CataclysmLaser:
        UpdateCataclysmLaserByStep(deltaTime);
        break;
    default:
        UpdateIdle(deltaTime);
        break;
    }
}

void Enemy::BeginAction(ActionKind kind, ActionStep step) {
    lastActionKind_ = kind;

    if (SupportsMeleeAttackCue(kind)) {
        rangedReengagePending_ = false;
    }

    if (kind == ActionKind::Warp) {
        ResetWarpTrails();
    } else {
        ResetWarpContext();
    }

    action_.kind = kind;

    action_.step = step;
    hasTrackingLocked_ = false;
    holdConfigured_ = false;
    currentHoldDuration_ = 0.0f;
    quickSlashActive_ = false;
    farSlashActive_ = false;
    farSlashLungeDuration_ = 0.0f;
    hasFarSlashLungeTarget_ = false;
    farSlashLungeStartPos_ = {0.0f, 0.0f, 0.0f};
    farSlashLungeTargetPos_ = {0.0f, 0.0f, 0.0f};
    warpFeintFollowupLocked_ = false;
    warpFeintImmediate_ = false;
    warpFeintDecisionMade_ = false;
    directionFeintDecisionMade_ = false;
    attackReleaseCueIssued_ = false;
    lockedAttackYaw_ = facingYaw_;
    arcaneLaserDirection_ = {std::sin(facingYaw_), 0.0f, std::cos(facingYaw_)};
    cataclysmLaserDirection_ = arcaneLaserDirection_;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;
    cinematicPitch_ = 0.0f;
    cinematicRoll_ = 0.0f;
    ResetPreAttackPresentationState();

    IssueBeginActionCue(kind, step);
}

void Enemy::IssueBeginActionCue(ActionKind kind, ActionStep step) {
    const bool supportsAttackCue = SupportsMeleeAttackCue(kind);
    if (supportsAttackCue && step == ActionStep::Charge) {
        const float chargeTime =
            kind == ActionKind::Smash ? GetCurrentSmashChargeTime()
            : kind == ActionKind::Sweep
                ? GetCurrentSweepChargeTime()
                : config_.attacks.bladeClash.profile.chargeTime;
        const EnemyAttackCueType cueType = kind == ActionKind::BladeClash
                                               ? EnemyAttackCueType::Release
                                               : EnemyAttackCueType::Telegraph;
        const float tellTime = kind == ActionKind::Smash   ? smashTellTime_
                               : kind == ActionKind::Sweep ? sweepTellTime_
                                                           : 0.0f;
        IssueAttackCue(cueType, kind, chargeTime + tellTime);
    } else if (supportsAttackCue && step == ActionStep::Active) {
        IssueAttackCue(EnemyAttackCueType::Release, kind, 0.0f);
    } else if (!supportsAttackCue) {
        IssueAttackCue(EnemyAttackCueType::Cancel, ActionKind::None, 0.0f);
    }
}

void Enemy::ChangeActionStep(ActionStep step) {
    const ActionStep previousStep = action_.step;
    action_.step = step;
    isAttackActive_ = false;
    stateTimer_ = 0.0f;

    const bool usesLockedAttackYaw = UsesLockedAttackYaw(action_.kind);
    const bool enteringActive =
        step == ActionStep::Active && previousStep != ActionStep::Active;
    if (enteringActive && usesLockedAttackYaw && !hasTrackingLocked_) {
        LockCurrentFacing();
        hasTrackingLocked_ = true;
    }
    if (enteringActive && action_.kind == ActionKind::ArcaneLaser) {
        arcaneLaserDirection_ = {std::sin(lockedAttackYaw_), 0.0f,
                                 std::cos(lockedAttackYaw_)};
    }
    if (enteringActive && action_.kind == ActionKind::CataclysmLaser) {
        cataclysmLaserDirection_ = {std::sin(lockedAttackYaw_), 0.0f,
                                    std::cos(lockedAttackYaw_)};
    }

    if (step != ActionStep::Charge && step != ActionStep::Hold) {
        ResetPreAttackPresentationState();
    }

    if (SupportsMeleeAttackCue(action_.kind) && step == ActionStep::Active &&
        previousStep != ActionStep::Active && !attackReleaseCueIssued_) {
        IssueAttackCue(EnemyAttackCueType::Release, action_.kind, 0.0f);
        attackReleaseCueIssued_ = true;
    }
}

void Enemy::EndAttack() {
    const bool endedRangedAttack = action_.kind == ActionKind::ArcaneLaser ||
                                   action_.kind == ActionKind::CataclysmLaser ||
                                   farSlashActive_;
    const bool keepTripleIaiChain =
        tripleIaiSlashActive_ && tripleIaiSlashesRemaining_ > 0 &&
        ((farSlashActive_ && (action_.kind == ActionKind::Smash ||
                              action_.kind == ActionKind::Sweep)) ||
         (action_.kind == ActionKind::Warp && warp_.farSlashFollowup));

    action_.kind = ActionKind::None;
    action_.step = ActionStep::None;
    IssueAttackCue(EnemyAttackCueType::Cancel, ActionKind::None, 0.0f);

    ResetWarpContext();
    isVisible_ = true;

    hasTrackingLocked_ = false;
    holdConfigured_ = false;
    currentHoldDuration_ = 0.0f;
    quickSlashActive_ = false;
    farSlashActive_ = false;
    farSlashLungeDuration_ = 0.0f;
    hasFarSlashLungeTarget_ = false;
    farSlashLungeStartPos_ = {0.0f, 0.0f, 0.0f};
    farSlashLungeTargetPos_ = {0.0f, 0.0f, 0.0f};
    warpFeintFollowupLocked_ = false;
    warpFeintImmediate_ = false;
    warpFeintDecisionMade_ = false;
    directionFeintDecisionMade_ = false;
    attackReleaseCueIssued_ = false;
    isAttackActive_ = false;
    arcaneLaserDirection_ = {0.0f, 0.0f, 1.0f};
    cataclysmLaserDirection_ = {0.0f, 0.0f, 1.0f};
    stateTimer_ = 0.0f;
    cinematicPitch_ = 0.0f;
    cinematicRoll_ = 0.0f;
    if (endedRangedAttack) {
        sharedRangedAttackCooldown_ =
            (std::max)(sharedRangedAttackCooldown_,
                       sharedRangedAttackCooldownDuration_);
    }
    if (!keepTripleIaiChain) {
        tripleIaiSlashActive_ = false;
        tripleIaiSlashesRemaining_ = 0;
        tripleIaiSlashIndex_ = 0;
        tripleIaiIntroActive_ = false;
        tripleIaiIntroTimer_ = 0.0f;
        ResetTripleIaiSlashClones();
    }

    ResetPreAttackPresentationState();
}

void Enemy::IssueAttackCue(EnemyAttackCueType type, ActionKind kind,
                           float duration) {
    if (type == EnemyAttackCueType::None) {
        return;
    }

    if (type == EnemyAttackCueType::Cancel) {
        pendingAttackCue_ = {};
        pendingAttackCue_.type = type;
        pendingAttackCue_.sequence = ++attackCueSequence_;
        return;
    }

    if (kind != ActionKind::Smash && kind != ActionKind::Sweep &&
        kind != ActionKind::BladeClash) {
        return;
    }

    pendingAttackCue_.type = type;
    pendingAttackCue_.kind = kind;
    pendingAttackCue_.yaw =
        ShouldUseLockedAttackYaw() ? lockedAttackYaw_ : GetTelegraphYaw();
    pendingAttackCue_.duration = type == EnemyAttackCueType::Release
                                     ? (std::max)(duration, 0.0f)
                                     : (std::max)(duration, 0.12f);
    pendingAttackCue_.sequence = ++attackCueSequence_;
}

void Enemy::IssueReleaseCueIfReady() {
    if (attackReleaseCueIssued_) {
        return;
    }
    if (action_.kind != ActionKind::Smash &&
        action_.kind != ActionKind::Sweep) {
        return;
    }
    if (action_.step != ActionStep::Charge &&
        action_.step != ActionStep::Hold) {
        return;
    }

    const float releaseAnticipation = GetReleaseAnticipationRatio();
    if (releaseAnticipation <= 0.0f) {
        return;
    }

    float releaseTime = 0.0f;
    if (action_.step == ActionStep::Charge) {
        releaseTime = action_.kind == ActionKind::Smash
                          ? GetCurrentSmashChargeTime()
                          : GetCurrentSweepChargeTime();
    } else {
        releaseTime = currentHoldDuration_;
    }

    const float remainingToRelease =
        (std::max)(0.0f, releaseTime - stateTimer_);
    IssueAttackCue(EnemyAttackCueType::Release, action_.kind,
                   remainingToRelease);
    attackReleaseCueIssued_ = true;
}

void Enemy::UpdateBossPhase() {
    if (phase_ == BossPhase::Phase3 || phaseTransitionActive_ ||
        config_.core.maxHp <= 0.0f) {
        return;
    }

    const float hpRatio = hp_ / config_.core.maxHp;
    BossPhase nextPhase = phase_;
    if (hpRatio <= config_.core.phase3HealthRatioThreshold) {
        nextPhase = BossPhase::Phase3;
    } else if (hpRatio <= config_.core.phase2HealthRatioThreshold) {
        nextPhase = BossPhase::Phase2;
    }

    if (nextPhase != phase_) {
        EndAttack();
        tripleIaiSlashActive_ = false;
        tripleIaiSlashesRemaining_ = 0;
        tripleIaiSlashIndex_ = 0;
        tripleIaiIntroActive_ = false;
        tripleIaiIntroTimer_ = 0.0f;
        ResetTripleIaiSlashClones();
        hitReactionTimer_ = 0.0f;
        counterRecoilTimer_ = 0.0f;
        SetIsPhaseChanging(true);
        phase_ = nextPhase;
        phaseTransitionActive_ = true;
        phaseTransitionTimer_ = 0.0f;
        if (nextPhase == BossPhase::Phase2) {
            runtime_.phase2BladeClashPending = true;
        }
        stateTimer_ = 0.0f;
        UpdateFacingToPlayer();
        UpdateParts();
    }
}
