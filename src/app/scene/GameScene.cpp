#include "GameScene.h"
#include "Input.h"
#include "ModelManager.h"
#include "WinApp.h"
#include <DirectXMath.h>

using namespace DirectX;

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

    float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                   static_cast<float>(ctx_->winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.SetPosition({0.0f, 0.0f, -5.0f});
    camera_.SetRotation({0.0f, 0.0f, 0.0f});
    camera_.Update();

    swordModelId_ = ctx.model->Load(L"resources/model/sword/sword.obj");

    swordTf_.position = {0.0f, 0.0f, 3.0f};
    swordTf_.scale = {1.0f, 1.0f, 1.0f};
    swordTf_.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
}

void GameScene::Update() {
    camera_.Update();

    XMVECTOR q = ctx_->input->GetOrientation();
    q = XMQuaternionConjugate(q);

    XMStoreFloat4(&swordTf_.rotation, q);
}

void GameScene::Draw() {
    ctx_->model->PreDraw();
    ctx_->model->Draw(swordModelId_, swordTf_, camera_);
    ctx_->model->PostDraw();
}