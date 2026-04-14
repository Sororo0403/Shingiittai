#include "GameScene.h"
#include "CollisionUtil.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "ModelManager.h"
#include "SpriteManager.h"
#include "TextureManager.h"
#include "WinApp.h"
#ifdef _DEBUG
#include "DebugDraw.h"
#endif // _DEBUG
#include "imgui.h"
#include "imgui_internal.h"
#include <cmath>
#ifdef _DEBUG
#include "EnemyTuningPresetIO.h"
#endif // _DEBUG

using namespace DirectX;

namespace {
bool IsWithinCounterJustWindow(const Enemy &enemy) {
    const AttackTimingParam *timing = enemy.GetCurrentAttackTimingPublic();
    if (!timing) {
        return false;
    }

    float t = enemy.GetCurrentActionTimePublic();

    float activeStart = timing->activeStartTime;
    float activeEnd = timing->activeEndTime;

    if (activeEnd < activeStart) {
        return false;
    }

    float activeLen = activeEnd - activeStart;
    if (activeLen <= 0.0001f) {
        return false;
    }

    float justStart = activeStart + activeLen * 0.30f;
    float justEnd = activeStart + activeLen * 0.70f;

    return (t >= justStart && t <= justEnd);
}
} // namespace

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
    warpSmokeSpriteId_ =
        ctx_->sprite->Create(L"resources/texture/effect/warp_smoke.png");

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

    // 肩越し三人称カメラ初期向き
    lockOnOrbitCameraPos_ = {0.0f, 0.0f, 0.0f};
    lockOnLookAt_ = {0.0f, 0.0f, 0.0f};

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

    // 先にプレイヤーを更新して、その結果をEnemyへ渡す
    player_.Update(input, ctx_->deltaTime, enemy_.GetTransform().position);

    PlayerCombatObservation playerObs{};
    playerObs.position = player_.GetTransform().position;
    playerObs.velocity = player_.GetVelocity();
    playerObs.isGuarding = player_.GetSword().IsGuard();
    playerObs.isCounterStance = player_.IsCounterStance();
    playerObs.justCountered = player_.JustCountered();
    playerObs.justCounterFailed = player_.JustCounterFailed();
    playerObs.justCounterEarly = player_.JustCounterEarly();
    playerObs.justCounterLate = player_.JustCounterLate();

    switch (player_.GetCounterAxis()) {
    case SwordCounterAxis::Vertical:
        playerObs.counterAxis = CounterAxis::Vertical;
        break;

    case SwordCounterAxis::Horizontal:
        playerObs.counterAxis = CounterAxis::Horizontal;
        break;

    default:
        playerObs.counterAxis = CounterAxis::None;
        break;
    }

    enemy_.Update(playerObs, ctx_->deltaTime);

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

    // カウンター成立条件
    const bool isCounterAxisMatch =
        (isEnemySmashActive &&
         player_.GetCounterAxis() == SwordCounterAxis::Vertical) ||
        (isEnemySweepActive &&
         player_.GetCounterAxis() == SwordCounterAxis::Horizontal);

    const bool canCounterThisHit =
        player_.IsCounterStance() && isCounterAxisMatch;

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

            // 1. カウンター成功
            if (canCounterThisHit) {
                player_.GetSword().NotifyCounterSuccess();

                // 仮のカウンターダメージ
                enemy_.TakeDamage(enemyAttackDamage);

                // プレイヤーはこのヒットでダメージを受けない
                playerHitCooldown_ = 0.2f;

                // デバッグ上は「被弾扱い」にしない
                bossHitPlayer = false;
            }
            // 2. ガード
            else if (player_.GetSword().IsGuard()) {
                dbgPlayerGuardedHit_ = true;
                enemy_.NotifyAttackGuarded();
                player_.AddKnockback({dx * (enemyAttackKnockback * 0.5f), 0.0f,
                                      dz * (enemyAttackKnockback * 0.5f)});
                playerHitCooldown_ = 0.2f;
            }
            // 3. 通常被弾
            else {
                enemy_.NotifyAttackConnected();
                player_.TakeDamage(enemyAttackDamage);
                player_.AddKnockback({dx * enemyAttackKnockback, 0.0f,
                                      dz * enemyAttackKnockback});
                playerHitCooldown_ = 0.4f;
            }
        }
    }

    dbgBossHitPlayer_ = bossHitPlayer;

    dbgBulletHitPlayer_ = false;

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

            enemy_.ConsumeBullet(i);
            break;
        }
    }

    dbgWaveHitPlayer_ = false;

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

            enemy_.ConsumeWave(i);
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
#endif // _DEBUG
    ctx_->model->PostDraw();

    DrawWarpSmokePass();
    DrawWarpDistortionPass();

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

    const char *tacticName = "Neutral";
    switch (enemy_.GetTacticState()) {
    case TacticState::Neutral:
        tacticName = "Neutral";
        break;
    case TacticState::Pressure:
        tacticName = "Pressure";
        break;
    case TacticState::CounterBait:
        tacticName = "CounterBait";
        break;
    case TacticState::CounterPunish:
        tacticName = "CounterPunish";
        break;
    case TacticState::AntiGuard:
        tacticName = "AntiGuard";
        break;
    case TacticState::Chase:
        tacticName = "Chase";
        break;
    case TacticState::Reset:
        tacticName = "Reset";
        break;
    }

    ImGui::Text("TacticState  : %s", tacticName);

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
    case GuardTarget::BodyLeft:
        guardName = "BodyLeft";
        break;
    case GuardTarget::BodyRight:
        guardName = "BodyRight";
        break;
    }

    ImGui::Text("GuardTarget     : %s", guardName);
    ImGui::Text("PlayerHP        : %.1f", player_.GetHP());
    ImGui::Text("PlayerHitCD     : %.2f", playerHitCooldown_);
    ImGui::Text("PlayerGuarded   : %s",
                dbgPlayerGuardedHit_ ? "true" : "false");
    ImGui::Text("PlayerGuard     : %s",
                player_.GetSword().IsGuard() ? "true" : "false");

    const bool dbgCounterJustWindow =
        (enemy_.GetActionKind() == ActionKind::Smash ||
         enemy_.GetActionKind() == ActionKind::Sweep) &&
        (enemy_.GetActionStep() == ActionStep::Active) &&
        IsWithinCounterJustWindow(enemy_);

    ImGui::Text("CounterStance   : %s",
                player_.IsCounterStance() ? "true" : "false");

    const char *counterAxisName = "None";
    switch (player_.GetCounterAxis()) {
    case SwordCounterAxis::Vertical:
        counterAxisName = "Vertical";
        break;
    case SwordCounterAxis::Horizontal:
        counterAxisName = "Horizontal";
        break;
    default:
        break;
    }
    ImGui::Text("CounterAxis     : %s", counterAxisName);
    ImGui::Text("CounterJustWin  : %s",
                dbgCounterJustWindow ? "true" : "false");
    ImGui::Text("EnemyActionTime : %.3f", enemy_.GetCurrentActionTimePublic());
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

