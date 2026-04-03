#include "GameScene.h"
#include "CollisionUtil.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#include "imgui.h"
#ifdef _DEBUG
#include "DebugDraw.h"
#include "EnemyTuningPresetIO.h"
#endif // _DEBUG

using namespace DirectX;

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

    float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                   static_cast<float>(ctx_->winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.SetMode(CameraMode::LookAt);
    camera_.UpdateMatrices();
    camera_.SetPerspectiveFovDeg(currentFovDeg_);
#ifdef _DEBUG
    debugCamera_.Initialize(aspect);
    debugCamera_.SetMode(CameraMode::Free);
    debugCamera_.UpdateMatrices();

    tripodCamera_.Initialize(aspect);
    camera_.SetMode(CameraMode::LookAt);
    tripodCamera_.SetPosition(tripodPos_);
    tripodCamera_.LookAt(tripodTarget_);
    tripodCamera_.UpdateMatrices();
#endif

    currentCamera_ = &camera_;

    DirectXCommon *dx = ctx_->dxCommon;
    ModelManager *model = ctx_->model;
    TextureManager *texture = ctx_->texture;

    dx->BeginUpload();

    uint32_t playerModel = model->Load(L"resources/model/player/player.glb");
    uint32_t swordModel = model->Load(L"resources/model/player/sword.glb");
    uint32_t enemyModel = model->Load(L"resources/model/enemy/enemy.glb");

    dx->EndUpload();

    texture->ReleaseUploadBuffers();

    player_.Initialize(playerModel, swordModel);
    enemy_.Initialize(enemyModel);

    // 一人称カメラ初期向き
    cameraYaw_ = 0.0f;
    cameraPitch_ = 0.0f;
    isLockOn_ = false;
    rushChargeAssistStrength_ = 4.0f;
    rushChargeAssistMaxStep_ = 6.0f;
    rushActiveAssistStrength_ = 5.0f;
    rushActiveAssistMaxStep_ = 8.0f;
    rushLeadDistance_ = 2.5f;
    currentFovDeg_ = normalFovDeg_;
    targetFovDeg_ = normalFovDeg_;

    uint32_t bulletModel =
        ctx_->model->Load(L"resources/model/bullet/bullet.obj");
    bullet_.Initialize(bulletModel);
}

