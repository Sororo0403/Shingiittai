#include "GameScene.h"
#include "CollisionUtil.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "WinApp.h"

#ifdef _DEBUG
#include "DebugDraw.h"
#endif

using namespace DirectX;

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

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

    // カメラ切り替え処理
    UpdateCamera(input);

#ifdef _DEBUG
    if (currentCamera_ == &debugCamera_) {
        return;
    }
#endif

    player_.Update(input, ctx_->deltaTime);
    enemy_.Update();

    // 戦闘カメラ更新
    UpdateBattleCamera();

    // 当たり判定
    auto swordBox = player_.GetSword().GetOBB();
    auto enemyBox = enemy_.GetOBB();

    if (CollisionUtil::CheckOBB(swordBox, enemyBox)) {
        enemy_.TakeDamage(100);
    }
}

void GameScene::Draw() {
    ModelManager *model = ctx_->model;

#ifdef _DEBUG
    DebugDraw *debugDraw = ctx_->debugDraw;
#endif

    model->PreDraw();

    player_.Draw(model, *currentCamera_);
    enemy_.Draw(model, *currentCamera_);

#ifdef _DEBUG
    debugDraw->DrawOBB(model, player_.GetSword().GetOBB(), *currentCamera_);

    if (enemy_.IsAlive()) {
        debugDraw->DrawOBB(model, enemy_.GetOBB(), *currentCamera_);
    }
#endif

    model->PostDraw();
}

void GameScene::UpdateCamera(Input *input) {
#ifdef _DEBUG
    if (input->IsKeyTrigger(DIK_F11)) {
        if (currentCamera_ == &camera_) {
            currentCamera_ = &debugCamera_;
        } else {
            currentCamera_ = &camera_;
        }
    }

    if (currentCamera_ == &debugCamera_) {
        debugCamera_.Update(*input, ctx_->deltaTime);
        currentCamera_->UpdateMatrices();
        return;
    }
#else
    (void)input;
#endif
}

void GameScene::UpdateBattleCamera() {
    auto &playerTf = player_.GetTransform();
    auto &enemyTf = enemy_.GetTransform();

    XMFLOAT3 playerPos = playerTf.position;
    XMFLOAT3 enemyPos = enemyTf.position;

    // プレイヤーを敵に向ける
    player_.LookAt(enemyPos);

    // forward
    XMVECTOR playerPosV = XMLoadFloat3(&playerPos);
    XMVECTOR enemyPosV = XMLoadFloat3(&enemyPos);

    XMVECTOR forward = XMVector3Normalize(enemyPosV - playerPosV);

    // カメラ位置
    XMVECTOR camPos = playerPosV - forward * 3.5f + XMVectorSet(0, 1.2f, 0, 0);

    XMFLOAT3 cameraPos;
    XMStoreFloat3(&cameraPos, camPos);

    camera_.SetPosition(cameraPos);
    camera_.LookAt(enemyPos);
}
