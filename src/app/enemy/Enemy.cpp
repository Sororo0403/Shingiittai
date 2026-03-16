#include "Enemy.h"
#include "ModelManager.h"

#include <cmath>
#include <cstdlib>

// ============================================================
// 初期化処理
// ============================================================
// - ボス本体の初期位置・初期スケールを設定する
// - 各部位（胴体・左右の手）のTransformも初期化する
void Enemy::Initialize(uint32_t modelId) {
    modelId_ = modelId;

    // ボス全体の基準位置
    tf_.position = {0.0f, 0.0f, 10.0f};
    tf_.scale = {1.0f, 1.0f, 1.0f};
    tf_.rotation = {0.0f, 0.0f, 0.0f, 1.0f};

    // 基準Transformから各部位の位置を更新
    UpdateParts();
}

// ============================================================
// 毎フレーム更新処理
// ============================================================
// - プレイヤー位置を記録
// - 現在の状態（Idle / Smash / Sweep / Shot ...）に応じた処理を実行
// - 弾・波の更新
// - 最後に各部位のTransformを更新
void Enemy::Update(const DirectX::XMFLOAT3 &playerPos, float deltaTime) {
    // HPが0なら更新しない
    if (!IsAlive()) {
        return;
    }

    // Smash timing の安全補正
    if (smashTiming_.activeStartTime < 0.0f) {
        smashTiming_.activeStartTime = 0.0f;
    }
    if (smashTiming_.activeEndTime < smashTiming_.activeStartTime) {
        smashTiming_.activeEndTime = smashTiming_.activeStartTime;
    }
    if (smashTiming_.recoveryStartTime < smashTiming_.activeEndTime) {
        smashTiming_.recoveryStartTime = smashTiming_.activeEndTime;
    }
    if (smashTiming_.totalTime < smashTiming_.recoveryStartTime) {
        smashTiming_.totalTime = smashTiming_.recoveryStartTime;
    }

    // Sweep timing の安全補正
    if (sweepTiming_.activeStartTime < 0.0f) {
        sweepTiming_.activeStartTime = 0.0f;
    }
    if (sweepTiming_.activeEndTime < sweepTiming_.activeStartTime) {
        sweepTiming_.activeEndTime = sweepTiming_.activeStartTime;
    }
    if (sweepTiming_.recoveryStartTime < sweepTiming_.activeEndTime) {
        sweepTiming_.recoveryStartTime = sweepTiming_.activeEndTime;
    }
    if (sweepTiming_.totalTime < sweepTiming_.recoveryStartTime) {
        sweepTiming_.totalTime = sweepTiming_.recoveryStartTime;
    }

    // プレイヤー位置を保存
    playerPos_ = playerPos;

    // Idle中は常にプレイヤー方向を向く
    if (state_ == EnemyState::Idle) {
        UpdateFacingToPlayer();
    }

    // 状態経過時間を進める
    stateTimer_ += deltaTime;

    // 攻撃判定・ガード判定は毎フレームいったんOFFにして、
    // 必要な状態のときだけ各更新関数内でONにする
    isAttackActive_ = false;
    isGuardActive_ = false;

    // 現在の状態に応じた更新処理を呼ぶ
    switch (state_) {
    case EnemyState::Idle:
        UpdateIdle(deltaTime);
        break;

    case EnemyState::SmashCharge:
        UpdateSmashCharge(deltaTime);
        break;
    case EnemyState::SmashAttack:
        UpdateSmashAttack(deltaTime);
        break;
    case EnemyState::SmashRecovery:
        UpdateSmashRecovery(deltaTime);
        break;

    case EnemyState::SweepCharge:
        UpdateSweepCharge(deltaTime);
        break;
    case EnemyState::SweepAttack:
        UpdateSweepAttack(deltaTime);
        break;
    case EnemyState::SweepRecovery:
        UpdateSweepRecovery(deltaTime);
        break;

    case EnemyState::ShotCharge:
        UpdateShotCharge(deltaTime);
        break;
    case EnemyState::ShotFire:
        UpdateShotFire(deltaTime);
        break;
    case EnemyState::ShotRecovery:
        UpdateShotRecovery(deltaTime);
        break;

    case EnemyState::WarpStart:
        UpdateWarpStart(deltaTime);
        break;
    case EnemyState::WarpMove:
        UpdateWarpMove(deltaTime);
        break;
    case EnemyState::WarpEnd:
        UpdateWarpEnd(deltaTime);
        break;

    case EnemyState::WaveCharge:
        UpdateWaveCharge(deltaTime);
        break;
    case EnemyState::WaveFire:
        UpdateWaveFire(deltaTime);
        break;
    case EnemyState::WaveRecovery:
        UpdateWaveRecovery(deltaTime);
        break;

    case EnemyState::GuardMove:
        UpdateGuardMove(deltaTime);
        break;
    case EnemyState::GuardHold:
        UpdateGuardHold(deltaTime);
        break;
    case EnemyState::GuardRecovery:
        UpdateGuardRecovery(deltaTime);
        break;
    }

    // 弾・波は状態とは独立して更新する
    UpdateBullets(deltaTime);
    UpdateWaves(deltaTime);

    // 最後に各部位の見た目位置を更新する
    UpdateParts();
}

