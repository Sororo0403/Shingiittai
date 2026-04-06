//#include "Enemy.h"
//#include "ModelManager.h"
//#include "imgui.h"
//
//#include <algorithm>
//#include <cmath>
//#include <cstdlib>
//
//// ============================================================
//// 初期化処理
//// ============================================================
//void Enemy::Initialize(uint32_t modelId) {
//    modelId_ = modelId;
//
//    tf_.position = {0.0f, 0.0f, 10.0f};
//    tf_.scale = {1.0f, 1.0f, 1.0f};
//    tf_.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
//
//    UpdateParts();
//    ValidateAllTimings();
//}
//
//// ============================================================
//// 毎フレーム更新処理（旧互換）
//// ============================================================
//void Enemy::Update(const DirectX::XMFLOAT3 &playerPos, float deltaTime,
//                   bool playerGuarding) {
//    PlayerCombatObservation obs{};
//    obs.position = playerPos;
//    obs.isGuarding = playerGuarding;
//    Update(obs, deltaTime);
//}
//
//// ============================================================
//// 毎フレーム更新処理（新版）
//// ============================================================
//void Enemy::Update(const PlayerCombatObservation &playerObs, float deltaTime) {
//    if (!IsAlive()) {
//        return;
//    }
//
//    playerObs_ = playerObs;
//    playerPos_ = playerObs.position;
//    playerGuarding_ = playerObs.isGuarding;
//
//    float currentDistance = GetDistanceToPlayer();
//    float distanceDelta = std::fabs(currentDistance - lastDistanceToPlayer_);
//
//    if (distanceDelta < stagnantDistanceThreshold_) {
//        stagnantTimer_ += deltaTime;
//    } else {
//        stagnantTimer_ = 0.0f;
//    }
//    isDistanceStagnant_ = (stagnantTimer_ >= stagnantTimeThreshold_);
//
//    if (currentDistance <= closePressureDistance_) {
//        closePressureTimer_ += deltaTime;
//        if (closePressureTimer_ > closePressureTimeThreshold_) {
//            closePressureTimer_ = closePressureTimeThreshold_;
//        }
//    } else {
//        closePressureTimer_ -= deltaTime;
//        if (closePressureTimer_ < 0.0f) {
//            closePressureTimer_ = 0.0f;
//        }
//    }
//
//    if (currentDistance > farAttackDistance_) {
//        farDistanceTimer_ += deltaTime;
//    } else {
//        farDistanceTimer_ = 0.0f;
//    }
//
//    if (warpEscapeCooldownTimer_ > 0.0f) {
//        warpEscapeCooldownTimer_ -= deltaTime;
//        if (warpEscapeCooldownTimer_ < 0.0f) {
//            warpEscapeCooldownTimer_ = 0.0f;
//        }
//    }
//
//    lastDistanceToPlayer_ = currentDistance;
//
//    if (action_.kind == ActionKind::None) {
//        UpdateFacingToPlayerWithSpeed(deltaTime, idleTurnSpeed_);
//    }
//
//    stateTimer_ += deltaTime;
//
//    isAttackActive_ = false;
//    isGuardActive_ = false;
//
//    UpdateCounterAdaptation(deltaTime);
//
//    // ------------------------------------------------------------
//    // カウンター成功リアクション
//    // ------------------------------------------------------------
//    if (playerObs_.justCountered) {
//        RegisterCounterSuccessReaction();
//
//        const bool isCounterBreakableAction =
//            (action_.kind == ActionKind::Smash) ||
//            (action_.kind == ActionKind::Sweep) ||
//            (action_.kind == ActionKind::Rush);
//
//        if (isCounterBreakableAction) {
//            float dx = tf_.position.x - playerPos_.x;
//            float dz = tf_.position.z - playerPos_.z;
//            float len = std::sqrtf(dx * dx + dz * dz);
//            if (len < 0.0001f) {
//                len = 1.0f;
//            }
//
//            dx /= len;
//            dz /= len;
//
//            const float counterPushBack = 0.9f;
//            tf_.position.x += dx * counterPushBack;
//            tf_.position.z += dz * counterPushBack;
//
//            EndAttack();
//            ResetChainContext();
//            ResetPostActionState();
//
//            stateTimer_ = -0.20f;
//
//            tactic_ = TacticState::Reset;
//            closePressureTimer_ = 0.0f;
//            stagnantTimer_ = 0.0f;
//            isDistanceStagnant_ = false;
//
//            UpdateFacingToPlayer();
//            UpdateParts();
//            return;
//        }
//    }
//
//    UpdateByAction(deltaTime);
//
//    UpdateBullets(deltaTime);
//    UpdateWaves(deltaTime);
//
//    UpdateParts();
//}
//
//// ============================================================
//// 描画処理
//// ============================================================
//void Enemy::Draw(ModelManager *modelManager, const Camera &camera) {
//    if (!IsAlive()) {
//        return;
//    }
//
//    if (isVisible_) {
//        modelManager->Draw(modelId_, bodyTf_, camera);
//        modelManager->Draw(modelId_, leftHandTf_, camera);
//        modelManager->Draw(modelId_, rightHandTf_, camera);
//    }
//
//    for (const auto &bullet : bullets_) {
//        if (!bullet.isAlive) {
//            continue;
//        }
//
//        Transform bulletTf = tf_;
//        bulletTf.position = bullet.position;
//        bulletTf.scale = {0.2f, 0.2f, 0.2f};
//
//        modelManager->Draw(modelId_, bulletTf, camera);
//    }
//
//    for (const auto &wave : waves_) {
//        if (!wave.isAlive) {
//            continue;
//        }
//
//        Transform waveTf = tf_;
//        waveTf.position = wave.position;
//        waveTf.scale = {0.6f, 0.2f, 1.2f};
//
//        modelManager->Draw(modelId_, waveTf, camera);
//    }
//}
//
//// ============================================================
//// 被ダメージ処理
//// ============================================================
//void Enemy::TakeDamage(float damage) {
//    hp_ -= damage;
//
//    if (hp_ < 0.0f) {
//        hp_ = 0.0f;
//    }
//}
//
//// ============================================================
//// 各部位Transform更新処理
//// ============================================================
//void Enemy::UpdateParts() {
//    float usedYaw = GetVisualYaw();
//
//    float forwardX = std::sinf(usedYaw);
//    float forwardZ = std::cosf(usedYaw);
//
//    float rightX = std::cosf(usedYaw);
//    float rightZ = -std::sinf(usedYaw);
//
//    bodyTf_ = tf_;
//    bodyTf_.position = tf_.position;
//    bodyTf_.scale = {1.2f, 1.4f, 0.8f};
//
//    leftHandTf_ = tf_;
//    leftHandTf_.position = tf_.position;
//    leftHandTf_.position.x += (-rightX) * 1.2f;
//    leftHandTf_.position.y += 0.9f;
//    leftHandTf_.position.z += (-rightZ) * 1.2f;
//    leftHandTf_.scale = {0.6f, 0.6f, 0.6f};
//
//    rightHandTf_ = tf_;
//    rightHandTf_.position = tf_.position;
//    rightHandTf_.position.x += rightX * 1.2f;
//    rightHandTf_.position.y += 0.9f;
//    rightHandTf_.position.z += rightZ * 1.2f;
//    rightHandTf_.scale = {0.6f, 0.6f, 0.6f};
//
//    if (action_.kind == ActionKind::Smash) {
//        if (action_.step == ActionStep::Charge ||
//            action_.step == ActionStep::Hold) {
//            rightHandTf_.position.y += 1.5f;
//            rightHandTf_.position.x += (-forwardX) * 0.5f;
//            rightHandTf_.position.z += (-forwardZ) * 0.5f;
//        } else if (action_.step == ActionStep::Active) {
//            rightHandTf_.position.y -= 0.2f;
//            rightHandTf_.position.x += forwardX * 1.8f;
//            rightHandTf_.position.z += forwardZ * 1.8f;
//        } else if (action_.step == ActionStep::Recovery) {
//            rightHandTf_.position.y += 0.3f;
//            rightHandTf_.position.x += forwardX * 0.8f;
//            rightHandTf_.position.z += forwardZ * 0.8f;
//        }
//
//    } else if (action_.kind == ActionKind::Sweep) {
//        if (action_.step == ActionStep::Charge ||
//            action_.step == ActionStep::Hold) {
//            rightHandTf_.position.x += rightX * 1.4f;
//            rightHandTf_.position.y += 0.4f;
//            rightHandTf_.position.z += rightZ * 1.4f;
//        } else if (action_.step == ActionStep::Active) {
//            rightHandTf_.position.x += (-rightX) * 1.6f;
//            rightHandTf_.position.y += 0.2f;
//            rightHandTf_.position.z += (-rightZ) * 1.6f;
//        } else if (action_.step == ActionStep::Recovery) {
//            rightHandTf_.position.x += rightX * 0.3f;
//            rightHandTf_.position.y += 0.1f;
//            rightHandTf_.position.z += rightZ * 0.3f;
//        }
//
//    } else if (action_.kind == ActionKind::Shot) {
//        if (action_.step == ActionStep::Charge) {
//            rightHandTf_.position.y += 0.5f;
//            rightHandTf_.position.x += forwardX * 0.8f;
//            rightHandTf_.position.z += forwardZ * 0.8f;
//        } else if (action_.step == ActionStep::Active) {
//            rightHandTf_.position.y += 0.3f;
//            rightHandTf_.position.x += forwardX * 1.0f;
//            rightHandTf_.position.z += forwardZ * 1.0f;
//        } else if (action_.step == ActionStep::Recovery) {
//            rightHandTf_.position.y += 0.2f;
//            rightHandTf_.position.x += forwardX * 0.4f;
//            rightHandTf_.position.z += forwardZ * 0.4f;
//        }
//
//    } else if (action_.kind == ActionKind::Wave) {
//        if (action_.step == ActionStep::Charge) {
//            rightHandTf_.position.y += 0.8f;
//            rightHandTf_.position.x += forwardX * 0.6f;
//            rightHandTf_.position.z += forwardZ * 0.6f;
//        } else if (action_.step == ActionStep::Active) {
//            rightHandTf_.position.y += 0.4f;
//            rightHandTf_.position.x += forwardX * 1.0f;
//            rightHandTf_.position.z += forwardZ * 1.0f;
//        } else if (action_.step == ActionStep::Recovery) {
//            rightHandTf_.position.y += 0.2f;
//            rightHandTf_.position.x += forwardX * 0.4f;
//            rightHandTf_.position.z += forwardZ * 0.4f;
//        }
//
//    } else if (action_.kind == ActionKind::Rush) {
//        if (action_.step == ActionStep::Charge) {
//            rightHandTf_.position.y += 0.4f;
//            rightHandTf_.position.x += forwardX * 0.8f;
//            rightHandTf_.position.z += forwardZ * 0.8f;
//        } else if (action_.step == ActionStep::Active) {
//            rightHandTf_.position.y += 0.1f;
//            rightHandTf_.position.x += forwardX * 1.6f;
//            rightHandTf_.position.z += forwardZ * 1.6f;
//        } else if (action_.step == ActionStep::Recovery) {
//            rightHandTf_.position.y += 0.2f;
//            rightHandTf_.position.x += forwardX * 0.6f;
//            rightHandTf_.position.z += forwardZ * 0.6f;
//        }
//
//    } else if (action_.kind == ActionKind::Warp) {
//        if (action_.step == ActionStep::End) {
//            rightHandTf_.position.y += 0.2f;
//            rightHandTf_.position.x += forwardX * 0.3f;
//            rightHandTf_.position.z += forwardZ * 0.3f;
//        }
//
//    } else if (action_.kind == ActionKind::Guard) {
//        if (guardTarget_ == GuardTarget::Face) {
//            leftHandTf_.position.x += forwardX * 0.6f;
//            leftHandTf_.position.y += 0.9f;
//            leftHandTf_.position.z += forwardZ * 0.6f;
//
//        } else if (guardTarget_ == GuardTarget::BodyLeft) {
//            leftHandTf_.position.x += (-rightX) * 0.35f;
//            leftHandTf_.position.y += 0.3f;
//            leftHandTf_.position.z += (-rightZ) * 0.35f;
//
//        } else if (guardTarget_ == GuardTarget::BodyRight) {
//            leftHandTf_.position.x += rightX * 0.35f;
//            leftHandTf_.position.y += 0.3f;
//            leftHandTf_.position.z += rightZ * 0.35f;
//        }
//    }
//}
//
//// ============================================================
//// OBB生成共通処理
//// ============================================================
//OBB Enemy::MakeOBB(const Transform &tf, const DirectX::XMFLOAT3 &size) const {
//    OBB box{};
//    box.center = tf.position;
//    box.center.y += size.y * 0.5f;
//    box.size = size;
//    box.rotation = tf.rotation;
//    return box;
//}
//
//// ============================================================
//// 各部位の当たり判定取得
//// ============================================================
//OBB Enemy::GetBodyOBB() const { return MakeOBB(bodyTf_, bodySize_); }
//OBB Enemy::GetLeftHandOBB() const { return MakeOBB(leftHandTf_, handSize_); }
//OBB Enemy::GetRightHandOBB() const { return MakeOBB(rightHandTf_, handSize_); }
//
//// ============================================================
//// 攻撃用OBB取得
//// ============================================================
//OBB Enemy::GetAttackOBB() const {
//    switch (action_.kind) {
//    case ActionKind::Smash:
//        return GetSmashAttackOBB();
//    case ActionKind::Sweep:
//        return GetSweepAttackOBB();
//    case ActionKind::Rush:
//        return GetRushAttackOBB();
//    default:
//        return OBB{};
//    }
//}
//
//OBB Enemy::GetSmashAttackOBB() const {
//    float usedYaw = ShouldUseLockedAttackYaw() ? lockedAttackYaw_ : facingYaw_;
//
//    float forwardX = std::sinf(usedYaw);
//    float forwardZ = std::cosf(usedYaw);
//
//    Transform attackTf{};
//    attackTf.scale = {1.0f, 1.0f, 1.0f};
//    attackTf.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
//    attackTf.position = bodyTf_.position;
//    attackTf.position.x += forwardX * smashAttackForwardOffset_;
//    attackTf.position.y += smashAttackHeightOffset_;
//    attackTf.position.z += forwardZ * smashAttackForwardOffset_;
//
//    return MakeOBB(attackTf, GetCurrentAttackHitBoxSize());
//}
//
//OBB Enemy::GetSweepAttackOBB() const {
//    float usedYaw = ShouldUseLockedAttackYaw() ? lockedAttackYaw_ : facingYaw_;
//
//    float rightX = std::cosf(usedYaw);
//    float rightZ = -std::sinf(usedYaw);
//
//    Transform attackTf{};
//    attackTf.scale = {1.0f, 1.0f, 1.0f};
//    attackTf.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
//    attackTf.position = bodyTf_.position;
//    attackTf.position.x += rightX * sweepAttackSideOffset_;
//    attackTf.position.y += sweepAttackHeightOffset_;
//    attackTf.position.z += rightZ * sweepAttackSideOffset_;
//
//    return MakeOBB(attackTf, GetCurrentAttackHitBoxSize());
//}
//
//OBB Enemy::GetRushAttackOBB() const {
//    float usedYaw = rushCurrentYaw_;
//
//    float forwardX = std::sinf(usedYaw);
//    float forwardZ = std::cosf(usedYaw);
//
//    Transform attackTf{};
//    attackTf.scale = {1.0f, 1.0f, 1.0f};
//    attackTf.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
//    attackTf.position = bodyTf_.position;
//    attackTf.position.x += forwardX * rushAttackForwardOffset_;
//    attackTf.position.y += rushAttackHeightOffset_;
//    attackTf.position.z += forwardZ * rushAttackForwardOffset_;
//
//    return MakeOBB(attackTf, GetCurrentAttackHitBoxSize());
//}
//
//float Enemy::GetVisualYaw() const {
//    if (action_.kind == ActionKind::Rush) {
//        return rushCurrentYaw_;
//    }
//
//    if (ShouldUseLockedAttackYaw()) {
//        return lockedAttackYaw_;
//    }
//    return facingYaw_;
//}
//
//float Enemy::GetCurrentAttackDamage() const {
//    const AttackParam *param = GetCurrentAttackParam();
//    if (!param) {
//        return 0.0f;
//    }
//    return param->damage;
//}
//
//float Enemy::GetCurrentAttackKnockback() const {
//    const AttackParam *param = GetCurrentAttackParam();
//    if (!param) {
//        return 0.0f;
//    }
//    return param->knockback;
//}
//
//DirectX::XMFLOAT3 Enemy::GetCurrentAttackHitBoxSize() const {
//    const AttackParam *param = GetCurrentAttackParam();
//    if (!param) {
//        return {0.1f, 0.1f, 0.1f};
//    }
//    return param->hitBoxSize;
//}
//
//// ============================================================
//// プレイヤーとの距離計算
//// ============================================================
//float Enemy::GetDistanceToPlayer() const {
//    float dx = playerPos_.x - tf_.position.x;
//    float dy = playerPos_.y - tf_.position.y;
//    float dz = playerPos_.z - tf_.position.z;
//
//    return std::sqrtf(dx * dx + dy * dy + dz * dz);
//}
//
//// ============================================================
//// Idle更新
//// ============================================================
//void Enemy::UpdateIdle(float deltaTime) {
//    (void)deltaTime;
//
//    if (stateTimer_ < 0.35f) {
//        return;
//    }
//
//    // Step3:
//    // Recovery からの再始動予約がある場合はそれを優先
//    if (recoveryFollowupKind_ != ActionKind::None &&
//        recoveryFollowupStep_ != ActionStep::None) {
//        ActionKind nextKind = recoveryFollowupKind_;
//        ActionStep nextStep = recoveryFollowupStep_;
//
//        ResetRecoveryBranchState();
//        BeginAction(nextKind, nextStep);
//        return;
//    }
//
//    tactic_ = DecideTactic();
//    BeginActionFromTactic(tactic_);
//}
//
//TacticState Enemy::DecideTactic() const {
//    float distance = GetDistanceToPlayer();
//
//    if (forceEscapeWarpNext_) {
//        return TacticState::Reset;
//    }
//
//    if (IsCounterFailObserved()) {
//        return TacticState::CounterPunish;
//    }
//
//    if (counterMemory_.successCount >= 1.6f && distance <= farAttackDistance_) {
//        return TacticState::CounterBait;
//    }
//
//    if ((playerObs_.isCounterStance ||
//         counterMemory_.counterStancePressure >= 0.8f) &&
//        distance <= farAttackDistance_) {
//        return TacticState::CounterBait;
//    }
//
//    if (playerObs_.isGuarding) {
//        return TacticState::AntiGuard;
//    }
//
//    if (distance <= nearAttackDistance_) {
//        return TacticState::Pressure;
//    }
//
//    if (distance > farAttackDistance_) {
//        return TacticState::Chase;
//    }
//
//    return TacticState::Neutral;
//}
//
//void Enemy::BeginActionFromTactic(TacticState tactic) {
//    switch (tactic) {
//    case TacticState::Pressure:
//        BeginPressureAction();
//        break;
//    case TacticState::CounterBait:
//        BeginCounterBaitAction();
//        break;
//    case TacticState::CounterPunish:
//        BeginCounterPunishAction();
//        break;
//    case TacticState::AntiGuard:
//        BeginAntiGuardAction();
//        break;
//    case TacticState::Chase:
//        BeginChaseAction();
//        break;
//    case TacticState::Reset:
//        BeginResetAction();
//        break;
//    case TacticState::Neutral:
//    default:
//        BeginPressureAction();
//        break;
//    }
//}
//
//void Enemy::BeginPressureAction() {
//    float distance = GetDistanceToPlayer();
//
//    if (distance <= nearAttackDistance_) {
//        int smashWeight = nearSmashWeight_;
//        int sweepWeight = nearSweepWeight_;
//        int guardWeight = nearGuardWeight_;
//        int rushWeight = nearRushWeight_;
//
//        if (postCounterRhythmTimer_ > 0.0f) {
//            smashWeight = static_cast<int>(smashWeight * 0.7f);
//            sweepWeight = static_cast<int>(sweepWeight * 0.7f);
//            guardWeight += 5;
//            rushWeight += 8;
//        }
//
//        if (lastActionKind_ == ActionKind::Smash) {
//            smashWeight /= 2;
//        } else if (lastActionKind_ == ActionKind::Sweep) {
//            sweepWeight /= 2;
//        } else if (lastActionKind_ == ActionKind::Guard) {
//            guardWeight /= 2;
//        } else if (lastActionKind_ == ActionKind::Rush) {
//            rushWeight /= 2;
//        }
//
//        int total = smashWeight + sweepWeight + guardWeight + rushWeight;
//        if (total <= 0) {
//            total = 1;
//        }
//
//        int r = std::rand() % total;
//
//        if (r < smashWeight) {
//            BeginAction(ActionKind::Smash, ActionStep::Charge);
//        } else if (r < smashWeight + sweepWeight) {
//            BeginAction(ActionKind::Sweep, ActionStep::Charge);
//        } else if (r < smashWeight + sweepWeight + guardWeight) {
//            DecideGuardTarget();
//            BeginAction(ActionKind::Guard, ActionStep::Move);
//        } else {
//            BeginAction(ActionKind::Rush, ActionStep::Charge);
//        }
//        return;
//    }
//
//    BeginChaseAction();
//}
//
//void Enemy::BeginCounterBaitAction() {
//    float distance = GetDistanceToPlayer();
//
//    if (distance > farAttackDistance_) {
//        BeginChaseAction();
//        return;
//    }
//
//    int guardWeight = counterBaitGuardWeight_;
//    int feintMeleeBonus = 0;
//
//    if (counterMemory_.counterStancePressure > 0.8f) {
//        feintMeleeBonus += 10;
//    }
//    if (counterMemory_.successCount > 0.8f) {
//        feintMeleeBonus += 10;
//    }
//
//    ActionKind baitKind = DecideAdaptiveCounterBaitAction();
//
//    int baitWeight = 20 + feintMeleeBonus;
//    int total = baitWeight + guardWeight;
//    if (total <= 0) {
//        total = 1;
//    }
//
//    int r = std::rand() % total;
//
//    if (r < baitWeight) {
//        BeginAction(baitKind, ActionStep::Charge);
//    } else {
//        DecideGuardTarget();
//        BeginAction(ActionKind::Guard, ActionStep::Move);
//    }
//}
//
//void Enemy::BeginCounterPunishAction() {
//    float distance = GetDistanceToPlayer();
//
//    if (distance > farAttackDistance_) {
//        if (PrepareWarpContext()) {
//            BeginAction(ActionKind::Warp, ActionStep::Start);
//        } else {
//            BeginAction(ActionKind::Rush, ActionStep::Charge);
//        }
//        return;
//    }
//
//    int smashWeight = counterPunishSmashWeight_;
//    int sweepWeight = counterPunishSweepWeight_;
//    int rushWeight = counterPunishRushWeight_;
//
//    if (distance <= nearAttackDistance_) {
//        smashWeight += 10;
//        sweepWeight += 5;
//    } else {
//        rushWeight += 15;
//    }
//
//    int total = smashWeight + sweepWeight + rushWeight;
//    if (total <= 0) {
//        total = 1;
//    }
//
//    int r = std::rand() % total;
//
//    if (r < smashWeight) {
//        BeginAction(ActionKind::Smash, ActionStep::Charge);
//    } else if (r < smashWeight + sweepWeight) {
//        BeginAction(ActionKind::Sweep, ActionStep::Charge);
//    } else {
//        BeginAction(ActionKind::Rush, ActionStep::Charge);
//    }
//}
//
//void Enemy::BeginAntiGuardAction() {
//    float distance = GetDistanceToPlayer();
//
//    if (distance <= nearAttackDistance_) {
//        int rushWeight = midRushWeight_ + antiGuardRushBonus_;
//        int waveWeight = midWaveWeight_ + antiGuardWaveBonus_;
//        int shotWeight = midShotWeight_ + antiGuardShotBonus_;
//
//        int total = rushWeight + waveWeight + shotWeight;
//        if (total <= 0) {
//            total = 1;
//        }
//
//        int r = std::rand() % total;
//
//        if (r < rushWeight) {
//            BeginAction(ActionKind::Rush, ActionStep::Charge);
//        } else if (r < rushWeight + waveWeight) {
//            BeginAction(ActionKind::Wave, ActionStep::Charge);
//        } else {
//            BeginAction(ActionKind::Shot, ActionStep::Charge);
//        }
//        return;
//    }
//
//    int shotWeight = farShotWeight_ + antiGuardShotBonus_;
//    int waveWeight = farWaveWeight_ + antiGuardWaveBonus_;
//    int warpWeight = farWarpWeight_;
//
//    int total = shotWeight + waveWeight + warpWeight;
//    if (total <= 0) {
//        total = 1;
//    }
//
//    int r = std::rand() % total;
//    if (r < shotWeight) {
//        BeginAction(ActionKind::Shot, ActionStep::Charge);
//    } else if (r < shotWeight + waveWeight) {
//        BeginAction(ActionKind::Wave, ActionStep::Charge);
//    } else {
//        if (PrepareWarpContext()) {
//            BeginAction(ActionKind::Warp, ActionStep::Start);
//        } else {
//            BeginAction(ActionKind::Wave, ActionStep::Charge);
//        }
//    }
//}
//
//void Enemy::BeginChaseAction() {
//    int shotWeight = farShotWeight_;
//    int warpWeight = farWarpWeight_;
//    int waveWeight = farWaveWeight_;
//
//    if (isDistanceStagnant_) {
//        warpWeight += stagnantWarpBonus_;
//    }
//
//    if (lastActionKind_ == ActionKind::Shot) {
//        shotWeight /= 2;
//    } else if (lastActionKind_ == ActionKind::Warp) {
//        warpWeight /= 2;
//    } else if (lastActionKind_ == ActionKind::Wave) {
//        waveWeight /= 2;
//    }
//
//    if (playerGuarding_) {
//        waveWeight += 10;
//    }
//
//    int total = shotWeight + warpWeight + waveWeight;
//    if (total <= 0) {
//        total = 1;
//    }
//
//    int r = std::rand() % total;
//    if (r < shotWeight) {
//        BeginAction(ActionKind::Shot, ActionStep::Charge);
//
//    } else if (r < shotWeight + warpWeight) {
//        if (PrepareWarpContext()) {
//            BeginAction(ActionKind::Warp, ActionStep::Start);
//        } else {
//            BeginAction(ActionKind::Wave, ActionStep::Charge);
//        }
//
//    } else {
//        BeginAction(ActionKind::Wave, ActionStep::Charge);
//    }
//}
//
//void Enemy::BeginResetAction() {
//    if (forceEscapeWarpNext_) {
//        ResetWarpContext();
//        warp_.type = WarpType::Escape;
//
//        if (DecideWarpTargetFarFromPlayer(warp_.targetPos)) {
//            warp_.hasValidTarget = true;
//            warp_.followupKind = ActionKind::Shot;
//            warp_.followupStep = ActionStep::Charge;
//            BeginAction(ActionKind::Warp, ActionStep::Start);
//            return;
//        }
//    }
//
//    if (PrepareWarpContext()) {
//        BeginAction(ActionKind::Warp, ActionStep::Start);
//    } else {
//        BeginAction(ActionKind::Guard, ActionStep::Move);
//    }
//}
//
//// ============================================================
//// 振り下ろし攻撃更新
//// ============================================================
//void Enemy::UpdateSmashCharge(float deltaTime) {
//    float currentChargeTime = GetCurrentSmashChargeTime();
//
//    float trackingEnd = smashTiming_.trackingEndTime;
//    if (trackingEnd < 0.0f) {
//        trackingEnd = 0.0f;
//    }
//    if (trackingEnd > currentChargeTime) {
//        trackingEnd = currentChargeTime;
//    }
//
//    if (!tellActive_ && stateTimer_ <= 0.0001f) {
//        EnterTell(ActionKind::Smash);
//    }
//
//    if (tellActive_) {
//        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * 0.55f);
//
//        if (!IsTellFinished()) {
//            return;
//        }
//
//        tellActive_ = false;
//        stateTimer_ = 0.0f;
//    }
//
//    if (stateTimer_ < trackingEnd) {
//        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_);
//    } else if (!hasTrackingLocked_) {
//        LockCurrentFacing();
//        hasTrackingLocked_ = true;
//    }
//
//    if (stateTimer_ >= currentChargeTime) {
//        if (!hasTrackingLocked_) {
//            LockCurrentFacing();
//            hasTrackingLocked_ = true;
//        }
//
//        if (ShouldEnterSmashHold()) {
//            EnterHold(RandomRange(smashHoldTimeMin_, smashHoldTimeMax_));
//
//            if (ShouldDoFakeCommit(ActionKind::Smash)) {
//                EnterFakeCommit(ActionKind::Smash);
//            }
//
//            ChangeActionStep(ActionStep::Hold);
//            return;
//        }
//
//        ChangeActionStep(ActionStep::Active);
//    }
//}
//
//void Enemy::UpdateSmashHold(float deltaTime) {
//    if (fakeCommitActive_) {
//        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * 0.35f);
//
//        if (!IsFakeCommitFinished()) {
//            return;
//        }
//
//        EnterFreezeHold(ActionKind::Smash);
//        stateTimer_ = 0.0f;
//        return;
//    }
//
//    if (freezeHoldActive_) {
//        if (!holdBranchDecided_ && stateTimer_ >= holdBranchDecisionTime_) {
//            DecideHoldBranch(ActionKind::Smash);
//        }
//
//        if (!IsFreezeHoldFinished()) {
//            return;
//        }
//
//        freezeHoldActive_ = false;
//        stateTimer_ = 0.0f;
//    }
//
//    if (!holdBranchDecided_ && stateTimer_ >= holdBranchDecisionTime_) {
//        DecideHoldBranch(ActionKind::Smash);
//    }
//
//    if (ShouldSnapReleaseFromRead()) {
//        if (TryExecuteHoldBranch(ActionKind::Smash)) {
//            return;
//        }
//
//        ChangeActionStep(ActionStep::Active);
//        return;
//    }
//
//    if (stateTimer_ >= currentHoldDuration_) {
//        if (TryExecuteHoldBranch(ActionKind::Smash)) {
//            return;
//        }
//
//        ChangeActionStep(ActionStep::Active);
//    }
//}
//
//void Enemy::UpdateSmashAttack(float deltaTime) {
//    (void)deltaTime;
//
//    isAttackActive_ = IsCurrentAttackInActiveWindow();
//
//    if (IsCurrentAttackInRecoveryWindow()) {
//        ChangeActionStep(ActionStep::Recovery);
//    }
//}
//
//void Enemy::UpdateSmashRecovery(float deltaTime) {
//    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_);
//
//    const AttackTimingParam *timing = GetCurrentAttackTiming();
//    if (!timing) {
//        EndAttack();
//        return;
//    }
//
//    float recoveryDuration = timing->totalTime - timing->recoveryStartTime;
//    if (recoveryDuration < 0.0f) {
//        recoveryDuration = 0.0f;
//    }
//
//    if (stateTimer_ >= recoveryDuration) {
//        FinishCurrentAction();
//    }
//}
//
//// ============================================================
//// 薙ぎ払い攻撃更新
//// ============================================================
//void Enemy::UpdateSweepCharge(float deltaTime) {
//    float currentChargeTime = GetCurrentSweepChargeTime();
//
//    float trackingEnd = sweepTiming_.trackingEndTime;
//    if (trackingEnd < 0.0f) {
//        trackingEnd = 0.0f;
//    }
//    if (trackingEnd > currentChargeTime) {
//        trackingEnd = currentChargeTime;
//    }
//
//    if (!tellActive_ && stateTimer_ <= 0.0001f) {
//        EnterTell(ActionKind::Sweep);
//    }
//
//    if (tellActive_) {
//        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * 0.50f);
//
//        if (!IsTellFinished()) {
//            return;
//        }
//
//        tellActive_ = false;
//        stateTimer_ = 0.0f;
//    }
//
//    if (stateTimer_ < trackingEnd) {
//        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_);
//    } else if (!hasTrackingLocked_) {
//        LockCurrentFacing();
//        hasTrackingLocked_ = true;
//    }
//
//    if (stateTimer_ >= currentChargeTime) {
//        if (!hasTrackingLocked_) {
//            LockCurrentFacing();
//            hasTrackingLocked_ = true;
//        }
//
//        if (ShouldEnterSweepHold()) {
//            EnterHold(RandomRange(sweepHoldTimeMin_, sweepHoldTimeMax_));
//
//            if (ShouldDoFakeCommit(ActionKind::Sweep)) {
//                EnterFakeCommit(ActionKind::Sweep);
//            }
//
//            ChangeActionStep(ActionStep::Hold);
//            return;
//        }
//
//        ChangeActionStep(ActionStep::Active);
//    }
//}
//
//void Enemy::UpdateSweepHold(float deltaTime) {
//    if (fakeCommitActive_) {
//        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_ * 0.30f);
//
//        if (!IsFakeCommitFinished()) {
//            return;
//        }
//
//        EnterFreezeHold(ActionKind::Sweep);
//        stateTimer_ = 0.0f;
//        return;
//    }
//
//    if (freezeHoldActive_) {
//        if (!holdBranchDecided_ && stateTimer_ >= holdBranchDecisionTime_) {
//            DecideHoldBranch(ActionKind::Sweep);
//        }
//
//        if (!IsFreezeHoldFinished()) {
//            return;
//        }
//
//        freezeHoldActive_ = false;
//        stateTimer_ = 0.0f;
//    }
//
//    if (!holdBranchDecided_ && stateTimer_ >= holdBranchDecisionTime_) {
//        DecideHoldBranch(ActionKind::Sweep);
//    }
//
//    if (ShouldSnapReleaseFromRead()) {
//        if (TryExecuteHoldBranch(ActionKind::Sweep)) {
//            return;
//        }
//
//        ChangeActionStep(ActionStep::Active);
//        return;
//    }
//
//    if (stateTimer_ >= currentHoldDuration_) {
//        if (TryExecuteHoldBranch(ActionKind::Sweep)) {
//            return;
//        }
//
//        ChangeActionStep(ActionStep::Active);
//    }
//}
//
//void Enemy::UpdateSweepAttack(float deltaTime) {
//    (void)deltaTime;
//
//    isAttackActive_ = IsCurrentAttackInActiveWindow();
//
//    if (IsCurrentAttackInRecoveryWindow()) {
//        ChangeActionStep(ActionStep::Recovery);
//    }
//}
//
//void Enemy::UpdateSweepRecovery(float deltaTime) {
//    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_);
//
//    if (TryBeginDoubleSweepSecondStage()) {
//        return;
//    }
//
//    const AttackTimingParam *timing = GetCurrentAttackTiming();
//    if (!timing) {
//        EndAttack();
//        return;
//    }
//
//    float recoveryDuration = timing->totalTime - timing->recoveryStartTime;
//    if (recoveryDuration < 0.0f) {
//        recoveryDuration = 0.0f;
//    }
//
//    if (stateTimer_ >= recoveryDuration) {
//        FinishCurrentAction();
//    }
//}
//
//bool Enemy::TryBeginDoubleSweepSecondStage() {
//    if (action_.id != ActionId::DoubleSweep) {
//        return false;
//    }
//
//    if (isDoubleSweepSecondStage_) {
//        return false;
//    }
//
//    if (stateTimer_ < doubleSweepSecondDelay_) {
//        return false;
//    }
//
//    isDoubleSweepSecondStage_ = true;
//    hasTrackingLocked_ = false;
//    holdConfigured_ = false;
//    currentHoldDuration_ = 0.0f;
//    isAttackActive_ = false;
//    stateTimer_ = 0.0f;
//    action_.step = ActionStep::Charge;
//    return true;
//}
//
//// ============================================================
//// Rush更新
//// ============================================================
//void Enemy::UpdateRushCharge(float deltaTime) {
//    float trackingEnd = rushTiming_.trackingEndTime;
//    if (trackingEnd < 0.0f) {
//        trackingEnd = 0.0f;
//    }
//    if (trackingEnd > rushChargeTime_) {
//        trackingEnd = rushChargeTime_;
//    }
//
//    if (stateTimer_ < trackingEnd) {
//        UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_);
//    } else if (!hasTrackingLocked_) {
//        LockCurrentFacing();
//        hasTrackingLocked_ = true;
//    }
//
//    if (stateTimer_ >= rushChargeTime_) {
//        if (!hasTrackingLocked_) {
//            LockCurrentFacing();
//            hasTrackingLocked_ = true;
//        }
//
//        rushCurveDir_ = (std::rand() % 2 == 0) ? -1.0f : 1.0f;
//        float curveOffsetRad =
//            rushStartCurveAngleDeg_ * 3.14159265f / 180.0f * rushCurveDir_;
//        rushCurrentYaw_ = lockedAttackYaw_ + curveOffsetRad;
//
//        ChangeActionStep(ActionStep::Active);
//    }
//}
//
//void Enemy::UpdateRushAttack(float deltaTime) {
//    isAttackActive_ = IsCurrentAttackInActiveWindow();
//
//    if (stateTimer_ <= rushMoveDuration_) {
//        float dx = playerPos_.x - tf_.position.x;
//        float dz = playerPos_.z - tf_.position.z;
//        float targetYaw = std::atan2f(dx, dz);
//
//        float diff = NormalizeAngle(targetYaw - rushCurrentYaw_);
//        float maxTurn = rushTurnSpeed_ * deltaTime;
//
//        if (diff > maxTurn) {
//            diff = maxTurn;
//        } else if (diff < -maxTurn) {
//            diff = -maxTurn;
//        }
//
//        rushCurrentYaw_ = NormalizeAngle(rushCurrentYaw_ + diff);
//
//        float forwardX = std::sinf(rushCurrentYaw_);
//        float forwardZ = std::cosf(rushCurrentYaw_);
//
//        tf_.position.x += forwardX * rushSpeed_ * deltaTime;
//        tf_.position.z += forwardZ * rushSpeed_ * deltaTime;
//
//        facingYaw_ = rushCurrentYaw_;
//    }
//
//    if (IsCurrentAttackInRecoveryWindow()) {
//        ChangeActionStep(ActionStep::Recovery);
//    }
//}
//
//void Enemy::UpdateRushRecovery(float deltaTime) {
//    UpdateFacingToPlayerWithSpeed(deltaTime, recoveryTurnSpeed_);
//
//    const AttackTimingParam *timing = GetCurrentAttackTiming();
//    if (!timing) {
//        EndAttack();
//        return;
//    }
//
//    float recoveryDuration = timing->totalTime - timing->recoveryStartTime;
//    if (recoveryDuration < 0.0f) {
//        recoveryDuration = 0.0f;
//    }
//
//    if (stateTimer_ >= recoveryDuration) {
//        FinishCurrentAction();
//    }
//}
//
//// ============================================================
//// 向き更新処理
//// ============================================================
//void Enemy::UpdateFacingToPlayer() {
//    float dx = playerPos_.x - tf_.position.x;
//    float dz = playerPos_.z - tf_.position.z;
//
//    facingYaw_ = std::atan2f(dx, dz);
//}
//
//void Enemy::LockCurrentFacing() { lockedAttackYaw_ = facingYaw_; }
//
//float Enemy::NormalizeAngle(float angle) const {
//    while (angle > 3.14159265f) {
//        angle -= 6.28318530f;
//    }
//    while (angle < -3.14159265f) {
//        angle += 6.28318530f;
//    }
//    return angle;
//}
//
//void Enemy::UpdateFacingToPlayerWithSpeed(float deltaTime, float turnSpeed) {
//    float dx = playerPos_.x - tf_.position.x;
//    float dz = playerPos_.z - tf_.position.z;
//
//    float targetYaw = std::atan2f(dx, dz);
//    float diff = NormalizeAngle(targetYaw - facingYaw_);
//
//    float maxStep = turnSpeed * deltaTime;
//
//    if (diff > maxStep) {
//        diff = maxStep;
//    } else if (diff < -maxStep) {
//        diff = -maxStep;
//    }
//
//    facingYaw_ = NormalizeAngle(facingYaw_ + diff);
//}
//
//// ============================================================
//// 弾攻撃更新
//// ============================================================
//void Enemy::UpdateShotCharge(float deltaTime) {
//    UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_);
//
//    if (stateTimer_ >= shotChargeTime_) {
//        ChangeActionStep(ActionStep::Active);
//
//        shotsRemaining_ =
//            shotMinCount_ + (std::rand() % (shotMaxCount_ - shotMinCount_ + 1));
//
//        shotIntervalTimer_ = 0.0f;
//    }
//}
//
//void Enemy::UpdateShotFire(float deltaTime) {
//    shotIntervalTimer_ += deltaTime;
//
//    if (shotsRemaining_ > 0 && shotIntervalTimer_ >= shotInterval_) {
//        SpawnBullet();
//        shotsRemaining_--;
//        shotIntervalTimer_ = 0.0f;
//    }
//
//    if (shotsRemaining_ <= 0) {
//        ChangeActionStep(ActionStep::Recovery);
//    }
//}
//
//void Enemy::UpdateShotRecovery(float deltaTime) {
//    (void)deltaTime;
//
//    if (stateTimer_ >= shotRecoveryTime_) {
//        FinishCurrentAction();
//    }
//}
//
//// ============================================================
//// 弾生成・弾更新
//// ============================================================
//void Enemy::SpawnBullet() {
//    EnemyBullet bullet{};
//
//    float dirX = playerPos_.x - rightHandTf_.position.x;
//    float dirY = playerPos_.y - rightHandTf_.position.y;
//    float dirZ = playerPos_.z - rightHandTf_.position.z;
//
//    float len = std::sqrtf(dirX * dirX + dirY * dirY + dirZ * dirZ);
//    if (len <= 0.0001f) {
//        len = 1.0f;
//    }
//
//    dirX /= len;
//    dirY /= len;
//    dirZ /= len;
//
//    bullet.position = rightHandTf_.position;
//    bullet.position.y += bulletSpawnHeightOffset_;
//
//    bullet.velocity = {dirX * bulletSpeed_, dirY * bulletSpeed_,
//                       dirZ * bulletSpeed_};
//    bullet.lifeTime = bulletLifeTime_;
//    bullet.isAlive = true;
//
//    bullets_.push_back(bullet);
//}
//
//void Enemy::UpdateBullets(float deltaTime) {
//    for (auto &bullet : bullets_) {
//        if (!bullet.isAlive) {
//            continue;
//        }
//
//        bullet.position.x += bullet.velocity.x * deltaTime;
//        bullet.position.y += bullet.velocity.y * deltaTime;
//        bullet.position.z += bullet.velocity.z * deltaTime;
//
//        bullet.lifeTime -= deltaTime;
//        if (bullet.lifeTime <= 0.0f) {
//            bullet.isAlive = false;
//        }
//    }
//}
//
//// ============================================================
//// ワープ処理
//// ============================================================
//bool Enemy::DecideWarpTargetNearPlayer(DirectX::XMFLOAT3 &outTarget) const {
//    float angle = (std::rand() % 360) * 3.14159265f / 180.0f;
//
//    float t = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
//    float radius =
//        warpNearRadiusMin_ + (warpNearRadiusMax_ - warpNearRadiusMin_) * t;
//
//    outTarget = playerPos_;
//    outTarget.x += std::cosf(angle) * radius;
//    outTarget.z += std::sinf(angle) * radius;
//    outTarget.y = tf_.position.y;
//
//    return true;
//}
//
//bool Enemy::DecideWarpTargetFarFromPlayer(DirectX::XMFLOAT3 &outTarget) const {
//    float angle = (std::rand() % 360) * 3.14159265f / 180.0f;
//
//    float t = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
//    float radius =
//        warpFarRadiusMin_ + (warpFarRadiusMax_ - warpFarRadiusMin_) * t;
//
//    outTarget = playerPos_;
//    outTarget.x += std::cosf(angle) * radius;
//    outTarget.z += std::sinf(angle) * radius;
//    outTarget.y = tf_.position.y;
//
//    return true;
//}
//
//bool Enemy::PrepareWarpContext() {
//    ResetWarpContext();
//
//    int approachWeight = warpApproachWeight_;
//    int escapeWeight = 0;
//
//    if (isDistanceStagnant_) {
//        approachWeight += stagnantWarpBonus_;
//    }
//
//    if (farDistanceTimer_ >= farDistanceWarpTimeThreshold_) {
//        approachWeight += farDistanceWarpBonus_;
//    }
//
//    bool canUseEscapeWarp =
//        (closePressureTimer_ >= closePressureTimeThreshold_) &&
//        (warpEscapeCooldownTimer_ <= 0.0f);
//
//    if (canUseEscapeWarp) {
//        escapeWeight = warpEscapeWeight_;
//    }
//
//    int warpTypeTotal = approachWeight + escapeWeight;
//    if (warpTypeTotal <= 0) {
//        warp_.type = WarpType::Approach;
//    } else {
//        int wr = std::rand() % warpTypeTotal;
//        warp_.type =
//            (wr < approachWeight) ? WarpType::Approach : WarpType::Escape;
//    }
//
//    bool ok = false;
//    if (warp_.type == WarpType::Escape) {
//        ok = DecideWarpTargetFarFromPlayer(warp_.targetPos);
//    } else {
//        warp_.type = WarpType::Approach;
//        ok = DecideWarpTargetNearPlayer(warp_.targetPos);
//    }
//
//    if (!ok) {
//        ResetWarpContext();
//        return false;
//    }
//
//    warp_.hasValidTarget = true;
//
//    if (chain_.active && (chain_.starter == ChainStarter::SweepWarpSmash ||
//                          chain_.starter == ChainStarter::WaveWarpSmash)) {
//        OverrideWarpFollowupByChain();
//    } else {
//        DecideWarpFollowupFromContext();
//        SetupChainFromWarpContext();
//    }
//
//    return true;
//}
//
//void Enemy::DecideWarpFollowupFromContext() {
//    if (warp_.type == WarpType::Approach) {
//        int total = nearSmashWeight_ + nearSweepWeight_;
//        if (total <= 0) {
//            warp_.followupKind = ActionKind::Smash;
//            warp_.followupStep = ActionStep::Charge;
//            return;
//        }
//
//        int r = std::rand() % total;
//        if (r < nearSmashWeight_) {
//            warp_.followupKind = ActionKind::Smash;
//            warp_.followupStep = ActionStep::Charge;
//        } else {
//            warp_.followupKind = ActionKind::Sweep;
//            warp_.followupStep = ActionStep::Charge;
//        }
//    } else if (warp_.type == WarpType::Escape) {
//        int total = farShotWeight_ + farWaveWeight_;
//        if (total <= 0) {
//            warp_.followupKind = ActionKind::Shot;
//            warp_.followupStep = ActionStep::Charge;
//            return;
//        }
//
//        int r = std::rand() % total;
//        if (r < farShotWeight_) {
//            warp_.followupKind = ActionKind::Shot;
//            warp_.followupStep = ActionStep::Charge;
//        } else {
//            warp_.followupKind = ActionKind::Wave;
//            warp_.followupStep = ActionStep::Charge;
//        }
//    }
//}
//
//void Enemy::SetupChainFromWarpContext() {
//    ResetChainContext();
//
//    if (warp_.type == WarpType::Approach) {
//        chain_.active = true;
//        chain_.starter = ChainStarter::WarpApproach;
//        chain_.stepCount = 0;
//        chain_.maxSteps = warpApproachChainMaxSteps_;
//    } else if (warp_.type == WarpType::Escape) {
//        chain_.active = true;
//        chain_.starter = ChainStarter::WarpEscape;
//        chain_.stepCount = 0;
//        chain_.maxSteps = warpEscapeChainMaxSteps_;
//    }
//}
//
//void Enemy::SetupSweepWarpSmashChain() {
//    ResetChainContext();
//    chain_.active = true;
//    chain_.starter = ChainStarter::SweepWarpSmash;
//    chain_.stepCount = 0;
//    chain_.maxSteps = 2;
//}
//
//void Enemy::SetupWaveWarpSmashChain() {
//    ResetChainContext();
//    chain_.active = true;
//    chain_.starter = ChainStarter::WaveWarpSmash;
//    chain_.stepCount = 0;
//    chain_.maxSteps = 2;
//}
//
//void Enemy::OverrideWarpFollowupByChain() {
//    switch (chain_.starter) {
//    case ChainStarter::SweepWarpSmash:
//    case ChainStarter::WaveWarpSmash:
//        warp_.followupKind = ActionKind::Smash;
//        warp_.followupStep = ActionStep::Charge;
//        break;
//    default:
//        break;
//    }
//}
//
//void Enemy::ResetPostActionState() {
//    postActionOption_ = PostActionOption::None;
//    backWarpFollowup_ = BackWarpFollowup::None;
//}
//
//void Enemy::BeginBackWarpPostAction() {
//    ResetWarpContext();
//
//    warp_.type = WarpType::Escape;
//
//    if (!DecideWarpTargetFarFromPlayer(warp_.targetPos)) {
//        ResetPostActionState();
//        return;
//    }
//
//    warp_.hasValidTarget = true;
//
//    if (backWarpFollowup_ == BackWarpFollowup::Shot) {
//        warp_.followupKind = ActionKind::Shot;
//        warp_.followupStep = ActionStep::Charge;
//    } else if (backWarpFollowup_ == BackWarpFollowup::Wave) {
//        warp_.followupKind = ActionKind::Wave;
//        warp_.followupStep = ActionStep::Charge;
//    } else if (backWarpFollowup_ == BackWarpFollowup::Rush) {
//        warp_.followupKind = ActionKind::Rush;
//        warp_.followupStep = ActionStep::Charge;
//    } else {
//        warp_.followupKind = ActionKind::Shot;
//        warp_.followupStep = ActionStep::Charge;
//    }
//
//    BeginAction(ActionKind::Warp, ActionStep::Start);
//}
//
//void Enemy::BeginWarpFollowup() {
//    ActionKind nextKind = warp_.followupKind;
//    ActionStep nextStep = warp_.followupStep;
//
//    if (nextKind == ActionKind::None || nextStep == ActionStep::None) {
//        ResetPostActionState();
//        EndAttack();
//        return;
//    }
//
//    if (chain_.active) {
//        if (chain_.stepCount < 1) {
//            chain_.stepCount = 1;
//        }
//    }
//
//    ResetPostActionState();
//    BeginAction(nextKind, nextStep);
//}
//
//void Enemy::ResetWarpContext() { warp_ = WarpContext{}; }
//
//void Enemy::ResetChainContext() { chain_ = ChainContext{}; }
//
//bool Enemy::DecideNextChainAction(ActionKind finishedKind, ActionKind &outKind,
//                                  ActionStep &outStep) const {
//    outKind = ActionKind::None;
//    outStep = ActionStep::None;
//
//    if (!chain_.active) {
//        return false;
//    }
//
//    float distance = GetDistanceToPlayer();
//
//    switch (chain_.starter) {
//    case ChainStarter::WarpApproach:
//        if (distance > approachChainContinueDistance_) {
//            return false;
//        }
//
//        if (finishedKind == ActionKind::Smash) {
//            outKind = ActionKind::Sweep;
//            outStep = ActionStep::Charge;
//            return true;
//        }
//
//        return false;
//
//    case ChainStarter::WarpEscape:
//        if (finishedKind == ActionKind::Shot) {
//            outKind = ActionKind::Rush;
//            outStep = ActionStep::Charge;
//            return true;
//        }
//
//        if (finishedKind == ActionKind::Wave) {
//            outKind = ActionKind::Rush;
//            outStep = ActionStep::Charge;
//            return true;
//        }
//
//        return false;
//
//    case ChainStarter::SweepWarpSmash:
//        return false;
//
//    case ChainStarter::WaveWarpSmash:
//        return false;
//
//    default:
//        return false;
//    }
//}
//
//bool Enemy::TryStartPostActionWarpChain(ActionKind finishedKind) {
//    float distance = GetDistanceToPlayer();
//
//    if (finishedKind == ActionKind::Sweep) {
//        if (distance <= sweepWarpSmashMaxDistance_) {
//            float r =
//                static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
//            if (r < sweepWarpSmashChance_) {
//                SetupSweepWarpSmashChain();
//
//                warp_.type = WarpType::Approach;
//
//                if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
//                    ResetChainContext();
//                    ResetWarpContext();
//                    return false;
//                }
//
//                warp_.hasValidTarget = true;
//                OverrideWarpFollowupByChain();
//                BeginAction(ActionKind::Warp, ActionStep::Start);
//                return true;
//            }
//        }
//    }
//
//    if (finishedKind == ActionKind::Wave) {
//        if (distance >= waveWarpSmashMinDistance_) {
//            float r =
//                static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
//            if (r < waveWarpSmashChance_) {
//                SetupWaveWarpSmashChain();
//
//                warp_.type = WarpType::Approach;
//
//                if (!DecideWarpTargetNearPlayer(warp_.targetPos)) {
//                    ResetChainContext();
//                    ResetWarpContext();
//                    return false;
//                }
//
//                warp_.hasValidTarget = true;
//                OverrideWarpFollowupByChain();
//                BeginAction(ActionKind::Warp, ActionStep::Start);
//                return true;
//            }
//        }
//    }
//
//    return false;
//}
//
//bool Enemy::TryStartBackWarpPostAction(ActionKind finishedKind) {
//    float chance = 0.0f;
//
//    switch (finishedKind) {
//    case ActionKind::Smash:
//        chance = backWarpAfterSmashChance_;
//        break;
//    case ActionKind::Sweep:
//        chance = backWarpAfterSweepChance_;
//        break;
//    case ActionKind::Wave:
//        chance = backWarpAfterWaveChance_;
//        break;
//    default:
//        return false;
//    }
//
//    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
//    if (r >= chance) {
//        return false;
//    }
//
//    float followupRoll =
//        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
//
//    if (followupRoll < backWarpShotChance_) {
//        backWarpFollowup_ = BackWarpFollowup::Shot;
//    } else {
//        backWarpFollowup_ = BackWarpFollowup::Wave;
//    }
//
//    postActionOption_ = PostActionOption::BackWarp;
//    BeginBackWarpPostAction();
//    return true;
//}
//
//bool Enemy::TryContinueChain() {
//    ActionKind finishedKind = action_.kind;
//
//    if (chain_.active) {
//        if (chain_.stepCount >= chain_.maxSteps) {
//            ResetChainContext();
//        } else {
//            ActionKind nextKind = ActionKind::None;
//            ActionStep nextStep = ActionStep::None;
//
//            if (DecideNextChainAction(finishedKind, nextKind, nextStep)) {
//                chain_.stepCount++;
//                BeginAction(nextKind, nextStep);
//                return true;
//            }
//
//            ResetChainContext();
//        }
//    }
//
//    if (TryStartPostActionWarpChain(finishedKind)) {
//        return true;
//    }
//
//    return false;
//}
//
//void Enemy::FinishCurrentAction() {
//    ActionKind finishedKind = action_.kind;
//
//    if (TryContinueChain()) {
//        return;
//    }
//
//    if (TryBranchFromRecovery(finishedKind)) {
//        if (recoveryBranchType_ == RecoveryBranchType::Recommit ||
//            recoveryBranchType_ == RecoveryBranchType::DelayedSecond) {
//            ActionKind nextKind = recoveryFollowupKind_;
//            ActionStep nextStep = recoveryFollowupStep_;
//
//            EndAttack();
//
//            if (nextKind == ActionKind::Smash ||
//                nextKind == ActionKind::Sweep || nextKind == ActionKind::Rush) {
//                tactic_ = TacticState::Pressure;
//            }
//
//            recoveryFollowupKind_ = nextKind;
//            recoveryFollowupStep_ = nextStep;
//            return;
//        }
//
//        return;
//    }
//
//    if (TryStartBackWarpPostAction(finishedKind)) {
//        return;
//    }
//
//    EndAttack();
//}
//
//void Enemy::UpdateWarpStart(float deltaTime) {
//    (void)deltaTime;
//
//    isVisible_ = false;
//    warp_.collisionDisabled = true;
//
//    if (stateTimer_ >= warpStartTime_) {
//        ChangeActionStep(ActionStep::Move);
//    }
//}
//
//void Enemy::UpdateWarpMove(float deltaTime) {
//    (void)deltaTime;
//
//    if (!warp_.hasValidTarget) {
//        EndAttack();
//        return;
//    }
//
//    tf_.position = warp_.targetPos;
//
//    UpdateFacingToPlayer();
//    LockCurrentFacing();
//
//    ChangeActionStep(ActionStep::End);
//}
//
//void Enemy::UpdateWarpEnd(float deltaTime) {
//    (void)deltaTime;
//
//    isVisible_ = true;
//    warp_.collisionDisabled = false;
//
//    if (stateTimer_ >= warpEndTime_) {
//        BeginWarpFollowup();
//    }
//}
//
//// ============================================================
//// 波攻撃更新
//// ============================================================
//void Enemy::UpdateWaveCharge(float deltaTime) {
//    UpdateFacingToPlayerWithSpeed(deltaTime, chargeTurnSpeed_);
//
//    if (stateTimer_ >= waveChargeTime_) {
//        LockCurrentFacing();
//        ChangeActionStep(ActionStep::Active);
//    }
//}
//
//void Enemy::UpdateWaveFire(float deltaTime) {
//    (void)deltaTime;
//
//    SpawnWave();
//    ChangeActionStep(ActionStep::Recovery);
//}
//
//void Enemy::UpdateWaveRecovery(float deltaTime) {
//    (void)deltaTime;
//
//    if (stateTimer_ >= waveRecoveryTime_) {
//        FinishCurrentAction();
//    }
//}
//
//// ============================================================
//// 波生成・波更新
//// ============================================================
//void Enemy::SpawnWave() {
//    EnemyWave wave{};
//
//    float usedYaw = lockedAttackYaw_;
//    float forwardX = std::sinf(usedYaw);
//    float forwardZ = std::cosf(usedYaw);
//
//    wave.position = bodyTf_.position;
//    wave.position.y = tf_.position.y + waveSpawnHeightOffset_;
//    wave.position.x += forwardX * waveSpawnForwardOffset_;
//    wave.position.z += forwardZ * waveSpawnForwardOffset_;
//
//    wave.direction = {forwardX, 0.0f, forwardZ};
//    wave.speed = waveSpeed_;
//    wave.traveledDistance = 0.0f;
//    wave.maxDistance = waveMaxDistance_;
//    wave.isAlive = true;
//
//    waves_.push_back(wave);
//}
//
//void Enemy::UpdateWaves(float deltaTime) {
//    for (auto &wave : waves_) {
//        if (!wave.isAlive) {
//            continue;
//        }
//
//        float moveX = wave.direction.x * wave.speed * deltaTime;
//        float moveZ = wave.direction.z * wave.speed * deltaTime;
//
//        wave.position.x += moveX;
//        wave.position.z += moveZ;
//
//        float moved = std::sqrtf(moveX * moveX + moveZ * moveZ);
//        wave.traveledDistance += moved;
//
//        if (wave.traveledDistance >= wave.maxDistance) {
//            wave.isAlive = false;
//        }
//    }
//}
//
//// ============================================================
//// ガード処理
//// ============================================================
//void Enemy::DecideGuardTarget() {
//    int r = std::rand() % 3;
//
//    if (r == 0) {
//        guardTarget_ = GuardTarget::Face;
//    } else if (r == 1) {
//        guardTarget_ = GuardTarget::BodyLeft;
//    } else {
//        guardTarget_ = GuardTarget::BodyRight;
//    }
//}
//
//void Enemy::UpdateGuardMove(float deltaTime) {
//    (void)deltaTime;
//
//    if (stateTimer_ >= guardMoveTime_) {
//        action_.step = ActionStep::Hold;
//        stateTimer_ = 0.0f;
//    }
//}
//
//void Enemy::UpdateGuardHold(float deltaTime) {
//    (void)deltaTime;
//
//    isGuardActive_ = true;
//
//    if (stateTimer_ >= guardHoldTime_) {
//        action_.step = ActionStep::Recovery;
//        stateTimer_ = 0.0f;
//    }
//}
//
//void Enemy::UpdateGuardRecovery(float deltaTime) {
//    (void)deltaTime;
//
//    if (stateTimer_ >= guardRecoveryTime_) {
//        guardTarget_ = GuardTarget::None;
//        isGuardActive_ = false;
//        EndAttack();
//    }
//}
//
//// ============================================================
//// アクションタイムの管理
//// ============================================================
//float Enemy::GetCurrentActionTime() const { return stateTimer_; }
//
//float Enemy::GetCurrentSmashChargeTime() const {
//    float result = smashChargeTime_;
//
//    if (action_.id == ActionId::DelaySmash) {
//        result += delaySmashExtraChargeTime_;
//    }
//
//    result += GetAdaptiveChargeOffset(ActionKind::Smash);
//
//    if (result < 0.05f) {
//        result = 0.05f;
//    }
//    return result;
//}
//
//float Enemy::GetCurrentSweepChargeTime() const {
//    float result = sweepChargeTime_;
//
//    if (action_.id == ActionId::DoubleSweep && isDoubleSweepSecondStage_) {
//        result *= doubleSweepSecondChargeScale_;
//    }
//
//    result += GetAdaptiveChargeOffset(ActionKind::Sweep);
//
//    if (result < 0.05f) {
//        result = 0.05f;
//    }
//    return result;
//}
//
//const AttackTimingParam *Enemy::GetCurrentAttackTiming() const {
//    switch (action_.kind) {
//    case ActionKind::Smash:
//        return &smashTiming_;
//    case ActionKind::Sweep:
//        return &sweepTiming_;
//    case ActionKind::Rush:
//        return &rushTiming_;
//    default:
//        return nullptr;
//    }
//}
//
//AttackParam *Enemy::GetCurrentAttackParam() {
//    switch (action_.kind) {
//    case ActionKind::Smash:
//        return &smashParam_;
//    case ActionKind::Sweep:
//        return &sweepParam_;
//    case ActionKind::Shot:
//        return &bulletParam_;
//    case ActionKind::Wave:
//        return &waveParam_;
//    case ActionKind::Rush:
//        return &rushParam_;
//    default:
//        return nullptr;
//    }
//}
//
//const AttackParam *Enemy::GetCurrentAttackParam() const {
//    switch (action_.kind) {
//    case ActionKind::Smash:
//        return &smashParam_;
//    case ActionKind::Sweep:
//        return &sweepParam_;
//    case ActionKind::Shot:
//        return &bulletParam_;
//    case ActionKind::Wave:
//        return &waveParam_;
//    case ActionKind::Rush:
//        return &rushParam_;
//    default:
//        return nullptr;
//    }
//}
//
//bool Enemy::IsCurrentAttackInActiveWindow() const {
//    const AttackTimingParam *timing = GetCurrentAttackTiming();
//    if (!timing) {
//        return false;
//    }
//
//    float t = GetCurrentActionTime();
//    return (t >= timing->activeStartTime && t <= timing->activeEndTime);
//}
//
//bool Enemy::IsCurrentAttackInRecoveryWindow() const {
//    const AttackTimingParam *timing = GetCurrentAttackTiming();
//    if (!timing) {
//        return false;
//    }
//
//    return GetCurrentActionTime() >= timing->recoveryStartTime;
//}
//
//bool Enemy::HasReachedTrackingEnd() const {
//    const AttackTimingParam *timing = GetCurrentAttackTiming();
//    if (!timing) {
//        return false;
//    }
//    return GetCurrentActionTime() >= timing->trackingEndTime;
//}
//
//bool Enemy::HasReachedHitStart() const {
//    const AttackTimingParam *timing = GetCurrentAttackTiming();
//    if (!timing) {
//        return false;
//    }
//    return GetCurrentActionTime() >= timing->activeStartTime;
//}
//
//bool Enemy::HasReachedHitEnd() const {
//    const AttackTimingParam *timing = GetCurrentAttackTiming();
//    if (!timing) {
//        return false;
//    }
//    return GetCurrentActionTime() > timing->activeEndTime;
//}
//
//bool Enemy::HasReachedRecoveryStart() const {
//    const AttackTimingParam *timing = GetCurrentAttackTiming();
//    if (!timing) {
//        return false;
//    }
//    return GetCurrentActionTime() >= timing->recoveryStartTime;
//}
//
//// ============================================================
//// 行動開始・行動遷移
//// ============================================================
//void Enemy::BeginAction(ActionKind kind, ActionStep step) {
//    lastActionKind_ = kind;
//
//    if (kind == ActionKind::Warp) {
//        stagnantTimer_ = 0.0f;
//        isDistanceStagnant_ = false;
//
//        if (warp_.type == WarpType::Escape) {
//            warpEscapeCooldownTimer_ = warpEscapeCooldown_;
//        }
//    } else {
//        ResetWarpContext();
//    }
//
//    action_.kind = kind;
//    action_.id = MakeDefaultActionId(kind);
//
//    if (kind == ActionKind::Smash) {
//        float r =
//            static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
//
//        float useDelayChance = delaySmashChance_;
//        if (tactic_ == TacticState::CounterBait || playerObs_.isCounterStance) {
//            useDelayChance += 0.20f;
//        }
//
//        if (r < useDelayChance) {
//            action_.id = ActionId::DelaySmash;
//        }
//    }
//
//    if (kind == ActionKind::Sweep) {
//        float r =
//            static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
//
//        float useDoubleChance = doubleSweepChance_;
//        if (tactic_ == TacticState::CounterPunish || IsCounterFailObserved()) {
//            useDoubleChance += 0.20f;
//        }
//
//        if (r < useDoubleChance) {
//            action_.id = ActionId::DoubleSweep;
//        }
//    }
//
//    action_.step = step;
//
//    hasTrackingLocked_ = false;
//    holdConfigured_ = false;
//    currentHoldDuration_ = 0.0f;
//    isAttackActive_ = false;
//    stateTimer_ = 0.0f;
//    isDoubleSweepSecondStage_ = false;
//    ResetPreAttackPresentationState();
//    ResetRecoveryBranchState();
//}
//
//ActionId Enemy::MakeDefaultActionId(ActionKind kind) const {
//    switch (kind) {
//    case ActionKind::Smash:
//        return ActionId::Smash;
//
//    case ActionKind::Sweep:
//        return ActionId::Sweep;
//
//    case ActionKind::Shot:
//        return ActionId::Shot;
//
//    case ActionKind::Wave:
//        return ActionId::Wave;
//
//    case ActionKind::Rush:
//        return ActionId::Rush;
//
//    case ActionKind::Warp:
//        return (warp_.type == WarpType::Escape) ? ActionId::WarpEscape
//                                                : ActionId::WarpApproach;
//
//    case ActionKind::Guard:
//        switch (guardTarget_) {
//        case GuardTarget::Face:
//            return ActionId::GuardFace;
//        case GuardTarget::BodyLeft:
//            return ActionId::GuardBodyLeft;
//        case GuardTarget::BodyRight:
//            return ActionId::GuardBodyRight;
//        default:
//            return ActionId::None;
//        }
//
//    default:
//        return ActionId::None;
//    }
//}
//
//void Enemy::ChangeActionStep(ActionStep step) {
//    action_.step = step;
//    isAttackActive_ = false;
//    stateTimer_ = 0.0f;
//
//    if (step != ActionStep::Hold) {
//        holdBranchType_ = HoldBranchType::None;
//        holdBranchDecided_ = false;
//        holdBranchDecisionTime_ = 0.0f;
//    }
//
//    if (step != ActionStep::Charge && step != ActionStep::Hold) {
//        ResetPreAttackPresentationState();
//    }
//}
//
//void Enemy::EndAttack() {
//    action_.kind = ActionKind::None;
//    action_.id = ActionId::None;
//    action_.step = ActionStep::None;
//
//    ResetWarpContext();
//    ResetChainContext();
//    ResetPostActionState();
//    isVisible_ = true;
//
//    hasTrackingLocked_ = false;
//    holdConfigured_ = false;
//    currentHoldDuration_ = 0.0f;
//    isAttackActive_ = false;
//    stateTimer_ = 0.0f;
//    isDoubleSweepSecondStage_ = false;
//    rushCurrentYaw_ = facingYaw_;
//
//    if (postCounterRhythmTimer_ <= 0.0f) {
//        counterMemory_.consecutiveSuccess = 0;
//    }
//
//    ResetPreAttackPresentationState();
//    ResetRecoveryBranchState();
//}
//
//// ============================================================
//// 現在の攻撃が、向き固定して攻撃判定を出すタイプか
//// ============================================================
//bool Enemy::ShouldUseLockedAttackYaw() const {
//    switch (action_.kind) {
//    case ActionKind::Smash:
//    case ActionKind::Sweep:
//    case ActionKind::Rush:
//        return true;
//    default:
//        return false;
//    }
//}
//
//// ============================================================
//// Timing検証
//// ============================================================
//void Enemy::ValidateTiming(AttackTimingParam &timing, float chargeTime) {
//    if (timing.totalTime < 0.0f) {
//        timing.totalTime = 0.0f;
//    }
//
//    if (timing.trackingEndTime < 0.0f) {
//        timing.trackingEndTime = 0.0f;
//    }
//    if (timing.trackingEndTime > chargeTime) {
//        timing.trackingEndTime = chargeTime;
//    }
//
//    if (timing.activeStartTime < 0.0f) {
//        timing.activeStartTime = 0.0f;
//    }
//    if (timing.activeEndTime < timing.activeStartTime) {
//        timing.activeEndTime = timing.activeStartTime;
//    }
//    if (timing.recoveryStartTime < timing.activeEndTime) {
//        timing.recoveryStartTime = timing.activeEndTime;
//    }
//    if (timing.totalTime < timing.recoveryStartTime) {
//        timing.totalTime = timing.recoveryStartTime;
//    }
//}
//
//void Enemy::ValidateAllTimings() {
//    ValidateTiming(smashTiming_, smashChargeTime_);
//    ValidateTiming(sweepTiming_, sweepChargeTime_);
//    ValidateTiming(rushTiming_, rushChargeTime_);
//}
//
//// ============================================================
//// action ベース更新
//// ============================================================
//void Enemy::UpdateByAction(float deltaTime) {
//    if (action_.kind == ActionKind::None) {
//        UpdateIdle(deltaTime);
//        return;
//    }
//
//    switch (action_.kind) {
//    case ActionKind::Smash:
//        UpdateSmashByStep(deltaTime);
//        break;
//    case ActionKind::Sweep:
//        UpdateSweepByStep(deltaTime);
//        break;
//    case ActionKind::Shot:
//        UpdateShotByStep(deltaTime);
//        break;
//    case ActionKind::Wave:
//        UpdateWaveByStep(deltaTime);
//        break;
//    case ActionKind::Rush:
//        UpdateRushByStep(deltaTime);
//        break;
//    case ActionKind::Warp:
//        UpdateWarpByStep(deltaTime);
//        break;
//    case ActionKind::Guard:
//        UpdateGuardByStep(deltaTime);
//        break;
//    default:
//        UpdateIdle(deltaTime);
//        break;
//    }
//}
//
//void Enemy::UpdateSmashByStep(float deltaTime) {
//    switch (action_.step) {
//    case ActionStep::Charge:
//        UpdateSmashCharge(deltaTime);
//        break;
//    case ActionStep::Hold:
//        UpdateSmashHold(deltaTime);
//        break;
//    case ActionStep::Active:
//        UpdateSmashAttack(deltaTime);
//        break;
//    case ActionStep::Recovery:
//        UpdateSmashRecovery(deltaTime);
//        break;
//    default:
//        EndAttack();
//        break;
//    }
//}
//
//void Enemy::UpdateSweepByStep(float deltaTime) {
//    switch (action_.step) {
//    case ActionStep::Charge:
//        UpdateSweepCharge(deltaTime);
//        break;
//    case ActionStep::Hold:
//        UpdateSweepHold(deltaTime);
//        break;
//    case ActionStep::Active:
//        UpdateSweepAttack(deltaTime);
//        break;
//    case ActionStep::Recovery:
//        UpdateSweepRecovery(deltaTime);
//        break;
//    default:
//        EndAttack();
//        break;
//    }
//}
//
//void Enemy::UpdateShotByStep(float deltaTime) {
//    switch (action_.step) {
//    case ActionStep::Charge:
//        UpdateShotCharge(deltaTime);
//        break;
//    case ActionStep::Active:
//        UpdateShotFire(deltaTime);
//        break;
//    case ActionStep::Recovery:
//        UpdateShotRecovery(deltaTime);
//        break;
//    default:
//        EndAttack();
//        break;
//    }
//}
//
//void Enemy::UpdateWaveByStep(float deltaTime) {
//    switch (action_.step) {
//    case ActionStep::Charge:
//        UpdateWaveCharge(deltaTime);
//        break;
//    case ActionStep::Active:
//        UpdateWaveFire(deltaTime);
//        break;
//    case ActionStep::Recovery:
//        UpdateWaveRecovery(deltaTime);
//        break;
//    default:
//        EndAttack();
//        break;
//    }
//}
//
//void Enemy::UpdateRushByStep(float deltaTime) {
//    switch (action_.step) {
//    case ActionStep::Charge:
//        UpdateRushCharge(deltaTime);
//        break;
//    case ActionStep::Active:
//        UpdateRushAttack(deltaTime);
//        break;
//    case ActionStep::Recovery:
//        UpdateRushRecovery(deltaTime);
//        break;
//    default:
//        EndAttack();
//        break;
//    }
//}
//
//void Enemy::UpdateWarpByStep(float deltaTime) {
//    switch (action_.step) {
//    case ActionStep::Start:
//        UpdateWarpStart(deltaTime);
//        break;
//    case ActionStep::Move:
//        UpdateWarpMove(deltaTime);
//        break;
//    case ActionStep::End:
//        UpdateWarpEnd(deltaTime);
//        break;
//    default:
//        EndAttack();
//        break;
//    }
//}
//
//void Enemy::UpdateGuardByStep(float deltaTime) {
//    switch (action_.step) {
//    case ActionStep::Move:
//        UpdateGuardMove(deltaTime);
//        break;
//    case ActionStep::Hold:
//        UpdateGuardHold(deltaTime);
//        break;
//    case ActionStep::Recovery:
//        UpdateGuardRecovery(deltaTime);
//        break;
//    default:
//        EndAttack();
//        break;
//    }
//}
//
//// ============================================================
//// 読み合い補助
//// ============================================================
//CounterReadAxis Enemy::GetCounterReadAxis(ActionKind kind) const {
//    switch (kind) {
//    case ActionKind::Smash:
//        return CounterReadAxis::Vertical;
//    case ActionKind::Sweep:
//        return CounterReadAxis::Horizontal;
//    case ActionKind::Rush:
//        return CounterReadAxis::ThrustLike;
//    case ActionKind::Wave:
//        return CounterReadAxis::Radial;
//    case ActionKind::Shot:
//        return CounterReadAxis::Projectile;
//    default:
//        return CounterReadAxis::None;
//    }
//}
//
//bool Enemy::ShouldEnterSmashHold() const {
//    float chance = GetAdaptiveHoldChance(ActionKind::Smash);
//    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
//    return r < chance;
//}
//
//bool Enemy::ShouldEnterSweepHold() const {
//    float chance = GetAdaptiveHoldChance(ActionKind::Sweep);
//    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
//    return r < chance;
//}
//
//void Enemy::EnterHold(float duration) {
//    holdConfigured_ = true;
//    currentHoldDuration_ = duration;
//
//    holdBranchType_ = HoldBranchType::None;
//    holdBranchDecided_ = false;
//
//    holdBranchDecisionTime_ = duration * 0.55f;
//    if (holdBranchDecisionTime_ < 0.04f) {
//        holdBranchDecisionTime_ = 0.04f;
//    }
//
//    ResetPreAttackPresentationState();
//}
//
//bool Enemy::IsCounterFailObserved() const {
//    return playerObs_.justCounterFailed || playerObs_.justCounterEarly ||
//           playerObs_.justCounterLate;
//}
//
//float Enemy::RandomRange(float minValue, float maxValue) const {
//    if (maxValue < minValue) {
//        return minValue;
//    }
//
//    float t = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
//    return minValue + (maxValue - minValue) * t;
//}
//
//void Enemy::DecideHoldBranch(ActionKind kind) {
//    holdBranchType_ = HoldBranchType::Active;
//    holdBranchDecided_ = true;
//
//    float warpChance = 0.0f;
//    float guardChance = 0.0f;
//    float rushChance = 0.0f;
//
//    if (kind == ActionKind::Smash) {
//        warpChance = smashHoldBranchWarpChance_;
//        guardChance = smashHoldBranchGuardChance_;
//        rushChance = smashHoldBranchRushChance_;
//    } else if (kind == ActionKind::Sweep) {
//        warpChance = sweepHoldBranchWarpChance_;
//        guardChance = sweepHoldBranchGuardChance_;
//        rushChance = sweepHoldBranchRushChance_;
//    } else {
//        return;
//    }
//
//    if (playerObs_.isCounterStance) {
//        warpChance += 0.10f;
//        guardChance += 0.06f;
//        rushChance += 0.08f;
//    }
//
//    if (playerObs_.justCounterEarly || counterMemory_.earlyCount > 0.6f) {
//        warpChance += 0.08f;
//        guardChance += 0.05f;
//    }
//
//    if (postCounterRhythmTimer_ > 0.0f) {
//        warpChance += 0.10f;
//        rushChance += 0.06f;
//    }
//
//    float distance = GetDistanceToPlayer();
//    if (distance <= nearAttackDistance_ * 0.8f) {
//        guardChance += 0.05f;
//        rushChance += 0.05f;
//    }
//
//    float totalSpecial = warpChance + guardChance + rushChance;
//    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
//
//    if (totalSpecial <= 0.0f || r >= totalSpecial) {
//        holdBranchType_ = HoldBranchType::Active;
//        return;
//    }
//
//    float pick = static_cast<float>(std::rand()) /
//                 static_cast<float>(RAND_MAX) * totalSpecial;
//
//    if (pick < warpChance) {
//        holdBranchType_ = HoldBranchType::Warp;
//    } else if (pick < warpChance + guardChance) {
//        holdBranchType_ = HoldBranchType::Guard;
//    } else {
//        holdBranchType_ = HoldBranchType::Rush;
//    }
//}
//
//bool Enemy::TryExecuteHoldBranch(ActionKind kind) {
//    (void)kind;
//    if (!holdBranchDecided_) {
//        return false;
//    }
//
//    switch (holdBranchType_) {
//    case HoldBranchType::Warp:
//        if (PrepareWarpContext()) {
//            BeginAction(ActionKind::Warp, ActionStep::Start);
//            return true;
//        }
//        holdBranchType_ = HoldBranchType::Active;
//        return false;
//
//    case HoldBranchType::Guard:
//        DecideGuardTarget();
//        BeginAction(ActionKind::Guard, ActionStep::Move);
//        return true;
//
//    case HoldBranchType::Rush:
//        BeginAction(ActionKind::Rush, ActionStep::Charge);
//        return true;
//
//    case HoldBranchType::Active:
//    case HoldBranchType::None:
//    default:
//        return false;
//    }
//}
//
//void Enemy::EnterTell(ActionKind kind) {
//    tellActive_ = true;
//    fakeCommitActive_ = false;
//    freezeHoldActive_ = false;
//
//    if (kind == ActionKind::Smash) {
//        tellDuration_ = smashTellTime_;
//    } else if (kind == ActionKind::Sweep) {
//        tellDuration_ = sweepTellTime_;
//    } else {
//        tellDuration_ = 0.0f;
//    }
//}
//
//bool Enemy::IsTellFinished() const { return stateTimer_ >= tellDuration_; }
//
//bool Enemy::ShouldDoFakeCommit(ActionKind kind) const {
//    float chance = 0.0f;
//
//    if (kind == ActionKind::Smash) {
//        chance = smashFakeCommitChance_;
//        if (playerObs_.isCounterStance) {
//            chance += 0.18f;
//        }
//        if (counterMemory_.earlyCount > 0.6f) {
//            chance += 0.12f;
//        }
//        if (action_.id == ActionId::DelaySmash) {
//            chance += 0.15f;
//        }
//    } else if (kind == ActionKind::Sweep) {
//        chance = sweepFakeCommitChance_;
//        if (playerObs_.isCounterStance) {
//            chance += 0.12f;
//        }
//        if (counterMemory_.earlyCount > 0.6f) {
//            chance += 0.08f;
//        }
//    }
//
//    if (postCounterRhythmTimer_ > 0.0f) {
//        chance += 0.10f;
//    }
//
//    if (chance < 0.0f) {
//        chance = 0.0f;
//    }
//    if (chance > 0.95f) {
//        chance = 0.95f;
//    }
//
//    float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
//    return r < chance;
//}
//
//void Enemy::EnterFakeCommit(ActionKind kind) {
//    tellActive_ = false;
//    fakeCommitActive_ = true;
//    freezeHoldActive_ = false;
//
//    if (kind == ActionKind::Smash) {
//        fakeCommitDuration_ = smashFakeCommitTime_;
//    } else if (kind == ActionKind::Sweep) {
//        fakeCommitDuration_ = sweepFakeCommitTime_;
//    } else {
//        fakeCommitDuration_ = 0.0f;
//    }
//}
//
//bool Enemy::IsFakeCommitFinished() const {
//    return stateTimer_ >= fakeCommitDuration_;
//}
//
//void Enemy::EnterFreezeHold(ActionKind kind) {
//    tellActive_ = false;
//    fakeCommitActive_ = false;
//    freezeHoldActive_ = true;
//
//    if (kind == ActionKind::Smash) {
//        freezeHoldDuration_ =
//            RandomRange(smashFreezeHoldTimeMin_, smashFreezeHoldTimeMax_);
//    } else if (kind == ActionKind::Sweep) {
//        freezeHoldDuration_ =
//            RandomRange(sweepFreezeHoldTimeMin_, sweepFreezeHoldTimeMax_);
//    } else {
//        freezeHoldDuration_ = 0.0f;
//    }
//}
//
//bool Enemy::IsFreezeHoldFinished() const {
//    return stateTimer_ >= freezeHoldDuration_;
//}
//
//void Enemy::ResetPreAttackPresentationState() {
//    tellActive_ = false;
//    fakeCommitActive_ = false;
//    freezeHoldActive_ = false;
//
//    tellDuration_ = 0.0f;
//    fakeCommitDuration_ = 0.0f;
//    freezeHoldDuration_ = 0.0f;
//}
//
//void Enemy::ResetRecoveryBranchState() {
//    recoveryBranchType_ = RecoveryBranchType::None;
//    recoveryFollowupKind_ = ActionKind::None;
//    recoveryFollowupStep_ = ActionStep::None;
//}
//
//bool Enemy::TryBranchFromRecovery(ActionKind finishedKind) {
//    ResetRecoveryBranchState();
//
//    if (!(finishedKind == ActionKind::Smash ||
//          finishedKind == ActionKind::Sweep ||
//          finishedKind == ActionKind::Rush)) {
//        return false;
//    }
//
//    float recommitChance = recommitChance_;
//    float delayedSecondChance = delayedSecondChance_;
//    float fakeoutChance = escapeFakeoutChance_;
//
//    if (playerObs_.isCounterStance) {
//        delayedSecondChance += 0.08f;
//        fakeoutChance += 0.08f;
//    }
//
//    if (postCounterRhythmTimer_ > 0.0f) {
//        recommitChance += 0.04f;
//        delayedSecondChance += 0.06f;
//        fakeoutChance += 0.12f;
//    }
//
//    if (counterMemory_.earlyCount > 0.6f) {
//        delayedSecondChance += 0.08f;
//    }
//
//    float total = recommitChance + delayedSecondChance + fakeoutChance;
//    if (total <= 0.0f) {
//        return false;
//    }
//
//    float roll = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
//    if (roll >= total) {
//        return false;
//    }
//
//    float pick =
//        static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * total;
//
//    if (pick < recommitChance) {
//        recoveryBranchType_ = RecoveryBranchType::Recommit;
//
//        if (finishedKind == ActionKind::Smash) {
//            recoveryFollowupKind_ = ActionKind::Sweep;
//        } else if (finishedKind == ActionKind::Sweep) {
//            recoveryFollowupKind_ = ActionKind::Smash;
//        } else {
//            recoveryFollowupKind_ =
//                (std::rand() % 2 == 0) ? ActionKind::Smash : ActionKind::Sweep;
//        }
//
//        recoveryFollowupStep_ = ActionStep::Charge;
//        stateTimer_ = -RandomRange(recommitDelayMin_, recommitDelayMax_);
//        return true;
//    }
//
//    if (pick < recommitChance + delayedSecondChance) {
//        recoveryBranchType_ = RecoveryBranchType::DelayedSecond;
//
//        if (finishedKind == ActionKind::Sweep) {
//            recoveryFollowupKind_ = ActionKind::Sweep;
//        } else if (finishedKind == ActionKind::Smash) {
//            recoveryFollowupKind_ = ActionKind::Smash;
//        } else {
//            recoveryFollowupKind_ = ActionKind::Rush;
//        }
//
//        recoveryFollowupStep_ = ActionStep::Charge;
//        stateTimer_ =
//            -RandomRange(delayedSecondDelayMin_, delayedSecondDelayMax_);
//        return true;
//    }
//
//    recoveryBranchType_ = RecoveryBranchType::EscapeFakeout;
//
//    ResetWarpContext();
//    warp_.type = WarpType::Escape;
//
//    if (!DecideWarpTargetFarFromPlayer(warp_.targetPos)) {
//        ResetRecoveryBranchState();
//        return false;
//    }
//
//    warp_.hasValidTarget = true;
//
//    if (std::rand() % 2 == 0) {
//        warp_.followupKind = ActionKind::Shot;
//        warp_.followupStep = ActionStep::Charge;
//    } else {
//        warp_.followupKind = ActionKind::Wave;
//        warp_.followupStep = ActionStep::Charge;
//    }
//
//    BeginAction(ActionKind::Warp, ActionStep::Start);
//    return true;
//}
//
//// ============================================================
//// マルギット風 学習・適応
//// ============================================================
//void Enemy::UpdateCounterAdaptation(float deltaTime) {
//    const float decay = (std::max)(0.0f, 1.0f - deltaTime * 0.55f);
//
//    counterMemory_.counterStancePressure *= decay;
//    counterMemory_.earlyCount *= decay;
//    counterMemory_.lateCount *= decay;
//    counterMemory_.successCount *= decay;
//    counterMemory_.verticalBias *= decay;
//    counterMemory_.horizontalBias *= decay;
//
//    if (playerObs_.isCounterStance) {
//        counterMemory_.counterStancePressure += deltaTime * 1.4f;
//    }
//
//    if (playerObs_.justCounterEarly) {
//        counterMemory_.earlyCount += 1.0f;
//    }
//    if (playerObs_.justCounterLate) {
//        counterMemory_.lateCount += 1.0f;
//    }
//
//    if (playerObs_.counterAxis == CounterAxis::Vertical) {
//        counterMemory_.verticalBias += deltaTime * 1.2f;
//    } else if (playerObs_.counterAxis == CounterAxis::Horizontal) {
//        counterMemory_.horizontalBias += deltaTime * 1.2f;
//    }
//
//    if (postCounterRhythmTimer_ > 0.0f) {
//        postCounterRhythmTimer_ -= deltaTime;
//        if (postCounterRhythmTimer_ < 0.0f) {
//            postCounterRhythmTimer_ = 0.0f;
//        }
//    } else {
//        forceEscapeWarpNext_ = false;
//        forceCounterBaitNext_ = false;
//    }
//}
//
//void Enemy::RegisterCounterSuccessReaction() {
//    counterMemory_.successCount += 1.4f;
//    counterMemory_.consecutiveSuccess++;
//
//    if (action_.kind == ActionKind::Smash) {
//        counterMemory_.verticalBias += 0.8f;
//    } else if (action_.kind == ActionKind::Sweep) {
//        counterMemory_.horizontalBias += 0.8f;
//    }
//
//    if (counterMemory_.consecutiveSuccess >= 2) {
//        forceEscapeWarpNext_ = true;
//        forceCounterBaitNext_ = true;
//        postCounterRhythmTimer_ = 4.0f;
//    } else {
//        postCounterRhythmTimer_ = 2.0f;
//    }
//}
//
//float Enemy::GetAdaptiveHoldChance(ActionKind kind) const {
//    float chance = 0.0f;
//
//    if (kind == ActionKind::Smash) {
//        chance = smashFeintChance_;
//        if (action_.id == ActionId::DelaySmash) {
//            chance += 0.20f;
//        }
//    } else if (kind == ActionKind::Sweep) {
//        chance = sweepFeintChance_;
//    }
//
//    chance += counterMemory_.counterStancePressure * 0.12f;
//    chance += counterMemory_.successCount * 0.08f;
//    chance += counterMemory_.earlyCount * 0.10f;
//
//    if (tactic_ == TacticState::CounterBait) {
//        chance += 0.15f;
//    }
//
//    if (chance < 0.0f) {
//        chance = 0.0f;
//    }
//    if (chance > 0.95f) {
//        chance = 0.95f;
//    }
//
//    return chance;
//}
//
//float Enemy::GetAdaptiveChargeOffset(ActionKind kind) const {
//    float offset = 0.0f;
//
//    offset += counterMemory_.earlyCount * 0.035f;
//    offset -= counterMemory_.lateCount * 0.015f;
//
//    if (postCounterRhythmTimer_ > 0.0f) {
//        if (kind == ActionKind::Smash) {
//            offset += 0.08f;
//        } else if (kind == ActionKind::Sweep) {
//            offset += 0.05f;
//        }
//    }
//
//    if (offset > 0.28f) {
//        offset = 0.28f;
//    }
//    if (offset < -0.08f) {
//        offset = -0.08f;
//    }
//
//    return offset;
//}
//
//bool Enemy::ShouldSnapReleaseFromRead() const {
//    if (currentHoldDuration_ <= 0.0f) {
//        return false;
//    }
//
//    float releaseRatio = 0.60f;
//
//    if (playerObs_.justCounterEarly) {
//        releaseRatio = 0.30f;
//    } else if (playerObs_.justCounterLate) {
//        releaseRatio = 0.75f;
//    } else if (playerObs_.justCounterFailed) {
//        releaseRatio = 0.45f;
//    } else if (playerObs_.isCounterStance) {
//        releaseRatio = 0.55f;
//    }
//
//    return stateTimer_ >= currentHoldDuration_ * releaseRatio;
//}
//
//ActionKind Enemy::DecideAdaptiveCounterBaitAction() const {
//    if (playerObs_.counterAxis == CounterAxis::Horizontal) {
//        return ActionKind::Smash;
//    }
//    if (playerObs_.counterAxis == CounterAxis::Vertical) {
//        return ActionKind::Sweep;
//    }
//
//    if (counterMemory_.horizontalBias > counterMemory_.verticalBias + 0.4f) {
//        return ActionKind::Smash;
//    }
//    if (counterMemory_.verticalBias > counterMemory_.horizontalBias + 0.4f) {
//        return ActionKind::Sweep;
//    }
//
//    return (std::rand() % 2 == 0) ? ActionKind::Smash : ActionKind::Sweep;
//}
//
//// ============================================================
//// プリセット保存用：現在値 → 構造体
//// ============================================================
//EnemyTuningPreset Enemy::CreateTuningPreset() const {
//    EnemyTuningPreset p{};
//
//    p.nearAttackDistance = nearAttackDistance_;
//    p.farAttackDistance = farAttackDistance_;
//
//    p.smash.damage = smashParam_.damage;
//    p.smash.knockback = smashParam_.knockback;
//    p.smash.hitBoxSize = smashParam_.hitBoxSize;
//    p.smashChargeTime = smashChargeTime_;
//    p.smashAttackForwardOffset = smashAttackForwardOffset_;
//    p.smashAttackHeightOffset = smashAttackHeightOffset_;
//    p.smashTiming.trackingEndTime = smashTiming_.trackingEndTime;
//
//    p.sweep.damage = sweepParam_.damage;
//    p.sweep.knockback = sweepParam_.knockback;
//    p.sweep.hitBoxSize = sweepParam_.hitBoxSize;
//    p.sweepChargeTime = sweepChargeTime_;
//    p.sweepAttackSideOffset = sweepAttackSideOffset_;
//    p.sweepAttackHeightOffset = sweepAttackHeightOffset_;
//    p.sweepTiming.trackingEndTime = sweepTiming_.trackingEndTime;
//
//    p.bullet.damage = bulletParam_.damage;
//    p.bullet.knockback = bulletParam_.knockback;
//    p.bullet.hitBoxSize = bulletParam_.hitBoxSize;
//    p.shotChargeTime = shotChargeTime_;
//    p.shotRecoveryTime = shotRecoveryTime_;
//    p.shotInterval = shotInterval_;
//    p.shotMinCount = shotMinCount_;
//    p.shotMaxCount = shotMaxCount_;
//    p.bulletSpeed = bulletSpeed_;
//    p.bulletLifeTime = bulletLifeTime_;
//    p.bulletSpawnHeightOffset = bulletSpawnHeightOffset_;
//
//    p.wave.damage = waveParam_.damage;
//    p.wave.knockback = waveParam_.knockback;
//    p.wave.hitBoxSize = waveParam_.hitBoxSize;
//    p.waveChargeTime = waveChargeTime_;
//    p.waveRecoveryTime = waveRecoveryTime_;
//    p.waveSpeed = waveSpeed_;
//    p.waveMaxDistance = waveMaxDistance_;
//    p.waveSpawnForwardOffset = waveSpawnForwardOffset_;
//    p.waveSpawnHeightOffset = waveSpawnHeightOffset_;
//
//    p.smashTiming.totalTime = smashTiming_.totalTime;
//    p.smashTiming.trackingEndTime = smashTiming_.trackingEndTime;
//    p.smashTiming.activeStartTime = smashTiming_.activeStartTime;
//    p.smashTiming.activeEndTime = smashTiming_.activeEndTime;
//    p.smashTiming.recoveryStartTime = smashTiming_.recoveryStartTime;
//
//    p.sweepTiming.totalTime = sweepTiming_.totalTime;
//    p.sweepTiming.trackingEndTime = sweepTiming_.trackingEndTime;
//    p.sweepTiming.activeStartTime = sweepTiming_.activeStartTime;
//    p.sweepTiming.activeEndTime = sweepTiming_.activeEndTime;
//    p.sweepTiming.recoveryStartTime = sweepTiming_.recoveryStartTime;
//
//    p.warpApproachChainMaxSteps = warpApproachChainMaxSteps_;
//    p.warpEscapeChainMaxSteps = warpEscapeChainMaxSteps_;
//    p.approachChainContinueDistance = approachChainContinueDistance_;
//    p.escapeChainContinueDistance = escapeChainContinueDistance_;
//
//    p.sweepWarpSmashMaxDistance = sweepWarpSmashMaxDistance_;
//    p.sweepWarpSmashChance = sweepWarpSmashChance_;
//    p.waveWarpSmashMinDistance = waveWarpSmashMinDistance_;
//    p.waveWarpSmashChance = waveWarpSmashChance_;
//
//    return p;
//}
//
//// ============================================================
//// プリセット読込用：構造体 → 現在値
//// ============================================================
//void Enemy::ApplyTuningPreset(const EnemyTuningPreset &p) {
//    nearAttackDistance_ = p.nearAttackDistance;
//    farAttackDistance_ = p.farAttackDistance;
//
//    smashParam_.damage = p.smash.damage;
//    smashParam_.knockback = p.smash.knockback;
//    smashParam_.hitBoxSize = p.smash.hitBoxSize;
//    smashChargeTime_ = p.smashChargeTime;
//    smashAttackForwardOffset_ = p.smashAttackForwardOffset;
//    smashAttackHeightOffset_ = p.smashAttackHeightOffset;
//    smashTiming_.totalTime = p.smashTiming.totalTime;
//    smashTiming_.activeStartTime = p.smashTiming.activeStartTime;
//    smashTiming_.activeEndTime = p.smashTiming.activeEndTime;
//    smashTiming_.recoveryStartTime = p.smashTiming.recoveryStartTime;
//    smashTiming_.trackingEndTime = p.smashTiming.trackingEndTime;
//
//    sweepParam_.damage = p.sweep.damage;
//    sweepParam_.knockback = p.sweep.knockback;
//    sweepParam_.hitBoxSize = p.sweep.hitBoxSize;
//    sweepChargeTime_ = p.sweepChargeTime;
//    sweepAttackSideOffset_ = p.sweepAttackSideOffset;
//    sweepAttackHeightOffset_ = p.sweepAttackHeightOffset;
//    sweepTiming_.totalTime = p.sweepTiming.totalTime;
//    sweepTiming_.activeStartTime = p.sweepTiming.activeStartTime;
//    sweepTiming_.activeEndTime = p.sweepTiming.activeEndTime;
//    sweepTiming_.recoveryStartTime = p.sweepTiming.recoveryStartTime;
//    sweepTiming_.trackingEndTime = p.sweepTiming.trackingEndTime;
//
//    bulletParam_.damage = p.bullet.damage;
//    bulletParam_.knockback = p.bullet.knockback;
//    bulletParam_.hitBoxSize = p.bullet.hitBoxSize;
//    shotChargeTime_ = p.shotChargeTime;
//    shotRecoveryTime_ = p.shotRecoveryTime;
//    shotInterval_ = p.shotInterval;
//    shotMinCount_ = p.shotMinCount;
//    shotMaxCount_ = p.shotMaxCount;
//    bulletSpeed_ = p.bulletSpeed;
//    bulletLifeTime_ = p.bulletLifeTime;
//    bulletSpawnHeightOffset_ = p.bulletSpawnHeightOffset;
//
//    waveParam_.damage = p.wave.damage;
//    waveParam_.knockback = p.wave.knockback;
//    waveParam_.hitBoxSize = p.wave.hitBoxSize;
//    waveChargeTime_ = p.waveChargeTime;
//    waveRecoveryTime_ = p.waveRecoveryTime;
//    waveSpeed_ = p.waveSpeed;
//    waveMaxDistance_ = p.waveMaxDistance;
//    waveSpawnForwardOffset_ = p.waveSpawnForwardOffset;
//    waveSpawnHeightOffset_ = p.waveSpawnHeightOffset;
//
//    warpApproachChainMaxSteps_ = p.warpApproachChainMaxSteps;
//    warpEscapeChainMaxSteps_ = p.warpEscapeChainMaxSteps;
//    approachChainContinueDistance_ = p.approachChainContinueDistance;
//    escapeChainContinueDistance_ = p.escapeChainContinueDistance;
//
//    sweepWarpSmashMaxDistance_ = p.sweepWarpSmashMaxDistance;
//    sweepWarpSmashChance_ = p.sweepWarpSmashChance;
//    waveWarpSmashMinDistance_ = p.waveWarpSmashMinDistance;
//    waveWarpSmashChance_ = p.waveWarpSmashChance;
//
//    if (nearAttackDistance_ > farAttackDistance_) {
//        farAttackDistance_ = nearAttackDistance_;
//    }
//
//    ValidateAllTimings();
//}
//
//// ============================================================
//// プリセット初期化
//// ============================================================
//void Enemy::ResetTuningPreset() { ApplyTuningPreset(EnemyTuningPreset{}); }