void GameScene::Update() {
    Input *input = ctx_->input;

    UpdateCamera(input);

#ifdef _DEBUG
    if (currentCamera_ == &debugCamera_) {
        return;
    }
#endif

    enemy_.Update(player_.GetTransform().position, ctx_->deltaTime,
                  player_.GetSword().IsGuard());

    // 当たり判定
    player_.Update(input, ctx_->deltaTime, enemy_.GetTransform().position);

    UpdateBattleCamera();

    auto swordBox = player_.GetSword().GetOBB();
    auto playerBox = player_.GetOBB();

    // if (CollisionUtil::CheckOBB(swordBox, bulletBox) &&
    // player_.GetSword().GetSlashMode()) {
    //     player_.GetSword().SetCounter(true);
    // }
    //  // 敵の行動状態を取得してガード状態を判定
    const ActionKind enemyActionKind = enemy_.GetActionKind();
    const ActionStep enemyActionStep = enemy_.GetActionStep();

    // 敵ヒットクールダウンの更新
    if (enemyHitCooldown_ > 0.0f) {
        enemyHitCooldown_ -= ctx_->deltaTime;
        if (enemyHitCooldown_ < 0.0f) {
            enemyHitCooldown_ = 0.0f;
        }
    }

    // プレイヤーのヒットクールダウン更新
    if (playerHitCooldown_ > 0.0f) {
        playerHitCooldown_ -= ctx_->deltaTime;
        if (playerHitCooldown_ < 0.0f) {
            playerHitCooldown_ = 0.0f;
        }
    }

    // 毎フレームいったんリセット
    dbgHitLeftHand_ = false;
    dbgHitRightHand_ = false;
    dbgHitBody_ = false;
    dbgWaveHitPlayer_ = false;
    dbgPlayerGuardedHit_ = false;

    // プレイヤーの攻撃判定とあたり判定
    if (player_.GetSword().IsSlashMode()) {
        // auto swordBox = player_.GetSword().GetOBB();

        auto bodyBox = enemy_.GetBodyOBB();
        auto leftHandBox = enemy_.GetLeftHandOBB();
        auto rightHandBox = enemy_.GetRightHandOBB();

        bool hitLeftHand = CollisionUtil::CheckOBB(swordBox, leftHandBox);
        bool hitRightHand = CollisionUtil::CheckOBB(swordBox, rightHandBox);
        bool hitBody = CollisionUtil::CheckOBB(swordBox, bodyBox);

        dbgHitLeftHand_ = hitLeftHand;
        dbgHitRightHand_ = hitRightHand;
        dbgHitBody_ = hitBody;

        const bool isEnemyGuardHold = (enemyActionKind == ActionKind::Guard &&
                                       enemyActionStep == ActionStep::Hold);

        if (enemyHitCooldown_ <= 0.0f) {
            // 左手ガード中は左手優先
            if (isEnemyGuardHold && hitLeftHand) {
                enemyHitCooldown_ = 0.2f;
            } else if (hitBody) {
                enemy_.TakeDamage(10.0f);
                enemyHitCooldown_ = 0.2f;
            }
        }
    }

    // ボスの攻撃判定とあたり判定
    const bool isEnemySmashActive = (enemyActionKind == ActionKind::Smash &&
                                     enemyActionStep == ActionStep::Active);

    const bool isEnemySweepActive = (enemyActionKind == ActionKind::Sweep &&
                                     enemyActionStep == ActionStep::Active);

    const bool isEnemyMeleeActive = isEnemySmashActive || isEnemySweepActive;

    const float enemyAttackDamage = enemy_.GetCurrentAttackDamage();
    const float enemyAttackKnockback = enemy_.GetCurrentAttackKnockback();

    bool bossHitPlayer = false;

    if (isEnemyMeleeActive) {
        auto enemyAttackBox = enemy_.GetAttackOBB();

        bossHitPlayer = CollisionUtil::CheckOBB(enemyAttackBox, playerBox);

        if (bossHitPlayer && playerHitCooldown_ <= 0.0f) {
            float dx = player_.GetTransform().position.x -
                       enemy_.GetTransform().position.x;
            float dz = player_.GetTransform().position.z -
                       enemy_.GetTransform().position.z;
            float len = std::sqrt(dx * dx + dz * dz);
            if (len < 0.0001f) {
                len = 1.0f;
            }

            dx /= len;
            dz /= len;

            if (player_.GetSword().IsGuard()) {
                dbgPlayerGuardedHit_ = true;
                player_.AddKnockback({dx * (enemyAttackKnockback * 0.5f), 0.0f,
                                      dz * (enemyAttackKnockback * 0.5f)});
                playerHitCooldown_ = 0.2f;
            } else {
                player_.TakeDamage(enemyAttackDamage);
                player_.AddKnockback({dx * enemyAttackKnockback, 0.0f,
                                      dz * enemyAttackKnockback});
                playerHitCooldown_ = 0.4f;
            }
        }
    }

    dbgBossHitPlayer_ = bossHitPlayer;

    dbgBulletHitPlayer_ = false;

    for (const auto &bullet : enemy_.GetBullets()) {
        if (!bullet.isAlive) {
            continue;
        }

        OBB bulletBox{};
        bulletBox.center = bullet.position;
        bulletBox.size = enemy_.GetBulletHitBoxSize();
        bulletBox.rotation = player_.GetTransform().rotation;

        if (CollisionUtil::CheckOBB(bulletBox, playerBox)) {
            dbgBulletHitPlayer_ = true;

            if (playerHitCooldown_ <= 0.0f) {
                float vx = bullet.velocity.x;
                float vz = bullet.velocity.z;
                float len = std::sqrt(vx * vx + vz * vz);
                if (len < 0.0001f) {
                    len = 1.0f;
                }

                vx /= len;
                vz /= len;

                if (player_.GetSword().IsGuard()) {
                    dbgPlayerGuardedHit_ = true;
                    player_.AddKnockback(
                        {vx * (enemy_.GetBulletKnockback() * 0.5f), 0.0f,
                         vz * (enemy_.GetBulletKnockback() * 0.5f)});
                    playerHitCooldown_ = 0.15f;
                } else {
                    player_.TakeDamage(enemy_.GetBulletDamage());
                    player_.AddKnockback({vx * enemy_.GetBulletKnockback(),
                                          0.0f,
                                          vz * enemy_.GetBulletKnockback()});
                    playerHitCooldown_ = 0.3f;
                }
            }
            break;
        }
    }

    dbgWaveHitPlayer_ = false;

    for (const auto &wave : enemy_.GetWaves()) {
        if (!wave.isAlive) {
            continue;
        }

        OBB waveBox{};
        waveBox.center = wave.position;
        waveBox.size = enemy_.GetWaveHitBoxSize();
        waveBox.rotation = player_.GetTransform().rotation;

        if (CollisionUtil::CheckOBB(waveBox, playerBox)) {
            dbgWaveHitPlayer_ = true;

            if (playerHitCooldown_ <= 0.0f) {
                float vx = wave.direction.x;
                float vz = wave.direction.z;
                float len = std::sqrt(vx * vx + vz * vz);
                if (len < 0.0001f) {
                    len = 1.0f;
                }

                vx /= len;
                vz /= len;

                if (player_.GetSword().IsGuard()) {
                    dbgPlayerGuardedHit_ = true;
                    player_.AddKnockback(
                        {vx * (enemy_.GetWaveKnockback() * 0.5f), 0.0f,
                         vz * (enemy_.GetWaveKnockback() * 0.5f)});
                    playerHitCooldown_ = 0.15f;
                } else {
                    player_.TakeDamage(enemy_.GetWaveDamage());
                    player_.AddKnockback({vx * enemy_.GetWaveKnockback(), 0.0f,
                                          vz * enemy_.GetWaveKnockback()});
                    playerHitCooldown_ = 0.35f;
                }
            }
            break;
        }
    }
}
// #ifdef _DEBUG
//     DebugDraw *debugDraw = ctx_->debugDraw;
// #endif