// ============================================================
// 描画処理
// ============================================================
// - ボス本体（胴体・左右の手）を描画
// - 生存中の弾を描画
// - 生存中の波を描画
void Enemy::Draw(ModelManager *modelManager, const Camera &camera) {
    // HPが0なら描画しない
    if (!IsAlive()) {
        return;
    }

    // ワープ中など非表示状態でなければ本体を描画
    if (isVisible_) {
        modelManager->Draw(modelId_, bodyTf_, camera);
        modelManager->Draw(modelId_, leftHandTf_, camera);
        modelManager->Draw(modelId_, rightHandTf_, camera);
    }

    // 生存中の弾を描画
    for (const auto &bullet : bullets_) {
        if (!bullet.isAlive) {
            continue;
        }

        Transform bulletTf = tf_;
        bulletTf.position = bullet.position;
        bulletTf.scale = {0.2f, 0.2f, 0.2f};

        modelManager->Draw(modelId_, bulletTf, camera);
    }

    // 生存中の波を描画
    for (const auto &wave : waves_) {
        if (!wave.isAlive) {
            continue;
        }

        Transform waveTf = tf_;
        waveTf.position = wave.position;
        waveTf.scale = {0.6f, 0.2f, 1.2f};

        modelManager->Draw(modelId_, waveTf, camera);
    }
}

// ============================================================
// 被ダメージ処理
// ============================================================
// - HPを減らす
// - 0未満にはならないようにする
void Enemy::TakeDamage(float damage) {
    hp_ -= damage;

    if (hp_ < 0.0f) {
        hp_ = 0.0f;
    }
}

// ============================================================
// 各部位Transform更新処理
// ============================================================
// - ボス全体の基準Transformから胴体・左右の手の位置を決める
// - 状態に応じて手の位置を変え、疑似的にアニメーションさせる
void Enemy::UpdateParts() {
    // ------------------------------------------------------------
    // 向き計算
    // ------------------------------------------------------------
    // 通常は現在の向きを使用する。
    // ただし近接攻撃中は、攻撃開始時に固定した向きを使うことで
    // 攻撃途中でプレイヤー方向へ不自然に追従しないようにする。
    float usedYaw = facingYaw_;

    if (ShouldUseLockedAttackYaw()) {
        usedYaw = lockedAttackYaw_;
    }

    // 前方向ベクトル
    float forwardX = std::sinf(usedYaw);
    float forwardZ = std::cosf(usedYaw);

    // 右方向ベクトル
    float rightX = std::cosf(usedYaw);
    float rightZ = -std::sinf(usedYaw);

    // ------------------------------------------------------------
    // 基本位置の設定
    // ------------------------------------------------------------
    // 胴体
    bodyTf_ = tf_;
    bodyTf_.position = tf_.position;
    bodyTf_.scale = {1.2f, 1.4f, 0.8f};

    // 左手
    leftHandTf_ = tf_;
    leftHandTf_.position = tf_.position;
    leftHandTf_.position.x += (-rightX) * 1.2f;
    leftHandTf_.position.y += 0.9f;
    leftHandTf_.position.z += (-rightZ) * 1.2f;
    leftHandTf_.scale = {0.6f, 0.6f, 0.6f};

    // 右手
    rightHandTf_ = tf_;
    rightHandTf_.position = tf_.position;
    rightHandTf_.position.x += rightX * 1.2f;
    rightHandTf_.position.y += 0.9f;
    rightHandTf_.position.z += rightZ * 1.2f;
    rightHandTf_.scale = {0.6f, 0.6f, 0.6f};

    // ------------------------------------------------------------
    // 状態ごとの疑似アニメーション
    // ------------------------------------------------------------
    // 実アニメーション未接続時でも、手の位置をずらすことで
    // 「構え」「振り下ろし」「薙ぎ払い」などの見た目を出している。
    if (state_ == EnemyState::SmashCharge) {
        // 振り下ろし準備：
        // 右手を上に持ち上げつつ、少し後ろへ引く
        rightHandTf_.position.y += 1.5f;
        rightHandTf_.position.x += (-forwardX) * 0.5f;
        rightHandTf_.position.z += (-forwardZ) * 0.5f;

    } else if (state_ == EnemyState::SmashAttack) {
        // 振り下ろし本体：
        // 右手を前下方向へ動かす
        rightHandTf_.position.y -= 0.2f;
        rightHandTf_.position.x += forwardX * 1.8f;
        rightHandTf_.position.z += forwardZ * 1.8f;

    } else if (state_ == EnemyState::SmashRecovery) {
        // 振り下ろし後の戻り
        rightHandTf_.position.y += 0.3f;
        rightHandTf_.position.x += forwardX * 0.8f;
        rightHandTf_.position.z += forwardZ * 0.8f;

    } else if (state_ == EnemyState::SweepCharge) {
        // 薙ぎ払い準備：
        // 右側へ大きく引く
        rightHandTf_.position.x += rightX * 1.4f;
        rightHandTf_.position.y += 0.4f;
        rightHandTf_.position.z += rightZ * 1.4f;

    } else if (state_ == EnemyState::SweepAttack) {
        // 薙ぎ払い本体：
        // 右から左へ横切る
        rightHandTf_.position.x += (-rightX) * 1.6f;
        rightHandTf_.position.y += 0.2f;
        rightHandTf_.position.z += (-rightZ) * 1.6f;

    } else if (state_ == EnemyState::SweepRecovery) {
        // 薙ぎ払い後の戻り
        rightHandTf_.position.x += rightX * 0.3f;
        rightHandTf_.position.y += 0.1f;
        rightHandTf_.position.z += rightZ * 0.3f;

    } else if (state_ == EnemyState::ShotCharge) {
        // 弾攻撃準備：
        // 少し前へ出して溜める
        rightHandTf_.position.y += 0.5f;
        rightHandTf_.position.x += forwardX * 0.8f;
        rightHandTf_.position.z += forwardZ * 0.8f;

    } else if (state_ == EnemyState::ShotFire) {
        // 弾発射中：
        // 前に構えた状態を維持する
        rightHandTf_.position.y += 0.3f;
        rightHandTf_.position.x += forwardX * 1.0f;
        rightHandTf_.position.z += forwardZ * 1.0f;

    } else if (state_ == EnemyState::ShotRecovery) {
        // 弾攻撃後の戻り
        rightHandTf_.position.y += 0.2f;
        rightHandTf_.position.x += forwardX * 0.4f;
        rightHandTf_.position.z += forwardZ * 0.4f;

    } else if (state_ == EnemyState::WarpEnd) {
        // ワープ終了後：
        // やや前に構えた見た目にする
        rightHandTf_.position.y += 0.2f;
        rightHandTf_.position.x += forwardX * 0.3f;
        rightHandTf_.position.z += forwardZ * 0.3f;

    } else if (state_ == EnemyState::WaveCharge) {
        // 波攻撃準備：
        // 右手を少し上げて力を溜める
        rightHandTf_.position.y += 0.8f;
        rightHandTf_.position.x += forwardX * 0.6f;
        rightHandTf_.position.z += forwardZ * 0.6f;

    } else if (state_ == EnemyState::WaveFire) {
        // 波発射中：
        // 前に押し出すような見た目
        rightHandTf_.position.y += 0.4f;
        rightHandTf_.position.x += forwardX * 1.0f;
        rightHandTf_.position.z += forwardZ * 1.0f;

    } else if (state_ == EnemyState::WaveRecovery) {
        // 波攻撃後の戻り
        rightHandTf_.position.y += 0.2f;
        rightHandTf_.position.x += forwardX * 0.4f;
        rightHandTf_.position.z += forwardZ * 0.4f;

    } else if (state_ == EnemyState::GuardMove ||
               state_ == EnemyState::GuardHold ||
               state_ == EnemyState::GuardRecovery) {
        // ガード中は左手を防御位置に移動させる。
        // guardTarget_ に応じて防御する部位を変える。
        if (guardTarget_ == GuardTarget::Face) {
            // 顔付近を守る
            leftHandTf_.position.x += forwardX * 0.6f;
            leftHandTf_.position.y += 0.9f;
            leftHandTf_.position.z += forwardZ * 0.6f;

        } else if (guardTarget_ == GuardTarget::BodyCenter) {
            // 胴体中央を守る
            leftHandTf_.position.x += forwardX * 0.4f;
            leftHandTf_.position.y += 0.3f;
            leftHandTf_.position.z += forwardZ * 0.4f;

        } else if (guardTarget_ == GuardTarget::BodyLeft) {
            // 胴体左側を守る
            leftHandTf_.position.x += (-rightX) * 0.2f;
            leftHandTf_.position.y += 0.3f;
            leftHandTf_.position.z += (-rightZ) * 0.2f;
        }
    }
}

