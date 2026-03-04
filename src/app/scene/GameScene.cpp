#include "GameScene.h"
#include "Input.h"
#include "ModelManager.h"
#include "WinApp.h"
#include <DirectXMath.h>

using namespace DirectX;

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

    // =============================
    // Camera
    // =============================
    float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                   static_cast<float>(ctx_->winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.SetPosition({0.0f, 1.0f, 0.0f});
    camera_.SetRotation({0.0f, 0.0f, 0.0f});
    camera_.Update();

    // =============================
    // Sword
    // =============================
    swordModelId_ = ctx_->model->Load(L"resources/model/sword/sword.obj");

    swordTf_.position = {0.0f, 0.0f, 3.0f};
    swordTf_.scale = {1.0f, 1.0f, 1.0f};
    swordTf_.rotation = {0, 0, 0, 1};
}

void GameScene::Update() {

    camera_.Update();

    // =============================
    // 剣の姿勢（ジャイロ）
    // =============================
    XMVECTOR q = ctx_->input->GetOrientation();
    q = XMQuaternionConjugate(q);
    XMStoreFloat4(&swordTf_.rotation, q);
}

void GameScene::Draw() {

    ctx_->model->PreDraw();

    // 剣描画
    ctx_->model->Draw(swordModelId_, swordTf_, camera_);

    ctx_->model->PostDraw();
}