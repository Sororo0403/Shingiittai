#include "GameScene.h"
#include "CollisionUtil.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "ModelManager.h"
#include "TextureManager.h"
#include "WinApp.h"

#ifdef _DEBUG
#include "DebugDraw.h"
#endif // _DEBUG

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

    // Camera
    float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                   static_cast<float>(ctx_->winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.SetPosition({0.0f, 1.0f, 0.0f});
    camera_.SetRotation({0.0f, 0.0f, 0.0f});
    camera_.Update();

    ctx_->dxCommon->BeginUpload();

    // Player
    uint32_t playerModel =
        ctx_->model->Load(L"resources/model/player/player.glb");
    uint32_t swordModel = ctx_->model->Load(L"resources/model/sword/sword.glb");

    // Enemy
    uint32_t enemyModel = ctx_->model->Load(L"resources/model/enemy/enemy.glb");

    // Model
    modelId_ = ctx_->model->Load(L"resources/model/debug/anime.glb");

    ctx_->dxCommon->EndUpload();

    ctx_->texture->ReleaseUploadBuffers();

    player_.Initialize(playerModel, swordModel);
    enemy_.Initialize(enemyModel);

    modelTf_.position = {0.0f, 0.0f, 5.0f};
}

void GameScene::Update() {
    camera_.Update();

    player_.Update(ctx_->input, ctx_->deltaTime);

    enemy_.Update();

    // 当たり判定
    auto swordBox = player_.GetSword().GetOBB();
    auto enemyBox = enemy_.GetOBB();

    if (CollisionUtil::CheckOBB(swordBox, enemyBox)) {
        enemy_.TakeDamage(100);
    }
}

void GameScene::Draw() {
    ctx_->model->PreDraw();

    player_.Draw(ctx_->model, camera_);
    enemy_.Draw(ctx_->model, camera_);

    ctx_->model->Draw(modelId_, modelTf_, camera_);

#ifdef _DEBUG
    // 当たり判定描画
    ctx_->debugDraw->DrawOBB(ctx_->model, player_.GetSword().GetOBB(), camera_);

    if (enemy_.IsAlive()) {
        ctx_->debugDraw->DrawOBB(ctx_->model, enemy_.GetOBB(), camera_);
    }
#endif // _DEBUG

    ctx_->model->PostDraw();
}