// ============================================================
// OBB生成共通処理
// ============================================================
// - TransformとサイズからOBBを作る
// - center.y は見た目の足元基準から中央基準へ補正している
OBB Enemy::MakeOBB(const Transform &tf, const DirectX::XMFLOAT3 &size) const {
    OBB box{};
    box.center = tf.position;
    box.center.y += size.y * 0.5f;
    box.size = size;
    box.rotation = tf.rotation;
    return box;
}

// ============================================================
// 各部位の当たり判定取得
// ============================================================
OBB Enemy::GetBodyOBB() const { return MakeOBB(bodyTf_, bodySize_); }
OBB Enemy::GetLeftHandOBB() const { return MakeOBB(leftHandTf_, handSize_); }
OBB Enemy::GetRightHandOBB() const { return MakeOBB(rightHandTf_, handSize_); }

// ============================================================
// 攻撃用OBB取得
// ============================================================
// - 近接攻撃中のみ、その攻撃に応じた判定サイズと位置を返す
// - それ以外の状態では極小サイズのダミーOBBを返す
OBB Enemy::GetAttackOBB() const {
    OBB box{};

    if (!isAttackActive_) {
        box.center = rightHandTf_.position;
        box.size = {0.1f, 0.1f, 0.1f};
        box.rotation = bodyTf_.rotation;
        return box;
    }

    float usedYaw = lockedAttackYaw_;
    float forwardX = std::sinf(usedYaw);
    float forwardZ = std::cosf(usedYaw);
    float rightX = std::cosf(usedYaw);
    float rightZ = -std::sinf(usedYaw);

    if (IsCurrentAttack(AttackType::Smash)) {
        box.center = bodyTf_.position;
        box.center.y += smashAttackHeightOffset_;
        box.center.x += forwardX * smashAttackForwardOffset_;
        box.center.z += forwardZ * smashAttackForwardOffset_;
        box.size = smashParam_.hitBoxSize;
    } else if (IsCurrentAttack(AttackType::Sweep)) {
        box.center = bodyTf_.position;
        box.center.y += sweepAttackHeightOffset_;
        box.center.x += rightX * sweepAttackSideOffset_;
        box.center.z += rightZ * sweepAttackSideOffset_;
        box.size = sweepParam_.hitBoxSize;
    } else {
        box.center = rightHandTf_.position;
        box.size = {0.1f, 0.1f, 0.1f};
    }

    box.rotation = bodyTf_.rotation;
    return box;
}

