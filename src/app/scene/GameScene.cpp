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
#endif // _DEBUG

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

    // Camera
    float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                   static_cast<float>(ctx_->winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.SetPosition(kCameraStartPos);
    camera_.UpdateMatrices();

#ifdef _DEBUG
    debugCamera_.Initialize(aspect);
    debugCamera_.SetPosition(kCameraStartPos);
    debugCamera_.UpdateMatrices();
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
}

void GameScene::Update() {
    Input *input = ctx_->input;

    UpdateCamera(input);

#ifdef _DEBUG
    if (currentCamera_ == &debugCamera_) {
        return;
    }
#endif

    player_.Update(input, ctx_->deltaTime);

    enemy_.Update(player_.GetTransform().position, ctx_->deltaTime);

    // ヒットクールダウンの更新
    if (enemyHitCooldown_ > 0.0f) {
        enemyHitCooldown_ -= ctx_->deltaTime;
        if (enemyHitCooldown_ < 0.0f) {
            enemyHitCooldown_ = 0.0f;
        }
    }

      // 毎フレームいったんリセット
    dbgHitLeftHand_ = false;
    dbgHitRightHand_ = false;
    dbgHitBody_ = false;

    // プレイヤーの攻撃判定とあたり判定
    if (player_.GetSword().IsSlashMode()) {
        auto swordBox = player_.GetSword().GetOBB();

        auto bodyBox = enemy_.GetBodyOBB();
        auto leftHandBox = enemy_.GetLeftHandOBB();
        auto rightHandBox = enemy_.GetRightHandOBB();

        bool hitLeftHand = CollisionUtil::CheckOBB(swordBox, leftHandBox);
        bool hitRightHand = CollisionUtil::CheckOBB(swordBox, rightHandBox);
        bool hitBody = CollisionUtil::CheckOBB(swordBox, bodyBox);

        dbgHitLeftHand_ = hitLeftHand;
        dbgHitRightHand_ = hitRightHand;
        dbgHitBody_ = hitBody;

        if (enemyHitCooldown_ <= 0.0f) {
            if (hitBody) {
                enemy_.TakeDamage(10.0f);
                enemyHitCooldown_ = 0.2f; // 0.2秒だけ再ヒット禁止
            }
        }
    }

    // ボスの攻撃判定とあたり判定
    bool bossHitPlayer = false;

    if (enemy_.IsAttackActive()) {
        auto enemyAttackBox = enemy_.GetAttackOBB();
        auto playerBox = player_.GetOBB();

        bossHitPlayer = CollisionUtil::CheckOBB(enemyAttackBox, playerBox);
        dbgBossHitPlayer_ = bossHitPlayer;
        if (bossHitPlayer) {
            // いったん確認用。あとでプレイヤーHP処理に置き換える
        }
    }

    // 敵の弾とプレイヤーのあたり判定
    dbgBulletHitPlayer_ = false;

    auto playerBox = player_.GetOBB();

    for (const auto &bullet : enemy_.GetBullets()) {
        if (!bullet.isAlive) {
            continue;
        }

        OBB bulletBox{};
        bulletBox.center = bullet.position;
        bulletBox.size = {0.4f, 0.4f, 0.4f};
        bulletBox.rotation = player_.GetTransform().rotation;

        if (CollisionUtil::CheckOBB(bulletBox, playerBox)) {
            dbgBulletHitPlayer_ = true;
            break;
        }
    }
}