void GameScene::Draw() {
    ctx_->model->PreDraw();

    player_.Draw(ctx_->model, *currentCamera_);
    enemy_.Draw(ctx_->model, *currentCamera_);
    int aliveBulletCount = 0;
    for (const auto &bullet : enemy_.GetBullets()) {
        if (bullet.isAlive) {
            aliveBulletCount++;
        }
    }

    int aliveWaveCount = 0;
    for (const auto &wave : enemy_.GetWaves()) {
        if (wave.isAlive) {
            aliveWaveCount++;
        }
    }
#ifdef _DEBUG
    // 当たり判定描画
    ctx_->debugDraw->DrawOBB(ctx_->model, player_.GetSword().GetOBB(),
                             *currentCamera_);

    // ボス部位
    if (enemy_.IsAlive()) {
        ctx_->debugDraw->DrawOBB(ctx_->model, enemy_.GetBodyOBB(),
                                 *currentCamera_);
        ctx_->debugDraw->DrawOBB(ctx_->model, enemy_.GetLeftHandOBB(),
                                 *currentCamera_);
        ctx_->debugDraw->DrawOBB(ctx_->model, enemy_.GetRightHandOBB(),
                                 *currentCamera_);

        const bool isEnemySmashActive =
            (enemy_.GetActionKind() == ActionKind::Smash &&
             enemy_.GetActionStep() == ActionStep::Active);
        const bool isEnemySweepActive =
            (enemy_.GetActionKind() == ActionKind::Sweep &&
             enemy_.GetActionStep() == ActionStep::Active);

        if (isEnemySmashActive || isEnemySweepActive) {
            ctx_->debugDraw->DrawOBB(ctx_->model, enemy_.GetAttackOBB(),
                                     *currentCamera_);
        }
    }
#endif

    ctx_->model->PostDraw();

#ifdef _DEBUG
    ImGui::Begin("HitInfo");
    ImGui::Text("Hit LeftHand : %s", dbgHitLeftHand_ ? "true" : "false");
    ImGui::Text("Hit RightHand: %s", dbgHitRightHand_ ? "true" : "false");
    ImGui::Text("Hit Body     : %s", dbgHitBody_ ? "true" : "false");
    ImGui::Text("Cooldown     : %.2f", enemyHitCooldown_);

    const ActionKind enemyActionKind = enemy_.GetActionKind();
    const ActionStep enemyActionStep = enemy_.GetActionStep();

    const char *actionKindName = "None";
    switch (enemyActionKind) {
    case ActionKind::Smash:
        actionKindName = "Smash";
        break;
    case ActionKind::Sweep:
        actionKindName = "Sweep";
        break;
    case ActionKind::Shot:
        actionKindName = "Shot";
        break;
    case ActionKind::Wave:
        actionKindName = "Wave";
        break;
    case ActionKind::Warp:
        actionKindName = "Warp";
        break;
    case ActionKind::Guard:
        actionKindName = "Guard";
        break;
    case ActionKind::Rush:
        actionKindName = "Rush";
        break;
    default:
        break;
    }

    const char *actionStepName = "None";
    switch (enemyActionStep) {
    case ActionStep::Charge:
        actionStepName = "Charge";
        break;
    case ActionStep::Active:
        actionStepName = "Active";
        break;
    case ActionStep::Recovery:
        actionStepName = "Recovery";
        break;
    case ActionStep::Start:
        actionStepName = "Start";
        break;
    case ActionStep::Move:
        actionStepName = "Move";
        break;
    case ActionStep::Hold:
        actionStepName = "Hold";
        break;
    case ActionStep::End:
        actionStepName = "End";
        break;
    default:
        break;
    }

    const bool isEnemySmashActive = (enemyActionKind == ActionKind::Smash &&
                                     enemyActionStep == ActionStep::Active);

    const bool isEnemySweepActive = (enemyActionKind == ActionKind::Sweep &&
                                     enemyActionStep == ActionStep::Active);

    const bool isEnemyAttackActive = isEnemySmashActive || isEnemySweepActive;

    const bool isEnemyGuardHold = (enemyActionKind == ActionKind::Guard &&
                                   enemyActionStep == ActionStep::Hold);

    ImGui::Text("ActionKind   : %s", actionKindName);
    ImGui::Text("ActionStep   : %s", actionStepName);
    ImGui::Text("AttackActive : %s", isEnemyAttackActive ? "true" : "false");
    ImGui::Text("GuardActive  : %s", isEnemyGuardHold ? "true" : "false");

    ImGui::Text("BossHitPlayer: %s", dbgBossHitPlayer_ ? "true" : "false");
    ImGui::Text("DistanceToPlayer : %.2f", enemy_.GetDistanceToPlayer());
    ImGui::Text("FacingYaw       : %.2f", enemy_.GetFacingYaw());
    ImGui::Text("LockedAttackYaw : %.2f", enemy_.GetLockedAttackYaw());
    ImGui::Text("CameraYaw       : %.2f", cameraYaw_);
    ImGui::Text("CameraPitch     : %.2f", cameraPitch_);
    ImGui::Text("LockOn          : %s", isLockOn_ ? "true" : "false");
    const bool isEnemyRushChargeDbg = (enemyActionKind == ActionKind::Rush &&
                                       enemyActionStep == ActionStep::Charge);

    const bool isEnemyRushActiveDbg = (enemyActionKind == ActionKind::Rush &&
                                       enemyActionStep == ActionStep::Active);

    ImGui::Text("RushChargeAssist: %s",
                isEnemyRushChargeDbg ? "true" : "false");
    ImGui::Text("RushActiveAssist: %s",
                isEnemyRushActiveDbg ? "true" : "false");
    ImGui::Text("Stagnant        : %s",
                enemy_.IsDistanceStagnant() ? "true" : "false");
    ImGui::Text("StagnantTimer   : %.2f", enemy_.GetStagnantTimer());
    ImGui::Text("LastDistance    : %.2f", enemy_.GetLastDistanceToPlayer());
    ImGui::Text("StagDistThresh  : %.2f",
                enemy_.GetStagnantDistanceThreshold());
    ImGui::Text("StagTimeThresh  : %.2f", enemy_.GetStagnantTimeThreshold());
    ImGui::Text("WarpBonus       : %d", enemy_.GetStagnantWarpBonus());
    ImGui::Text("BulletHitPlayer : %s", dbgBulletHitPlayer_ ? "true" : "false");
    ImGui::Text("AliveBullets    : %d", aliveBulletCount);
    auto warpPos = enemy_.GetWarpTargetPos();
    ImGui::Text("Visible         : %s", enemy_.IsVisible() ? "true" : "false");
    ImGui::Text("WarpTarget      : (%.2f, %.2f, %.2f)", warpPos.x, warpPos.y,
                warpPos.z);
    ImGui::Text("WaveHitPlayer   : %s", dbgWaveHitPlayer_ ? "true" : "false");
    ImGui::Text("AliveWaves      : %d", aliveWaveCount);
    const char *guardName = "None";
    switch (enemy_.GetGuardTarget()) {
    case GuardTarget::None:
        guardName = "None";
        break;
    case GuardTarget::Face:
        guardName = "Face";
        break;
    case GuardTarget::BodyCenter:
        guardName = "BodyCenter";
        break;
    case GuardTarget::BodyLeft:
        guardName = "BodyLeft";
        break;
    }

    ImGui::Text("GuardTarget     : %s", guardName);
    ImGui::Text("PlayerHP        : %.1f", player_.GetHP());
    ImGui::Text("PlayerHitCD     : %.2f", playerHitCooldown_);
    ImGui::Text("PlayerGuarded   : %s",
                dbgPlayerGuardedHit_ ? "true" : "false");
    ImGui::Text("PlayerGuard     : %s",
                player_.GetSword().IsGuard() ? "true" : "false");
    ImGui::Text("SmashDamage     : %.2f", enemy_.GetSmashDamage());
    ImGui::Text("SweepDamage     : %.2f", enemy_.GetSweepDamage());
    ImGui::Text("BulletDamage    : %.2f", enemy_.GetBulletDamage());
    ImGui::Text("WaveDamage      : %.2f", enemy_.GetWaveDamage());
    ImGui::Text("BulletKB        : %.2f", enemy_.GetBulletKnockback());
    ImGui::Text("WaveKB          : %.2f", enemy_.GetWaveKnockback());

    ImGui::Separator();
    ImGui::Text("=== Enemy Tuning ===");

    if (ImGui::TreeNode("Distance")) {
        ImGui::DragFloat("NearAttackDistance", &enemy_.EditNearAttackDistance(),
                         0.05f, 0.5f, 20.0f);
        ImGui::DragFloat("FarAttackDistance", &enemy_.EditFarAttackDistance(),
                         0.05f, 1.0f, 30.0f);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Smash")) {
        auto &p = enemy_.EditSmashParam();
        ImGui::DragFloat("Smash Damage", &p.damage, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Smash Knockback", &p.knockback, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat3("Smash HitBox", &p.hitBoxSize.x, 0.05f, 0.1f, 10.0f);

        float &smashCharge = enemy_.EditSmashChargeTime();
        ImGui::DragFloat("Smash Charge", &smashCharge, 0.01f, 0.0f, 5.0f);

        /* ImGui::DragFloat("Smash Attack", &enemy_.EditSmashAttackTime(),
         0.01f, 0.0f, 5.0f); ImGui::DragFloat("Smash Recovery",
         &enemy_.EditSmashRecoveryTime(), 0.01f, 0.0f, 5.0f);
         ImGui::DragFloat("Smash Active Start",
                          &enemy_.EditSmashActiveStartTime(), 0.01f,
         0.0f, 1.0f); ImGui::DragFloat("Smash Active End",
         &enemy_.EditSmashActiveEndTime(), 0.01f, 0.0f, 1.0f);*/
        if (ImGui::TreeNode("Smash Timing")) {
            auto &t = enemy_.EditSmashTiming();
            ImGui::DragFloat("Smash Total", &t.totalTime, 0.01f, 0.0f, 3.0f);
            ImGui::DragFloat("Smash Active Start", &t.activeStartTime, 0.01f,
                             0.0f, 3.0f);
            ImGui::DragFloat("Smash Active End", &t.activeEndTime, 0.01f, 0.0f,
                             3.0f);
            ImGui::DragFloat("Smash Recovery Start", &t.recoveryStartTime,
                             0.01f, 0.0f, 3.0f);

            ImGui::DragFloat("Smash Tracking End", &t.trackingEndTime, 0.01f,
                             0.0f, 2.0f);

            ImGui::TreePop();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Sweep")) {
        auto &p = enemy_.EditSweepParam();
        ImGui::DragFloat("Sweep Damage", &p.damage, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Sweep Knockback", &p.knockback, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat3("Sweep HitBox", &p.hitBoxSize.x, 0.05f, 0.1f, 10.0f);

        float &sweepCharge = enemy_.EditSweepChargeTime();
        ImGui::DragFloat("Sweep Charge", &sweepCharge, 0.01f, 0.0f, 5.0f);

        /* ImGui::DragFloat("Sweep Attack", &enemy_.EditSweepAttackTime(),
         0.01f, 0.0f, 5.0f); ImGui::DragFloat("Sweep Recovery",
         &enemy_.EditSweepRecoveryTime(), 0.01f, 0.0f, 5.0f);
         ImGui::DragFloat("Sweep Active Start",
                          &enemy_.EditSweepActiveStartTime(), 0.01f,
         0.0f, 1.0f); ImGui::DragFloat("Sweep Active End",
         &enemy_.EditSweepActiveEndTime(), 0.01f, 0.0f, 1.0f);*/
        if (ImGui::TreeNode("Sweep Timing")) {
            auto &t = enemy_.EditSweepTiming();
            ImGui::DragFloat("Sweep Total", &t.totalTime, 0.01f, 0.0f, 3.0f);
            ImGui::DragFloat("Sweep Active Start", &t.activeStartTime, 0.01f,
                             0.0f, 3.0f);
            ImGui::DragFloat("Sweep Active End", &t.activeEndTime, 0.01f, 0.0f,
                             3.0f);
            ImGui::DragFloat("Sweep Recovery Start", &t.recoveryStartTime,
                             0.01f, 0.0f, 3.0f);

            ImGui::DragFloat("Sweep Tracking End", &t.trackingEndTime, 0.01f,
                             0.0f, 2.0f);
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Bullet")) {
        auto &p = enemy_.EditBulletParam();
        ImGui::DragFloat("Bullet Damage", &p.damage, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Bullet Knockback", &p.knockback, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat3("Bullet HitBox", &p.hitBoxSize.x, 0.01f, 0.05f, 5.0f);

        ImGui::DragFloat("Bullet Speed", &enemy_.EditBulletSpeed(), 0.1f, 0.1f,
                         30.0f);
        ImGui::DragFloat("Bullet LifeTime", &enemy_.EditBulletLifeTime(), 0.01f,
                         0.1f, 10.0f);
        ImGui::DragFloat("Shot Charge", &enemy_.EditShotChargeTime(), 0.01f,
                         0.0f, 5.0f);
        ImGui::DragFloat("Shot Recovery", &enemy_.EditShotRecoveryTime(), 0.01f,
                         0.0f, 5.0f);
        ImGui::DragFloat("Shot Interval", &enemy_.EditShotInterval(), 0.01f,
                         0.01f, 2.0f);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Wave")) {
        auto &p = enemy_.EditWaveParam();
        ImGui::DragFloat("Wave Damage", &p.damage, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Wave Knockback", &p.knockback, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat3("Wave HitBox", &p.hitBoxSize.x, 0.05f, 0.1f, 10.0f);

        ImGui::DragFloat("Wave Speed", &enemy_.EditWaveSpeed(), 0.1f, 0.1f,
                         30.0f);
        ImGui::DragFloat("Wave MaxDistance", &enemy_.EditWaveMaxDistance(),
                         0.1f, 0.1f, 50.0f);
        ImGui::DragFloat("Wave Charge", &enemy_.EditWaveChargeTime(), 0.01f,
                         0.0f, 5.0f);
        ImGui::DragFloat("Wave Recovery", &enemy_.EditWaveRecoveryTime(), 0.01f,
                         0.0f, 5.0f);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Rush")) {
        auto &p = enemy_.EditRushParam();
        ImGui::DragFloat("Rush Damage", &p.damage, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Rush Knockback", &p.knockback, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat3("Rush HitBox", &p.hitBoxSize.x, 0.05f, 0.1f, 10.0f);

        ImGui::DragFloat("Rush Charge", &enemy_.EditRushChargeTime(), 0.01f,
                         0.0f, 5.0f);
        ImGui::DragFloat("Rush Speed", &enemy_.EditRushSpeed(), 0.05f, 0.0f,
                         30.0f);
        ImGui::DragFloat("Rush Move Duration", &enemy_.EditRushMoveDuration(),
                         0.01f, 0.0f, 5.0f);

        if (ImGui::TreeNode("Rush Timing")) {
            auto &t = enemy_.EditRushTiming();
            ImGui::DragFloat("Rush Total", &t.totalTime, 0.01f, 0.0f, 3.0f);
            ImGui::DragFloat("Rush Active Start", &t.activeStartTime, 0.01f,
                             0.0f, 3.0f);
            ImGui::DragFloat("Rush Active End", &t.activeEndTime, 0.01f, 0.0f,
                             3.0f);
            ImGui::DragFloat("Rush Recovery Start", &t.recoveryStartTime, 0.01f,
                             0.0f, 3.0f);
            ImGui::DragFloat("Rush Tracking End", &t.trackingEndTime, 0.01f,
                             0.0f, 2.0f);
            ImGui::TreePop();
        }

        ImGui::TreePop();
    }

    ImGui::Separator();
    ImGui::Text("=== Preset ===");

    static char presetPath[256] = "Resources/enemy_tuning.txt";
    ImGui::InputText("Preset Path", presetPath, sizeof(presetPath));

    if (ImGui::Button("Save Preset")) {
        EnemyTuningPreset preset = enemy_.CreateTuningPreset();
        EnemyTuningPresetIO::Save(presetPath, preset);
    }

    ImGui::SameLine();

    if (ImGui::Button("Load Preset")) {
        EnemyTuningPreset preset{};
        if (EnemyTuningPresetIO::Load(presetPath, preset)) {
            enemy_.ApplyTuningPreset(preset);
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Reset Preset")) {
        enemy_.ResetTuningPreset();
    }

    if (ImGui::TreeNode("Action Weight")) {
        ImGui::DragInt("Near Smash Weight", &enemy_.EditNearSmashWeight(), 1.0f,
                       0, 100);
        ImGui::DragInt("Near Sweep Weight", &enemy_.EditNearSweepWeight(), 1.0f,
                       0, 100);
        ImGui::DragInt("Near Guard Weight", &enemy_.EditNearGuardWeight(), 1.0f,
                       0, 100);

        ImGui::DragInt("Far Shot Weight", &enemy_.EditFarShotWeight(), 1.0f, 0,
                       100);
        ImGui::DragInt("Far Warp Weight", &enemy_.EditFarWarpWeight(), 1.0f, 0,
                       100);
        ImGui::DragInt("Far Wave Weight", &enemy_.EditFarWaveWeight(), 1.0f, 0,
                       100);
        ImGui::TreePop();
    }

    ImGui::Separator();
    ImGui::Text("Chain: Sweep -> Warp -> Smash");
    ImGui::DragFloat("Sweep Warp Smash MaxDist",
                     &enemy_.EditSweepWarpSmashMaxDistance(), 0.1f, 0.0f,
                     20.0f);
    ImGui::DragFloat("Sweep Warp Smash Chance",
                     &enemy_.EditSweepWarpSmashChance(), 0.01f, 0.0f, 1.0f);

    ImGui::Text("Chain: Wave -> Warp -> Smash");
    ImGui::DragFloat("Wave Warp Smash MinDist",
                     &enemy_.EditWaveWarpSmashMinDistance(), 0.1f, 0.0f, 20.0f);
    ImGui::DragFloat("Wave Warp Smash Chance",
                     &enemy_.EditWaveWarpSmashChance(), 0.01f, 0.0f, 1.0f);

    ImGui::End();

    ImGui::Begin("Camera");

    if (ImGui::Button("Normal")) {
        currentCamera_ = &camera_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Debug")) {
        currentCamera_ = &debugCamera_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Tripod")) {
        currentCamera_ = &tripodCamera_;
    }

    if (ImGui::Button("Snap From Current")) {
        tripodPos_ = currentCamera_->GetPosition();

        // DebugCamera対策：forwardからtarget作る
        DirectX::XMFLOAT3 rot = currentCamera_->GetRotation();

        float cosPitch = std::cosf(rot.x);

        DirectX::XMFLOAT3 forward = {std::sinf(rot.y) * cosPitch,
                                     std::sinf(rot.x),
                                     std::cosf(rot.y) * cosPitch};

        tripodTarget_ = {tripodPos_.x + forward.x, tripodPos_.y + forward.y,
                         tripodPos_.z + forward.z};
    }

    ImGui::End();
#endif
}

// void GameScene::UpdateCamera(Input *input) {
// #ifdef _DEBUG
//     if (input->IsKeyTrigger(DIK_F11)) {
//         if (currentCamera_ == &camera_) {
//             currentCamera_ = &debugCamera_;
//         } else {
//             currentCamera_ = &camera_;
//         }
//     }
//
//     if (currentCamera_ == &debugCamera_) {
//         debugCamera_.Update(*input, ctx_->deltaTime);
//         currentCamera_->UpdateMatrices();
//         return;
//     }
// #else
//     (void)input;
// #endif
// }

// void GameScene::UpdateBattleCamera() {
//     auto &playerTf = player_.GetTransform();
//     auto &enemyTf = enemy_.GetTransform();
//
//     XMFLOAT3 playerPos = playerTf.position;
//     XMFLOAT3 enemyPos = enemyTf.position;
//
//     XMVECTOR playerPosV = XMLoadFloat3(&playerPos);
//     XMVECTOR enemyPosV = XMLoadFloat3(&enemyPos);
//
//     XMVECTOR forward = XMVector3Normalize(enemyPosV - playerPosV);
//
//     XMVECTOR camPos = playerPosV - forward * kCameraDistance +
//                       XMVectorSet(0, kCameraHeight, 0, 0);
//
//     XMFLOAT3 cameraPos;
//     XMStoreFloat3(&cameraPos, camPos);
//
//     camera_.SetPosition(cameraPos);
//     camera_.LookAt({enemyPos.x, kCameraHeight, enemyPos.z});
// }

void GameScene::UpdateCamera(Input *input) {
#ifdef _DEBUG
    // 切り替え
    if (input->IsKeyTrigger(DIK_F11)) {
        currentCamera_ = &debugCamera_;
    }
    if (input->IsKeyTrigger(DIK_F10)) {
        currentCamera_ = &camera_;
    }
    if (input->IsKeyTrigger(DIK_F9)) {
        currentCamera_ = &tripodCamera_;
    }

    // DebugCamera
    if (currentCamera_ == &debugCamera_) {
        debugCamera_.Update(*input, ctx_->deltaTime);
        debugCamera_.UpdateMatrices();
        return;
    }

    // TripodCamera（完全固定）
    if (currentCamera_ == &tripodCamera_) {
        tripodCamera_.SetPosition(tripodPos_);
        tripodCamera_.LookAt(tripodTarget_);
        tripodCamera_.UpdateMatrices();
        return;
    }
#endif

    // ===== 通常カメラ =====

    // ロックオン切り替え
    if (input->IsKeyTrigger(DIK_Q)) {
        isLockOn_ = !isLockOn_;
    }

    float yawInput = 0.0f;
    float pitchInput = 0.0f;

#ifdef _DEBUG
    if (input->IsKeyPress(DIK_LEFT)) {
        yawInput -= 1.0f;
    }
    if (input->IsKeyPress(DIK_RIGHT)) {
        yawInput += 1.0f;
    }
    if (input->IsKeyPress(DIK_UP)) {
        pitchInput += 1.0f;
    }
    if (input->IsKeyPress(DIK_DOWN)) {
        pitchInput -= 1.0f;
    }
#endif

    cameraYaw_ += yawInput * cameraLookSensitivity_;
    cameraPitch_ += pitchInput * cameraLookSensitivity_;

    if (cameraPitch_ < cameraPitchMin_) {
        cameraPitch_ = cameraPitchMin_;
    }
    if (cameraPitch_ > cameraPitchMax_) {
        cameraPitch_ = cameraPitchMax_;
    }

    // ⭐ 最重要：ここでカメラ確定
    UpdateBattleCamera();

    // ⭐ 最重要：行列更新
    camera_.UpdateMatrices();
}

void GameScene::UpdateBattleCamera() {
    const auto &playerTf = player_.GetTransform();
    const auto &enemyTf = enemy_.GetTransform();

    const DirectX::XMFLOAT3 &playerPos = playerTf.position;
    const DirectX::XMFLOAT3 &enemyPos = enemyTf.position;

    // プレイヤー頭部位置にカメラを置く
    DirectX::XMFLOAT3 cameraPos = {playerPos.x + fpCameraOffset_.x,
                                   playerPos.y + fpCameraOffset_.y,
                                   playerPos.z + fpCameraOffset_.z};

    // 敵行動状態を取得
    const ActionKind enemyActionKind = enemy_.GetActionKind();
    const ActionStep enemyActionStep = enemy_.GetActionStep();

    const bool isEnemyRushCharge = (enemyActionKind == ActionKind::Rush &&
                                    enemyActionStep == ActionStep::Charge);

    const bool isEnemyRushActive = (enemyActionKind == ActionKind::Rush &&
                                    enemyActionStep == ActionStep::Active);

    const bool isEnemyWarpStart = (enemyActionKind == ActionKind::Warp &&
                                   enemyActionStep == ActionStep::Start);

    const bool isEnemyWarpMove = (enemyActionKind == ActionKind::Warp &&
                                  enemyActionStep == ActionStep::Move);

    const bool isEnemyWarpEnd = (enemyActionKind == ActionKind::Warp &&
                                 enemyActionStep == ActionStep::End);

    // =========================
    // FOVターゲット決定
    // =========================
    targetFovDeg_ = normalFovDeg_;

    if (isLockOn_) {
        targetFovDeg_ = lockOnFovDeg_;
    }

    if (isEnemyRushCharge || isEnemyRushActive) {
        targetFovDeg_ = rushFovDeg_;
    }

    if (isEnemyWarpStart || isEnemyWarpMove || isEnemyWarpEnd) {
        targetFovDeg_ = warpFovDeg_;
    }

    // 線形補間で滑らかに変更
    float fovAlpha = fovLerpSpeed_ * ctx_->deltaTime;
    if (fovAlpha > 1.0f) {
        fovAlpha = 1.0f;
    }
    currentFovDeg_ += (targetFovDeg_ - currentFovDeg_) * fovAlpha;

    // 必要なら Camera 側に FOV setter がある前提
    camera_.SetPerspectiveFovDeg(currentFovDeg_);

    // =========================
    // ロックオン中だけ yaw 補助
    // =========================
    if (isLockOn_) {
        DirectX::XMFLOAT3 assistTarget = enemyPos;

        float assistStrength = lockOnAssistStrength_;
        float assistMaxStep = lockOnAssistMaxStep_;

        // Rush Active 中は敵の進行方向の少し先を見る
        if (isEnemyRushActive) {
            float enemyYaw = enemy_.GetFacingYaw();
            assistTarget.x += std::sinf(enemyYaw) * rushLeadDistance_;
            assistTarget.z += std::cosf(enemyYaw) * rushLeadDistance_;

            assistStrength = rushActiveAssistStrength_;
            assistMaxStep = rushActiveAssistMaxStep_;
        }
        // Rush Charge 中は敵本体へ強めに寄せる
        else if (isEnemyRushCharge) {
            assistStrength = rushChargeAssistStrength_;
            assistMaxStep = rushChargeAssistMaxStep_;
        }
        // Warp Start 中は消失前の位置を見失いにくくする
        else if (isEnemyWarpStart) {
            assistStrength = warpStartAssistStrength_;
            assistMaxStep = warpStartAssistMaxStep_;
        }
        // Warp Move 中はカメラを無理に振り回さない
        else if (isEnemyWarpMove) {
            assistStrength = lockOnAssistStrength_ * 0.0f;
            assistMaxStep = lockOnAssistMaxStep_ * 0.0f;
        }
        // Warp End 中は再出現位置へ再捕捉しやすくする
        else if (isEnemyWarpEnd) {
            assistStrength = warpEndAssistStrength_;
            assistMaxStep = warpEndAssistMaxStep_;
        }

        float dx = assistTarget.x - cameraPos.x;
        float dz = assistTarget.z - cameraPos.z;

        float targetYaw = std::atan2f(dx, dz);
        float diff = targetYaw - cameraYaw_;

        while (diff > 3.14159265f) {
            diff -= 6.28318530f;
        }
        while (diff < -3.14159265f) {
            diff += 6.28318530f;
        }

        // 入力中は補助を弱める
        float inputMagnitude = 0.0f;
#ifdef _DEBUG
        Input *input = ctx_->input;
        if (input->IsKeyPress(DIK_LEFT) || input->IsKeyPress(DIK_RIGHT)) {
            inputMagnitude = 1.0f;
        }
#endif

        float assistScale = 1.0f;
        if (inputMagnitude > 0.0f) {
            assistScale = lockOnInputReduce_;
        }

        float maxStep = assistMaxStep * assistScale * ctx_->deltaTime;
        float applied = diff * assistStrength * assistScale * ctx_->deltaTime;

        if (applied > maxStep) {
            applied = maxStep;
        } else if (applied < -maxStep) {
            applied = -maxStep;
        }

        cameraYaw_ += applied;
    }

    // yaw / pitch から forward を作る
    float cosPitch = std::cosf(cameraPitch_);
    DirectX::XMFLOAT3 forward = {std::sinf(cameraYaw_) * cosPitch,
                                 std::sinf(cameraPitch_),
                                 std::cosf(cameraYaw_) * cosPitch};

    DirectX::XMFLOAT3 lookAt = {cameraPos.x + forward.x,
                                cameraPos.y + forward.y,
                                cameraPos.z + forward.z};

    camera_.SetPosition(cameraPos);
    camera_.LookAt(lookAt);
}