// ============================================================
// プレイヤーとの距離計算
// ============================================================
// - ボス中心とプレイヤー位置の3次元距離を返す
float Enemy::GetDistanceToPlayer() const {
    float dx = playerPos_.x - tf_.position.x;
    float dy = playerPos_.y - tf_.position.y;
    float dz = playerPos_.z - tf_.position.z;

    return std::sqrtf(dx * dx + dy * dy + dz * dz);
}

// ============================================================
// Idle更新
// ============================================================
// - 一定時間ごとに次の行動を決める
// - 近距離と遠距離で行動候補を分ける
// - 行動は固定順ではなく重み付きランダムで選ぶ
void Enemy::UpdateIdle(float deltaTime) {
    // 未使用引数警告回避
    deltaTime = deltaTime;

    // すぐ次の行動に移ると慌ただしいので、少し待つ
    if (stateTimer_ < 0.5f) {
        return;
    }

    float distance = GetDistanceToPlayer();

    // ------------------------------------------------------------
    // 近距離行動選択
    // ------------------------------------------------------------
    if (distance <= nearAttackDistance_) {
        int total = nearSmashWeight_ + nearSweepWeight_ + nearGuardWeight_;
        if (total <= 0) {
            total = 1;
        }

        int r = std::rand() % total;

        if (r < nearSmashWeight_) {
            state_ = EnemyState::SmashCharge;
            BeginAttack(AttackType::Smash, AttackPhase::Charge);
        } else if (r < nearSmashWeight_ + nearSweepWeight_) {
            state_ = EnemyState::SweepCharge;
            BeginAttack(AttackType::Sweep, AttackPhase::Charge);
        } else {
            currentAttackType_ = AttackType::None;
            currentAttackPhase_ = AttackPhase::None;
            DecideGuardTarget();
            state_ = EnemyState::GuardMove;
            stateTimer_ = 0.0f;
        }
    }
    // ------------------------------------------------------------
    // 遠距離行動選択
    // ------------------------------------------------------------
    else if (distance > farAttackDistance_) {
        int total = farShotWeight_ + farWarpWeight_ + farWaveWeight_;
        if (total <= 0) {
            total = 1;
        }

        int r = std::rand() % total;

        if (r < farShotWeight_) {
            state_ = EnemyState::ShotCharge;
            BeginAttack(AttackType::Shot, AttackPhase::Charge);
        } else if (r < farShotWeight_ + farWarpWeight_) {
            currentAttackType_ = AttackType::None;
            currentAttackPhase_ = AttackPhase::None;
            DecideWarpTargetNearPlayer();
            state_ = EnemyState::WarpStart;
            stateTimer_ = 0.0f;
        } else {
            state_ = EnemyState::WaveCharge;
            BeginAttack(AttackType::Wave, AttackPhase::Charge);
        }
    }

    // 中距離帯では何もしない設計
}

// ============================================================
// 振り下ろし攻撃更新
// ============================================================

// 振り下ろし準備
void Enemy::UpdateSmashCharge(float deltaTime) {
    deltaTime = deltaTime;

    currentAttackType_ = AttackType::Smash;
    currentAttackPhase_ = AttackPhase::Charge;

    // ため中はプレイヤー方向へ向く
    UpdateFacingToPlayer();

    if (stateTimer_ >= smashChargeTime_) {
        // 攻撃に入る瞬間の向きを固定
        LockCurrentFacing();

        state_ = EnemyState::SmashAttack;
        ChangeAttackPhase(AttackPhase::Active);
    }
}

// 振り下ろし本体
void Enemy::UpdateSmashAttack(float deltaTime) {
    deltaTime = deltaTime;

    currentAttackType_ = AttackType::Smash;
    currentAttackPhase_ = AttackPhase::Active;

    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        return;
    }

    float attackTime = GetCurrentActionTime();

    if (attackTime >= timing->activeStartTime &&
        attackTime <= timing->activeEndTime) {
        isAttackActive_ = true;
    }

    if (attackTime >= timing->recoveryStartTime) {
        state_ = EnemyState::SmashRecovery;
        ChangeAttackPhase(AttackPhase::Recovery);
    }
}

// 振り下ろし後の隙
void Enemy::UpdateSmashRecovery(float deltaTime) {
    deltaTime = deltaTime;

    currentAttackType_ = AttackType::Smash;
    currentAttackPhase_ = AttackPhase::Recovery;

    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        EndAttack();
        return;
    }

    float recoveryDuration = timing->totalTime - timing->recoveryStartTime;
    if (recoveryDuration < 0.0f) {
        recoveryDuration = 0.0f;
    }

    if (stateTimer_ >= recoveryDuration) {
        EndAttack();
    }
}

// ============================================================
// 薙ぎ払い攻撃更新
// ============================================================

