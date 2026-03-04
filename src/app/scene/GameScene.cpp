#include "GameScene.h"
#include "Input.h"
#include "ModelManager.h"
#include "WinApp.h"

void GameScene::Initialize(const SceneContext &ctx) {

    BaseScene::Initialize(ctx);

    float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                   static_cast<float>(ctx_->winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.SetPosition({0.0f, 1.0f, 0.0f});
    camera_.SetRotation({0.0f, 0.0f, 0.0f});
    camera_.Update();

    // モデルロード
    uint32_t playerModel =
        ctx_->model->Load(L"resources/model/player/player.obj");

    uint32_t swordModel = ctx_->model->Load(L"resources/model/sword/sword.obj");

    player_.Initialize(playerModel, swordModel);
}

void GameScene::Update() {

    camera_.Update();

    player_.Update(ctx_->input);
}

void GameScene::Draw() {

    ctx_->model->PreDraw();

    player_.Draw(ctx_->model, camera_);

    ctx_->model->PostDraw();
}