bool GameScene::ProjectWorldToScreen(const XMFLOAT3 &worldPos,
                                     XMFLOAT2 &outScreen) const {
    if (currentCamera_ == nullptr || ctx_ == nullptr || ctx_->winApp == nullptr) {
        return false;
    }

    XMMATRIX viewProj = currentCamera_->GetView() * currentCamera_->GetProj();
    XMVECTOR pos = XMVectorSet(worldPos.x, worldPos.y, worldPos.z, 1.0f);
    XMVECTOR clip = XMVector4Transform(pos, viewProj);
    float w = XMVectorGetW(clip);
    if (w <= 0.0001f) {
        return false;
    }

    float invW = 1.0f / w;
    float ndcX = XMVectorGetX(clip) * invW;
    float ndcY = XMVectorGetY(clip) * invW;
    float ndcZ = XMVectorGetZ(clip) * invW;
    if (ndcZ < 0.0f || ndcZ > 1.0f) {
        return false;
    }

    float width = static_cast<float>(ctx_->winApp->GetWidth());
    float height = static_cast<float>(ctx_->winApp->GetHeight());
    outScreen.x = (ndcX * 0.5f + 0.5f) * width;
    outScreen.y = (-ndcY * 0.5f + 0.5f) * height;
    return true;
}

void GameScene::DrawWarpSmokePass() {
    if (enemy_.GetActionKind() != ActionKind::Warp || ctx_ == nullptr ||
        ctx_->sprite == nullptr) {
        return;
    }

    ActionStep warpStep = enemy_.GetActionStep();
    float stepAlphaScale = 0.0f;
    switch (warpStep) {
    case ActionStep::Start:
        stepAlphaScale = 0.95f;
        break;
    case ActionStep::Move:
        stepAlphaScale = 1.15f;
        break;
    case ActionStep::End:
        stepAlphaScale = 1.0f;
        break;
    default:
        return;
    }

    XMFLOAT2 targetScreenF{};
    bool hasTarget = ProjectWorldToScreen(enemy_.GetWarpTargetPos(), targetScreenF);

    XMFLOAT2 sourceScreenF{};
    bool hasSource = enemy_.HasWarpDeparturePos() &&
                     ProjectWorldToScreen(enemy_.GetWarpDeparturePos(),
                                          sourceScreenF);
    if (!hasSource && !hasTarget) {
        return;
    }

    SpriteManager *spriteMgr = ctx_->sprite;
    Sprite &smoke = spriteMgr->GetSprite(warpSmokeSpriteId_);
    float time = enemy_.GetCurrentActionTimePublic();

    auto drawSmokeCluster = [&](const XMFLOAT2 &center, float sizeScale,
                                float alphaScale, float travelBiasX,
                                float travelBiasY, bool stretch,
                                const XMFLOAT4 &baseColor, int layers,
                                float radialSpreadScale) {
        for (int i = 0; i < layers; ++i) {
            float ratio = (layers > 1)
                              ? static_cast<float>(i) /
                                    static_cast<float>(layers - 1)
                              : 0.0f;
            float angle =
                time * (2.4f + radialSpreadScale * 0.45f) +
                ratio * DirectX::XM_PIDIV2 * (1.6f + radialSpreadScale);
            float driftX =
                std::cosf(angle) *
                    (14.0f + 10.0f * ratio) * radialSpreadScale +
                travelBiasX;
            float driftY =
                std::sinf(angle * 1.3f) *
                    (10.0f + 7.0f * ratio) * radialSpreadScale +
                travelBiasY;
            float sizeX = warpSmokeBaseSizePx_ * sizeScale *
                          (1.0f + 0.20f * ratio);
            float sizeY = warpSmokeBaseSizePx_ * sizeScale *
                          (1.0f + 0.28f * ratio);
            if (stretch) {
                sizeY += warpSmokeMoveStretchPx_ * warpMoveSmokeStretchScale_ *
                         (0.55f + ratio * 0.45f);
                sizeX *= 0.82f;
            }

            smoke.size = {sizeX, sizeY};
            smoke.position = {center.x + driftX - sizeX * 0.5f,
                              center.y + driftY - sizeY * 0.5f};
            float alpha = warpSmokeAlpha_ * alphaScale * (1.0f - ratio * 0.16f);
            smoke.color = {baseColor.x, baseColor.y, baseColor.z,
                           baseColor.w * alpha};
            spriteMgr->Draw(warpSmokeSpriteId_);
        }
    };

    spriteMgr->PreDraw();

    if (warpStep == ActionStep::Start && hasSource) {
        drawSmokeCluster(
            sourceScreenF, warpSourceSmokeBloomScale_, stepAlphaScale,
            0.0f, -10.0f, false,
            {0.03f, 0.00f, 0.02f, warpSourceSmokeDarkAlpha_}, 6, 1.55f);
        drawSmokeCluster(
            sourceScreenF, warpSourceSmokeBloomScale_ * 0.78f,
            stepAlphaScale * 1.08f, 0.0f, -16.0f, false,
            {0.68f, 0.05f, 0.10f, warpSourceSmokeRedAlpha_}, 5, 1.15f);
    }

    if (warpStep == ActionStep::Move && hasSource) {
        XMFLOAT2 mid = {(sourceScreenF.x + targetScreenF.x) * 0.5f,
                        (sourceScreenF.y + targetScreenF.y) * 0.5f};
        float dirX = targetScreenF.x - sourceScreenF.x;
        float dirY = targetScreenF.y - sourceScreenF.y;
        float dirLen = std::sqrtf(dirX * dirX + dirY * dirY);
        if (dirLen > 0.0001f) {
            dirX /= dirLen;
            dirY /= dirLen;
        } else {
            dirX = 0.0f;
            dirY = -1.0f;
        }

        drawSmokeCluster(mid, 0.90f, stepAlphaScale * warpMoveSmokeAlphaScale_,
                         dirX * 18.0f, dirY * 10.0f, true,
                         {0.12f, 0.01f, 0.05f, 0.72f}, 4, 1.0f);
        drawSmokeCluster(
            sourceScreenF, warpSourceSmokeBloomScale_ * 1.08f,
            stepAlphaScale * 0.92f, -dirX * 6.0f,
            -18.0f + dirY * warpSourceSmokeDriftPx_ * 0.18f, false,
            {0.02f, 0.00f, 0.01f, warpSourceSmokeDarkAlpha_}, 7, 1.82f);
        drawSmokeCluster(
            sourceScreenF, warpSourceSmokeBloomScale_ * 0.82f,
            stepAlphaScale * 0.88f, dirX * 8.0f,
            -10.0f + dirY * warpSourceSmokeDriftPx_ * 0.10f, false,
            {0.72f, 0.05f, 0.12f, warpSourceSmokeRedAlpha_}, 5, 1.22f);
    }

    if (hasTarget) {
        drawSmokeCluster(targetScreenF, warpArrivalSmokeScale_,
                         stepAlphaScale * warpArrivalSmokeAlphaScale_, 0.0f,
                         -14.0f, false, {0.34f, 0.04f, 0.09f, 0.62f}, 3, 0.72f);
    }

    spriteMgr->PostDraw();
}