// 薙ぎ払い準備
void Enemy::UpdateSweepCharge(float deltaTime) {
    deltaTime = deltaTime;

    currentAttackType_ = AttackType::Sweep;
    currentAttackPhase_ = AttackPhase::Charge;

    // ため中はプレイヤー方向へ向く
    UpdateFacingToPlayer();

    if (stateTimer_ >= sweepChargeTime_) {
        // 振る瞬間の向きを固定
        LockCurrentFacing();

        state_ = EnemyState::SweepAttack;
        ChangeAttackPhase(AttackPhase::Active);
    }
}

// 薙ぎ払い本体
void Enemy::UpdateSweepAttack(float deltaTime) {
    deltaTime = deltaTime;

    currentAttackType_ = AttackType::Sweep;
    currentAttackPhase_ = AttackPhase::Active;

    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        return;
    }

    float attackTime = GetCurrentActionTime();

    if (attackTime >= timing->activeStartTime &&
        attackTime <= timing->activeEndTime) {
        isAttackActive_ = true;
    }

    if (attackTime >= timing->recoveryStartTime) {
        state_ = EnemyState::SweepRecovery;
        ChangeAttackPhase(AttackPhase::Recovery);
    }
}

// 薙ぎ払い後の隙
void Enemy::UpdateSweepRecovery(float deltaTime) {
    deltaTime = deltaTime;

    currentAttackType_ = AttackType::Sweep;
    currentAttackPhase_ = AttackPhase::Recovery;

    const AttackTimingParam *timing = GetCurrentAttackTiming();
    if (!timing) {
        EndAttack();
        return;
    }

    float recoveryDuration = timing->totalTime - timing->recoveryStartTime;
    if (recoveryDuration < 0.0f) {
        recoveryDuration = 0.0f;
    }

    if (stateTimer_ >= recoveryDuration) {
        EndAttack();
    }
}
// ============================================================
// 向き更新処理
// ============================================================

// プレイヤー方向へ現在向きを更新する
void Enemy::UpdateFacingToPlayer() {
    float dx = playerPos_.x - tf_.position.x;
    float dz = playerPos_.z - tf_.position.z;

    // atan2 を使うことで、全方向のyawを正しく計算する
    facingYaw_ = std::atan2f(dx, dz);
}

// 現在向いている方向を攻撃用に固定する
void Enemy::LockCurrentFacing() { lockedAttackYaw_ = facingYaw_; }

// ============================================================
// 弾攻撃更新
// ============================================================

// 弾攻撃準備
void Enemy::UpdateShotCharge(float deltaTime) {
    deltaTime = deltaTime;

    currentAttackType_ = AttackType::Shot;
    currentAttackPhase_ = AttackPhase::Charge;

    // 溜め中はずっとプレイヤーを向く
    UpdateFacingToPlayer();

    if (stateTimer_ >= shotChargeTime_) {
        state_ = EnemyState::ShotFire;
        ChangeAttackPhase(AttackPhase::Active);

        shotsRemaining_ =
            shotMinCount_ + (std::rand() % (shotMaxCount_ - shotMinCount_ + 1));

        shotIntervalTimer_ = 0.0f;
    }
}

// 弾発射中
void Enemy::UpdateShotFire(float deltaTime) {
    currentAttackType_ = AttackType::Shot;
    currentAttackPhase_ = AttackPhase::Active;

    shotIntervalTimer_ += deltaTime;

    if (shotsRemaining_ > 0 && shotIntervalTimer_ >= shotInterval_) {
        SpawnBullet();
        shotsRemaining_--;
        shotIntervalTimer_ = 0.0f;
    }

    if (shotsRemaining_ <= 0) {
        state_ = EnemyState::ShotRecovery;
        ChangeAttackPhase(AttackPhase::Recovery);
    }
}

// 弾攻撃後の隙
void Enemy::UpdateShotRecovery(float deltaTime) {
    deltaTime = deltaTime;

    currentAttackType_ = AttackType::Shot;
    currentAttackPhase_ = AttackPhase::Recovery;

    if (stateTimer_ >= shotRecoveryTime_) {
        EndAttack();
    }
}

// ============================================================
// 弾生成・弾更新
// ============================================================