void GameScene::Draw() {
    ctx_->model->PreDraw();

    player_.Draw(ctx_->model, camera_);
    enemy_.Draw(ctx_->model, camera_);
    int aliveBulletCount = 0;
    for (const auto& bullet : enemy_.GetBullets()) {
        if (bullet.isAlive) {
            aliveBulletCount++;
        }
    }
#ifdef _DEBUG
    // 当たり判定描画
    ctx_->debugDraw->DrawOBB(ctx_->model, player_.GetSword().GetOBB(), camera_);

    // ボス部位
    if (enemy_.IsAlive()) {
        ctx_->debugDraw->DrawOBB(ctx_->model, enemy_.GetBodyOBB(), camera_);
        ctx_->debugDraw->DrawOBB(ctx_->model, enemy_.GetLeftHandOBB(), camera_);
        ctx_->debugDraw->DrawOBB(ctx_->model, enemy_.GetRightHandOBB(),
            camera_);

        if (enemy_.IsAttackActive()) {
            ctx_->debugDraw->DrawOBB(ctx_->model, enemy_.GetAttackOBB(),
                camera_);
        }
    }
#endif // _DEBUG

    ctx_->model->PostDraw();

#ifdef _DEBUG
    ImGui::Begin("HitInfo");
    ImGui::Text("Hit LeftHand : %s", dbgHitLeftHand_ ? "true" : "false");
    ImGui::Text("Hit RightHand: %s", dbgHitRightHand_ ? "true" : "false");
    ImGui::Text("Hit Body     : %s", dbgHitBody_ ? "true" : "false");
    ImGui::Text("Cooldown     : %.2f", enemyHitCooldown_);

    const char* stateName = "Unknown";
    switch (enemy_.GetState()) {
    case EnemyState::Idle:
        stateName = "Idle";
        break;
    case EnemyState::SmashCharge:
        stateName = "SmashCharge";
        break;
    case EnemyState::SmashAttack:
        stateName = "SmashAttack";
        break;
    case EnemyState::SmashRecovery:
        stateName = "SmashRecovery";
        break;
    case EnemyState::SweepCharge:
        stateName = "SweepCharge";
        break;
    case EnemyState::SweepAttack:
        stateName = "SweepAttack";
        break;
    case EnemyState::SweepRecovery:
        stateName = "SweepRecovery";
        break;
    case EnemyState::ShotCharge:
        stateName = "ShotCharge";
        break;
    case EnemyState::ShotFire:
        stateName = "ShotFire";
        break;
    case EnemyState::ShotRecovery:
        stateName = "ShotRecovery";
        break;
    case EnemyState::WarpStart:
        stateName = "WarpStart";
        break;
    case EnemyState::WarpMove:
        stateName = "WarpMove";
        break;
    case EnemyState::WarpEnd:
        stateName = "WarpEnd";
        break;
    }

    ImGui::Text("EnemyState   : %s", stateName);
    ImGui::Text("AttackActive : %s",
        enemy_.IsAttackActive() ? "true" : "false");
    ImGui::Text("BossHitPlayer: %s", dbgBossHitPlayer_ ? "true" : "false");
    ImGui::Text("DistanceToPlayer : %.2f", enemy_.GetDistanceToPlayer());
    ImGui::Text("FacingYaw       : %.2f", enemy_.GetFacingYaw());
    ImGui::Text("LockedAttackYaw : %.2f", enemy_.GetLockedAttackYaw());
    ImGui::Text("BulletHitPlayer : %s", dbgBulletHitPlayer_ ? "true" : "false");
    ImGui::Text("AliveBullets    : %d", aliveBulletCount);
    auto warpPos = enemy_.GetWarpTargetPos();
    ImGui::Text("Visible         : %s", enemy_.IsVisible() ? "true" : "false");
    ImGui::Text("WarpTarget      : (%.2f, %.2f, %.2f)", warpPos.x, warpPos.y, warpPos.z);
    ImGui::End();
#endif
}

void GameScene::UpdateCamera(Input *input) {
#ifdef _DEBUG
    if (input->IsKeyTrigger(DIK_F11)) {
        WinApp *win = ctx_->winApp;

        if (currentCamera_ == &camera_) {
            currentCamera_ = &debugCamera_;

            debugCamera_.SetPosition(camera_.GetPosition());
            debugCamera_.SetRotation(camera_.GetRotation());

            while (ShowCursor(FALSE) >= 0)
                ;
        } else {
            currentCamera_ = &camera_;

            while (ShowCursor(TRUE) < 0)
                ;

            int width = win->GetWidth();
            int height = win->GetHeight();
            HWND hwnd = win->GetHwnd();

            POINT center{width / 2, height / 2};
            ClientToScreen(hwnd, &center);
            SetCursorPos(center.x, center.y);
        }
    }

    if (currentCamera_ == &debugCamera_) {
        debugCamera_.Update(*input, ctx_->deltaTime);
    }
#else
    (void)input;
#endif // _DEBUG

    currentCamera_->UpdateMatrices();
}