void GameScene::DrawWarpDistortionPass() {
    if (enemy_.GetActionKind() != ActionKind::Warp) {
        return;
    }

    ImGuiContext *imguiCtx = ImGui::GetCurrentContext();
    if (imguiCtx == nullptr || imguiCtx->Viewports.Size <= 0) {
        return;
    }

    ActionStep warpStep = enemy_.GetActionStep();
    float stepIntensity = 0.0f;
    switch (warpStep) {
    case ActionStep::Start:
        stepIntensity = 0.72f;
        break;
    case ActionStep::Move:
        stepIntensity = 1.0f;
        break;
    case ActionStep::End:
        stepIntensity = 0.84f;
        break;
    default:
        return;
    }

    XMFLOAT2 targetScreenF{};
    bool hasTarget = ProjectWorldToScreen(enemy_.GetWarpTargetPos(), targetScreenF);
    ImVec2 targetScreen(targetScreenF.x, targetScreenF.y);

    XMFLOAT2 sourceScreenF{};
    bool hasSource = enemy_.HasWarpDeparturePos() &&
                     ProjectWorldToScreen(enemy_.GetWarpDeparturePos(),
                                          sourceScreenF);
    if (!hasSource && !hasTarget) {
        return;
    }
    ImVec2 sourceScreen(sourceScreenF.x, sourceScreenF.y);

    float time = enemy_.GetCurrentActionTimePublic();
    float baseRadius = warpDistortionRadiusPx_ * (0.85f + 0.30f * stepIntensity);
    float jitter = warpDistortionJitterPx_ * (0.80f + 0.40f * std::sinf(time * 20.0f));
    float alpha = warpDistortionAlpha_ + warpDistortionMoveAlphaBonus_ *
                                              (warpStep == ActionStep::Move ? 1.0f : 0.0f);

    ImGuiViewport *mainViewport = ImGui::GetMainViewport();
    if (mainViewport == nullptr) {
        return;
    }

    ImDrawList *drawList = ImGui::GetBackgroundDrawList(mainViewport);
    if (drawList == nullptr) {
        return;
    }
    ImU32 bright = IM_COL32(255, 48, 108,
                            static_cast<int>(255.0f * alpha));
    ImU32 soft = IM_COL32(120, 22, 70,
                          static_cast<int>(255.0f * (alpha * 0.82f)));
    ImU32 slash = IM_COL32(255, 215, 235,
                           static_cast<int>(255.0f * (alpha * 0.78f)));

    auto drawDistortionAt = [&](const ImVec2 &center, float radiusScale,
                                float rotationBias) {
        constexpr int kSegments = 28;
        ImVec2 points[kSegments + 1];
        for (int i = 0; i <= kSegments; ++i) {
            float ratio = static_cast<float>(i) / static_cast<float>(kSegments);
            float angle = ratio * DirectX::XM_2PI + time * 6.0f + rotationBias;
            float wave = std::sinf(angle * 3.0f + time * 17.0f) * jitter;
            float radius = baseRadius * radiusScale + wave;
            points[i] = ImVec2(center.x + std::cosf(angle) * radius,
                               center.y + std::sinf(angle) * radius);
        }

        drawList->AddPolyline(points, kSegments + 1, soft, true,
                              warpDistortionThicknessPx_);
        drawList->AddCircle(center, baseRadius * radiusScale * 0.62f, bright, 24,
                            warpDistortionThicknessPx_ * 0.7f);
        drawList->AddCircle(center, baseRadius * radiusScale * 0.82f, bright, 28,
                            warpDistortionThicknessPx_ * 0.42f);

        for (int i = 0; i < 8; ++i) {
            float ratio = static_cast<float>(i) / 8.0f;
            float angle = ratio * DirectX::XM_2PI + time * 8.5f + rotationBias;
            float inner = baseRadius * radiusScale * 0.42f;
            float outer = inner + warpDistortionLineLengthPx_ *
                                      (0.75f + 0.25f * std::sinf(time * 18.0f + i));
            ImVec2 a(center.x + std::cosf(angle) * inner,
                     center.y + std::sinf(angle) * inner);
            ImVec2 b(center.x + std::cosf(angle) * outer,
                     center.y + std::sinf(angle) * outer);
            drawList->AddLine(a, b, bright, 1.6f);
        }
    };

    if (warpStep == ActionStep::Start && hasSource) {
        drawDistortionAt(sourceScreen, 0.88f, 0.0f);
    }

    if (warpStep == ActionStep::Move && hasSource && hasTarget) {
        ImVec2 mid((sourceScreen.x + targetScreen.x) * 0.5f,
                   (sourceScreen.y + targetScreen.y) * 0.5f);
        drawDistortionAt(mid, 0.72f, 0.6f);
        drawList->AddLine(sourceScreen, targetScreen, soft, 2.0f);
    }

    if (hasTarget) {
        ImVec2 arrivalCenter = targetScreen;
        arrivalCenter.y -= warpDistortionPreviewOffsetPx_ *
                           (warpStep == ActionStep::Start ? 0.55f : 0.18f);
        drawDistortionAt(arrivalCenter,
                         warpStep == ActionStep::Move ? 1.12f : 1.0f, 1.2f);
    }

    float dirX = 0.0f;
    float dirY = -1.0f;
    if (hasSource && hasTarget) {
        dirX = targetScreen.x - sourceScreen.x;
        dirY = targetScreen.y - sourceScreen.y;
        float dirLen = std::sqrtf(dirX * dirX + dirY * dirY);
        if (dirLen > 0.0001f) {
            dirX /= dirLen;
            dirY /= dirLen;
        } else {
            dirX = 0.0f;
            dirY = -1.0f;
        }
    }

    float perpX = -dirY;
    float perpY = dirX;
    float slashLen = baseRadius * (warpStep == ActionStep::Move ? 1.72f : 1.38f);
    float branchLen = slashLen * 0.76f;
    float branchOffset = baseRadius * 0.28f;

    if (hasTarget) {
        ImVec2 arrivalCenter = targetScreen;
        arrivalCenter.y -= warpDistortionPreviewOffsetPx_ *
                           (warpStep == ActionStep::Start ? 0.55f : 0.18f);
        ImVec2 slashA(arrivalCenter.x - perpX * slashLen,
                      arrivalCenter.y - perpY * slashLen);
        ImVec2 slashB(arrivalCenter.x + perpX * slashLen,
                      arrivalCenter.y + perpY * slashLen);
        ImVec2 slashC(arrivalCenter.x - perpX * branchLen + dirX * branchOffset,
                      arrivalCenter.y - perpY * branchLen + dirY * branchOffset);
        ImVec2 slashD(arrivalCenter.x + perpX * branchLen + dirX * branchOffset,
                      arrivalCenter.y + perpY * branchLen + dirY * branchOffset);
        ImVec2 slashE(arrivalCenter.x - perpX * (branchLen * 0.58f) -
                          dirX * branchOffset * 0.72f,
                      arrivalCenter.y - perpY * (branchLen * 0.58f) -
                          dirY * branchOffset * 0.72f);
        ImVec2 slashF(arrivalCenter.x + perpX * (branchLen * 0.58f) -
                          dirX * branchOffset * 0.72f,
                      arrivalCenter.y + perpY * (branchLen * 0.58f) -
                          dirY * branchOffset * 0.72f);

        drawList->AddLine(slashA, slashB, slash,
                          warpDistortionThicknessPx_ * 0.82f);
        drawList->AddLine(slashC, slashD, bright,
                          warpDistortionThicknessPx_ * 0.55f);
        drawList->AddLine(slashE, slashF, soft,
                          warpDistortionThicknessPx_ * 0.42f);
    }
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
    const bool isEnemyPhaseTransition = enemy_.IsPhaseTransitionActive();
    const float enemyPhaseTransitionRatio = enemy_.GetPhaseTransitionRatio();

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

    if (isEnemyPhaseTransition) {
        targetFovDeg_ = phaseTransitionFovDeg_;
    }

    float fovAlpha = (isEnemyPhaseTransition ? phaseTransitionFovLerpSpeed_
                                             : fovLerpSpeed_) *
                     ctx_->deltaTime;
    if (fovAlpha > 1.0f) {
        fovAlpha = 1.0f;
    }

    currentFovDeg_ += (targetFovDeg_ - currentFovDeg_) * fovAlpha;
    camera_.SetPerspectiveFovDeg(currentFovDeg_);

    // =========================
    // ロックオン中だけ yaw 補助
    // =========================
    if (isLockOn_) {
        DirectX::XMFLOAT3 assistTarget = enemyPos;

        float assistStrength = lockOnAssistStrength_;
        float assistMaxStep = lockOnAssistMaxStep_;

        if (isEnemyRushActive) {
            float enemyYaw = enemy_.GetFacingYaw();
            assistTarget.x += std::sinf(enemyYaw) * rushLeadDistance_;
            assistTarget.z += std::cosf(enemyYaw) * rushLeadDistance_;

            assistStrength = rushActiveAssistStrength_;
            assistMaxStep = rushActiveAssistMaxStep_;
        } else if (isEnemyRushCharge) {
            assistStrength = rushChargeAssistStrength_;
            assistMaxStep = rushChargeAssistMaxStep_;
        } else if (isEnemyWarpStart) {
            assistStrength = warpStartAssistStrength_;
            assistMaxStep = warpStartAssistMaxStep_;
        } else if (isEnemyWarpMove) {
            // ワープ移動中は無理に振り回さない
            assistStrength = 0.0f;
            assistMaxStep = 0.0f;
        } else if (isEnemyWarpEnd) {
            assistStrength = warpEndAssistStrength_;
            assistMaxStep = warpEndAssistMaxStep_;
        } else if (isEnemyPhaseTransition) {
            assistStrength = lockOnAssistStrength_ * 1.35f;
            assistMaxStep = lockOnAssistMaxStep_ * 1.35f;
        }

        float dx = assistTarget.x - playerPos.x;
        float dz = assistTarget.z - playerPos.z;

        float targetYaw = std::atan2f(dx, dz);
        float diff = targetYaw - cameraYaw_;

        while (diff > 3.14159265f) {
            diff -= 6.28318530f;
        }
        while (diff < -3.14159265f) {
            diff += 6.28318530f;
        }

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

    // =========================
    // yaw / pitch から基準軸を作る
    // =========================
    float cosPitch = std::cosf(cameraPitch_);
    DirectX::XMFLOAT3 forward = {std::sinf(cameraYaw_) * cosPitch,
                                 std::sinf(cameraPitch_),
                                 std::cosf(cameraYaw_) * cosPitch};

    DirectX::XMFLOAT3 right = {std::cosf(cameraYaw_), 0.0f,
                               -std::sinf(cameraYaw_)};

    // =========================
    // カメラ基準点
    // =========================
    DirectX::XMFLOAT3 cameraTargetBase = {
        playerPos.x, playerPos.y + cameraLookHeight_, playerPos.z};

    DirectX::XMFLOAT3 cameraPos{};

    if (isLockOn_) {
        // ---------------------------------
        // ロックオン時: 敵とのライン基準で円弧追従
        // ---------------------------------
        float toEnemyX = enemyPos.x - playerPos.x;
        float toEnemyZ = enemyPos.z - playerPos.z;
        float distXZ = std::sqrt(toEnemyX * toEnemyX + toEnemyZ * toEnemyZ);

        if (distXZ < 0.0001f) {
            distXZ = 1.0f;
        }

        float invLen = 1.0f / distXZ;
        float lineX = toEnemyX * invLen;
        float lineZ = toEnemyZ * invLen;

        // 敵方向ラインに対する右ベクトル
        float orbitRightX = lineZ;
        float orbitRightZ = -lineX;

        // 敵との距離で少しだけ後ろに引く
        float pullT = 0.0f;
        {
            float minD = 3.0f;
            float maxD = 12.0f;
            float range = maxD - minD;
            if (range > 0.0001f) {
                pullT = (distXZ - minD) / range;
            }
            if (pullT < 0.0f) {
                pullT = 0.0f;
            }
            if (pullT > 1.0f) {
                pullT = 1.0f;
            }
        }

        float usedRadius = lockOnOrbitRadius_ + lockOnOrbitPullBackMax_ * pullT;
        if (isEnemyPhaseTransition) {
            usedRadius -= phaseTransitionPushIn_ * enemyPhaseTransitionRatio;
        }

        // cameraYaw_ と敵方向ラインとの差で、円弧上の左右位置を決める
        float lineYaw = std::atan2f(lineX, lineZ);
        float yawDiff = cameraYaw_ - lineYaw;

        while (yawDiff > 3.14159265f) {
            yawDiff -= 6.28318530f;
        }
        while (yawDiff < -3.14159265f) {
            yawDiff += 6.28318530f;
        }

        // 真横まで回りすぎると見づらいので制限
        const float maxOrbitAngle = 0.65f;
        if (yawDiff > maxOrbitAngle) {
            yawDiff = maxOrbitAngle;
        } else if (yawDiff < -maxOrbitAngle) {
            yawDiff = -maxOrbitAngle;
        }

        float sinA = std::sinf(yawDiff);
        float cosA = std::cosf(yawDiff);

        DirectX::XMFLOAT3 desiredCameraPos = {
            cameraTargetBase.x - lineX * usedRadius * cosA +
                orbitRightX * usedRadius * sinA +
                orbitRightX * lockOnOrbitSideBias_,
            cameraTargetBase.y + lockOnOrbitHeight_,
            cameraTargetBase.z - lineZ * usedRadius * cosA +
                orbitRightZ * usedRadius * sinA +
                orbitRightZ * lockOnOrbitSideBias_};

        float posAlpha = lockOnOrbitLerpSpeed_ * ctx_->deltaTime;
        if (posAlpha > 1.0f) {
            posAlpha = 1.0f;
        }

        lockOnOrbitCameraPos_.x +=
            (desiredCameraPos.x - lockOnOrbitCameraPos_.x) * posAlpha;
        lockOnOrbitCameraPos_.y +=
            (desiredCameraPos.y - lockOnOrbitCameraPos_.y) * posAlpha;
        lockOnOrbitCameraPos_.z +=
            (desiredCameraPos.z - lockOnOrbitCameraPos_.z) * posAlpha;

        cameraPos = lockOnOrbitCameraPos_;
    } else {
        // ---------------------------------
        // 通常時: 肩越し三人称
        // ---------------------------------
        cameraPos = {cameraTargetBase.x - forward.x * cameraDistance_ +
                         right.x * cameraSideOffset_,
                     cameraTargetBase.y + cameraHeight_ -
                         forward.y * cameraDistance_,
                     cameraTargetBase.z - forward.z * cameraDistance_ +
                         right.z * cameraSideOffset_};

        if (isEnemyPhaseTransition) {
            cameraPos.x += forward.x * phaseTransitionPushIn_ *
                           enemyPhaseTransitionRatio;
            cameraPos.y += 0.12f * enemyPhaseTransitionRatio;
            cameraPos.z += forward.z * phaseTransitionPushIn_ *
                           enemyPhaseTransitionRatio;
        }

        // 非ロック時は円弧用現在値を同期
        lockOnOrbitCameraPos_ = cameraPos;
    }

    // =========================
    // 注視点
    // =========================
    DirectX::XMFLOAT3 lookAt{};

    if (isLockOn_) {
        DirectX::XMFLOAT3 desiredLookAt = {
            playerPos.x * 0.35f + enemyPos.x * 0.65f,
            (playerPos.y + cameraLookHeight_) * 0.45f +
                (enemyPos.y + 1.2f) * 0.55f,
            playerPos.z * 0.35f + enemyPos.z * 0.65f};

        float lookAlpha = lockOnLookAtLerpSpeed_ * ctx_->deltaTime;
        if (lookAlpha > 1.0f) {
            lookAlpha = 1.0f;
        }

        lockOnLookAt_.x += (desiredLookAt.x - lockOnLookAt_.x) * lookAlpha;
        lockOnLookAt_.y += (desiredLookAt.y - lockOnLookAt_.y) * lookAlpha;
        lockOnLookAt_.z += (desiredLookAt.z - lockOnLookAt_.z) * lookAlpha;

        lookAt = lockOnLookAt_;
    } else {
        lookAt = {cameraTargetBase.x + forward.x * cameraLookAhead_,
                  cameraTargetBase.y + forward.y * cameraLookAhead_,
                  cameraTargetBase.z + forward.z * cameraLookAhead_};

        lockOnLookAt_ = lookAt;
    }

    if (isEnemyPhaseTransition) {
        DirectX::XMFLOAT3 transitionLookAt = {
            playerPos.x * (1.0f - phaseTransitionLookAtEnemyWeight_) +
                enemyPos.x * phaseTransitionLookAtEnemyWeight_,
            (playerPos.y + cameraLookHeight_) *
                    (1.0f - phaseTransitionLookAtEnemyWeight_) +
                (enemyPos.y + phaseTransitionLookAtHeight_) *
                    phaseTransitionLookAtEnemyWeight_,
            playerPos.z * (1.0f - phaseTransitionLookAtEnemyWeight_) +
                enemyPos.z * phaseTransitionLookAtEnemyWeight_};

        float blend = enemyPhaseTransitionRatio;
        lookAt.x += (transitionLookAt.x - lookAt.x) * blend;
        lookAt.y += (transitionLookAt.y - lookAt.y) * blend;
        lookAt.z += (transitionLookAt.z - lookAt.z) * blend;
    }

    camera_.SetPosition(cameraPos);
    camera_.LookAt(lookAt);
}