// プレイヤー方向へ飛ぶ弾を1発生成する
void Enemy::SpawnBullet() {
    EnemyBullet bullet{};

    // 右手位置からプレイヤー方向へのベクトルを計算
    float dirX = playerPos_.x - rightHandTf_.position.x;
    float dirY = playerPos_.y - rightHandTf_.position.y;
    float dirZ = playerPos_.z - rightHandTf_.position.z;

    float len = std::sqrtf(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (len <= 0.0001f) {
        len = 1.0f;
    }

    // 正規化
    dirX /= len;
    dirY /= len;
    dirZ /= len;

    // 右手位置から弾を出す
    bullet.position = rightHandTf_.position;
    bullet.position.y += bulletSpawnHeightOffset_;

    // 速度・寿命を設定
    bullet.velocity = {dirX * bulletSpeed_, dirY * bulletSpeed_,
                       dirZ * bulletSpeed_};
    bullet.lifeTime = bulletLifeTime_;
    bullet.isAlive = true;

    bullets_.push_back(bullet);
}

// 全弾を更新する
void Enemy::UpdateBullets(float deltaTime) {
    for (auto &bullet : bullets_) {
        if (!bullet.isAlive) {
            continue;
        }

        // 位置更新
        bullet.position.x += bullet.velocity.x * deltaTime;
        bullet.position.y += bullet.velocity.y * deltaTime;
        bullet.position.z += bullet.velocity.z * deltaTime;

        // 寿命を減らし、0以下なら消滅
        bullet.lifeTime -= deltaTime;
        if (bullet.lifeTime <= 0.0f) {
            bullet.isAlive = false;
        }
    }
}

// ============================================================
// ワープ処理
// ============================================================

// プレイヤー近くにワープ先を決める
void Enemy::DecideWarpTargetNearPlayer() {
    // プレイヤーの周囲にランダム角度でワープする
    float angle = (std::rand() % 360) * 3.14159265f / 180.0f;

    warpTargetPos_ = playerPos_;
    warpTargetPos_.x += std::cosf(angle) * warpRadius_;
    warpTargetPos_.z += std::sinf(angle) * warpRadius_;

    // 地面高さは現状のボス高さをそのまま使う
    warpTargetPos_.y = tf_.position.y;
}

// ワープ開始：非表示にする
void Enemy::UpdateWarpStart(float deltaTime) {
    // 未使用引数警告回避
    deltaTime = deltaTime;

    isVisible_ = false;

    if (stateTimer_ >= warpStartTime_) {
        state_ = EnemyState::WarpMove;
        stateTimer_ = 0.0f;
    }
}

// ワープ移動：座標を瞬間移動させる
void Enemy::UpdateWarpMove(float deltaTime) {
    // 未使用引数警告回避
    deltaTime = deltaTime;

    tf_.position = warpTargetPos_;

    // ワープ先でプレイヤー方向を向き直し、その向きを固定
    UpdateFacingToPlayer();
    LockCurrentFacing();

    state_ = EnemyState::WarpEnd;
    stateTimer_ = 0.0f;
}

// ワープ終了：再表示する
void Enemy::UpdateWarpEnd(float deltaTime) {
    // 未使用引数警告回避
    deltaTime = deltaTime;

    isVisible_ = true;

    if (stateTimer_ >= warpEndTime_) {
        currentAttackType_ = AttackType::None;
        currentAttackPhase_ = AttackPhase::None;
        state_ = EnemyState::Idle;
        stateTimer_ = 0.0f;
    }
}

// ============================================================
// 波攻撃更新
// ============================================================

// 波攻撃準備
void Enemy::UpdateWaveCharge(float deltaTime) {
    deltaTime = deltaTime;

    currentAttackType_ = AttackType::Wave;
    currentAttackPhase_ = AttackPhase::Charge;

    // 溜め中はプレイヤー方向へ向く
    UpdateFacingToPlayer();

    if (stateTimer_ >= waveChargeTime_) {
        // 発射方向をここで固定
        LockCurrentFacing();

        state_ = EnemyState::WaveFire;
        ChangeAttackPhase(AttackPhase::Active);
    }
}

// 波発射
void Enemy::UpdateWaveFire(float deltaTime) {
    deltaTime = deltaTime;

    currentAttackType_ = AttackType::Wave;
    currentAttackPhase_ = AttackPhase::Active;

    SpawnWave();

    state_ = EnemyState::WaveRecovery;
    ChangeAttackPhase(AttackPhase::Recovery);
}

// 波攻撃後の隙
void Enemy::UpdateWaveRecovery(float deltaTime) {
    deltaTime = deltaTime;

    currentAttackType_ = AttackType::Wave;
    currentAttackPhase_ = AttackPhase::Recovery;

    if (stateTimer_ >= waveRecoveryTime_) {
        EndAttack();
    }
}

// ============================================================
// 波生成・波更新
// ============================================================

// 前方へ進む波を1つ生成する
void Enemy::SpawnWave() {
    EnemyWave wave{};

    // 攻撃開始時に固定した方向へ進ませる
    float usedYaw = lockedAttackYaw_;
    float forwardX = std::sinf(usedYaw);
    float forwardZ = std::cosf(usedYaw);

    // 胴体前方の地面付近から波を発生させる
    wave.position = bodyTf_.position;
    wave.position.y = tf_.position.y + waveSpawnHeightOffset_;
    wave.position.x += forwardX * waveSpawnForwardOffset_;
    wave.position.z += forwardZ * waveSpawnForwardOffset_;

    wave.direction = {forwardX, 0.0f, forwardZ};
    wave.speed = waveSpeed_;
    wave.traveledDistance = 0.0f;
    wave.maxDistance = waveMaxDistance_;
    wave.isAlive = true;

    waves_.push_back(wave);
}

// 全波を更新する
void Enemy::UpdateWaves(float deltaTime) {
    for (auto &wave : waves_) {
        if (!wave.isAlive) {
            continue;
        }

        // 進行方向へ移動
        float moveX = wave.direction.x * wave.speed * deltaTime;
        float moveZ = wave.direction.z * wave.speed * deltaTime;

        wave.position.x += moveX;
        wave.position.z += moveZ;

        // 移動距離を積算し、最大距離を超えたら消す
        float moved = std::sqrtf(moveX * moveX + moveZ * moveZ);
        wave.traveledDistance += moved;

        if (wave.traveledDistance >= wave.maxDistance) {
            wave.isAlive = false;
        }
    }
}

// ============================================================
// ガード処理
// ============================================================

// ランダムにどこを守るか決める
void Enemy::DecideGuardTarget() {
    int r = std::rand() % 3;

    if (r == 0) {
        guardTarget_ = GuardTarget::Face;
    } else if (r == 1) {
        guardTarget_ = GuardTarget::BodyCenter;
    } else {
        guardTarget_ = GuardTarget::BodyLeft;
    }
}

// ガード開始移動
void Enemy::UpdateGuardMove(float deltaTime) {
    // 未使用引数警告回避
    deltaTime = deltaTime;

    if (stateTimer_ >= guardMoveTime_) {
        state_ = EnemyState::GuardHold;
        stateTimer_ = 0.0f;
    }
}

// ガード維持
void Enemy::UpdateGuardHold(float deltaTime) {
    // 未使用引数警告回避
    deltaTime = deltaTime;

    isGuardActive_ = true;

    if (stateTimer_ >= guardHoldTime_) {
        state_ = EnemyState::GuardRecovery;
        stateTimer_ = 0.0f;
    }
}

// ガード終了
void Enemy::UpdateGuardRecovery(float deltaTime) {
    // 未使用引数警告回避
    deltaTime = deltaTime;

    if (stateTimer_ >= guardRecoveryTime_) {
        currentAttackType_ = AttackType::None;
        currentAttackPhase_ = AttackPhase::None;
        state_ = EnemyState::Idle;
        stateTimer_ = 0.0f;
        guardTarget_ = GuardTarget::None;
    }
}

//**************************
//アクションタイムの管理
//**************************

//アクションタイム
float Enemy::GetCurrentActionTime() const { return stateTimer_; }

// 現在の攻撃タイプに応じたタイミング情報を返す
const AttackTimingParam *Enemy::GetCurrentAttackTiming() const {
    switch (currentAttackType_) {
    case AttackType::Smash:
        return &smashTiming_;
    case AttackType::Sweep:
        return &sweepTiming_;
    default:
        return nullptr;
    }
}

// 攻撃開始：攻撃タイプとフェーズをセット
void Enemy::BeginAttack(AttackType type, AttackPhase phase) {
    currentAttackType_ = type;
    currentAttackPhase_ = phase;
    stateTimer_ = 0.0f;
}

// 攻撃フェーズ移行：フェーズを切り替えてタイマーリセット
void Enemy::ChangeAttackPhase(AttackPhase phase) {
    currentAttackPhase_ = phase;
    stateTimer_ = 0.0f;
}

// 攻撃終了：攻撃タイプとフェーズをリセットしてIdleに戻す
void Enemy::EndAttack() {
    currentAttackType_ = AttackType::None;
    currentAttackPhase_ = AttackPhase::None;
    state_ = EnemyState::Idle;
    stateTimer_ = 0.0f;
}

// 現在の攻撃が、向き固定して攻撃判定を出すタイプか
bool Enemy::ShouldUseLockedAttackYaw() const {
    switch (currentAttackType_) {
    case AttackType::Smash:
    case AttackType::Sweep:
        return true;
    default:
        return false;
    }
}

// 現在の攻撃が、攻撃判定が有効なフェーズに入っているか
bool Enemy::IsCurrentAttack(AttackType type) const {
    return currentAttackType_ == type;
}

// ============================================================
// プリセット保存用：現在値 → 構造体
// ============================================================
// - 現在のEnemyチューニング値をまとめて取り出す
EnemyTuningPreset Enemy::CreateTuningPreset() const {
    EnemyTuningPreset p{};

    p.nearAttackDistance = nearAttackDistance_;
    p.farAttackDistance = farAttackDistance_;

    p.smash.damage = smashParam_.damage;
    p.smash.knockback = smashParam_.knockback;
    p.smash.hitBoxSize = smashParam_.hitBoxSize;
    p.smashChargeTime = smashChargeTime_;
    //p.smashAttackTime = smashAttackTime_;
    //p.smashRecoveryTime = smashRecoveryTime_;
    p.smashAttackForwardOffset = smashAttackForwardOffset_;
    p.smashAttackHeightOffset = smashAttackHeightOffset_;
    p.smashTiming.trackingEndTime = smashTiming_.trackingEndTime;

    p.sweep.damage = sweepParam_.damage;
    p.sweep.knockback = sweepParam_.knockback;
    p.sweep.hitBoxSize = sweepParam_.hitBoxSize;
    p.sweepChargeTime = sweepChargeTime_;
    //p.sweepAttackTime = sweepAttackTime_;
    //p.sweepRecoveryTime = sweepRecoveryTime_;
    p.sweepAttackSideOffset = sweepAttackSideOffset_;
    p.sweepAttackHeightOffset = sweepAttackHeightOffset_;
    p.sweepTiming.trackingEndTime = sweepTiming_.trackingEndTime;

    p.bullet.damage = bulletParam_.damage;
    p.bullet.knockback = bulletParam_.knockback;
    p.bullet.hitBoxSize = bulletParam_.hitBoxSize;
    p.shotChargeTime = shotChargeTime_;
    p.shotRecoveryTime = shotRecoveryTime_;
    p.shotInterval = shotInterval_;
    p.shotMinCount = shotMinCount_;
    p.shotMaxCount = shotMaxCount_;
    p.bulletSpeed = bulletSpeed_;
    p.bulletLifeTime = bulletLifeTime_;
    p.bulletSpawnHeightOffset = bulletSpawnHeightOffset_;

    p.wave.damage = waveParam_.damage;
    p.wave.knockback = waveParam_.knockback;
    p.wave.hitBoxSize = waveParam_.hitBoxSize;
    p.waveChargeTime = waveChargeTime_;
    p.waveRecoveryTime = waveRecoveryTime_;
    p.waveSpeed = waveSpeed_;
    p.waveMaxDistance = waveMaxDistance_;
    p.waveSpawnForwardOffset = waveSpawnForwardOffset_;
    p.waveSpawnHeightOffset = waveSpawnHeightOffset_;

    p.smashTiming.totalTime = smashTiming_.totalTime;
    p.smashTiming.activeStartTime = smashTiming_.activeStartTime;
    p.smashTiming.activeEndTime = smashTiming_.activeEndTime;
    p.smashTiming.recoveryStartTime = smashTiming_.recoveryStartTime;

    p.sweepTiming.totalTime = sweepTiming_.totalTime;
    p.sweepTiming.activeStartTime = sweepTiming_.activeStartTime;
    p.sweepTiming.activeEndTime = sweepTiming_.activeEndTime;
    p.sweepTiming.recoveryStartTime = sweepTiming_.recoveryStartTime;

    return p;
}

// ============================================================
// プリセット読込用：構造体 → 現在値
// ============================================================
// - 保存済みプリセットの内容をEnemyに反映する
void Enemy::ApplyTuningPreset(const EnemyTuningPreset &p) {
    nearAttackDistance_ = p.nearAttackDistance;
    farAttackDistance_ = p.farAttackDistance;

    smashParam_.damage = p.smash.damage;
    smashParam_.knockback = p.smash.knockback;
    smashParam_.hitBoxSize = p.smash.hitBoxSize;
    smashChargeTime_ = p.smashChargeTime;
   /* smashAttackTime_ = p.smashAttackTime;
    smashRecoveryTime_ = p.smashRecoveryTime;*/
    smashAttackForwardOffset_ = p.smashAttackForwardOffset;
    smashAttackHeightOffset_ = p.smashAttackHeightOffset;
    smashTiming_.totalTime = p.smashTiming.totalTime;
    smashTiming_.activeStartTime = p.smashTiming.activeStartTime;
    smashTiming_.activeEndTime = p.smashTiming.activeEndTime;
    smashTiming_.recoveryStartTime = p.smashTiming.recoveryStartTime;
    smashTiming_.trackingEndTime = p.smashTiming.trackingEndTime;
    

    sweepParam_.damage = p.sweep.damage;
    sweepParam_.knockback = p.sweep.knockback;
    sweepParam_.hitBoxSize = p.sweep.hitBoxSize;
    sweepChargeTime_ = p.sweepChargeTime;
    /*sweepAttackTime_ = p.sweepAttackTime;
    sweepRecoveryTime_ = p.sweepRecoveryTime;*/
    sweepAttackSideOffset_ = p.sweepAttackSideOffset;
    sweepAttackHeightOffset_ = p.sweepAttackHeightOffset;
    sweepTiming_.totalTime = p.sweepTiming.totalTime;
    sweepTiming_.activeStartTime = p.sweepTiming.activeStartTime;
    sweepTiming_.activeEndTime = p.sweepTiming.activeEndTime;
    sweepTiming_.recoveryStartTime = p.sweepTiming.recoveryStartTime;
    sweepTiming_.trackingEndTime = p.sweepTiming.trackingEndTime;
    bulletParam_.damage = p.bullet.damage;
    bulletParam_.knockback = p.bullet.knockback;
    bulletParam_.hitBoxSize = p.bullet.hitBoxSize;
    shotChargeTime_ = p.shotChargeTime;
    shotRecoveryTime_ = p.shotRecoveryTime;
    shotInterval_ = p.shotInterval;
    shotMinCount_ = p.shotMinCount;
    shotMaxCount_ = p.shotMaxCount;
    bulletSpeed_ = p.bulletSpeed;
    bulletLifeTime_ = p.bulletLifeTime;
    bulletSpawnHeightOffset_ = p.bulletSpawnHeightOffset;

    waveParam_.damage = p.wave.damage;
    waveParam_.knockback = p.wave.knockback;
    waveParam_.hitBoxSize = p.wave.hitBoxSize;
    waveChargeTime_ = p.waveChargeTime;
    waveRecoveryTime_ = p.waveRecoveryTime;
    waveSpeed_ = p.waveSpeed;
    waveMaxDistance_ = p.waveMaxDistance;
    waveSpawnForwardOffset_ = p.waveSpawnForwardOffset;
    waveSpawnHeightOffset_ = p.waveSpawnHeightOffset;

    // near > far になると距離判定ロジックが破綻するため、
    // 必ず near <= far になるよう補正する
    if (nearAttackDistance_ > farAttackDistance_) {
        farAttackDistance_ = nearAttackDistance_;
    }
}

// ============================================================
// プリセット初期化
// ============================================================
// - デフォルト値のプリセットを適用して初期値に戻す
void Enemy::ResetTuningPreset() { ApplyTuningPreset(EnemyTuningPreset{}); }