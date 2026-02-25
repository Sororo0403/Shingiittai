#include "GameScene.h"
#include "Input.h"
#include "ModelManager.h"
#include "WinApp.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

using namespace DirectX;

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

    // モデル読み込み
    enemyModelId_ = ctx.model->Load(L"resources/model/enemy/enemy.obj");

    // プレイヤー初期位置
    playerTf_.position = {0, 0, 0};

    // 敵を大量に奥へ配置
    for (int z = 10; z < 100; z += 10) {
        for (int x = -20; x <= 20; x += 10) {
            Transform tf;
            tf.position = {(float)x, 0.0f, (float)z};
            tf.scale = {1.5f, 1.5f, 1.5f};
            enemies_.push_back(tf);
        }
    }

    float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                   static_cast<float>(ctx_->winApp->GetHeight());

    camera_.Initialize(aspect);
}

void GameScene::Update() {

    float dt = 1.0f / 60.0f;

    // ======================
    // マウス回転
    // ======================
    LONG mouseX = ctx_->input->GetMouseMoveX();
    LONG mouseY = ctx_->input->GetMouseMoveY();

    float sensitivity = 0.0025f;

    cameraRot_.y += mouseX * sensitivity;
    cameraRot_.x += mouseY * sensitivity;

    cameraRot_.x =
        std::clamp(cameraRot_.x, -XM_PIDIV2 + 0.1f, XM_PIDIV2 - 0.1f);

    // ======================
    // WASD移動
    // ======================
    XMVECTOR forward =
        XMVectorSet(sinf(cameraRot_.y), 0, cosf(cameraRot_.y), 0);

    XMVECTOR right = XMVectorSet(cosf(cameraRot_.y), 0, -sinf(cameraRot_.y), 0);

    XMVECTOR move = XMVectorZero();

    if (ctx_->input->IsKeyPress(DIK_W))
        move += forward;
    if (ctx_->input->IsKeyPress(DIK_S))
        move -= forward;
    if (ctx_->input->IsKeyPress(DIK_A))
        move -= right;
    if (ctx_->input->IsKeyPress(DIK_D))
        move += right;

    if (!XMVector3Equal(move, XMVectorZero())) {
        move = XMVector3Normalize(move);
        move *= moveSpeed_ * dt;
        XMStoreFloat3(&playerTf_.position,
                      XMLoadFloat3(&playerTf_.position) + move);
    }

    // ======================
    // カメラ追従
    // ======================
    XMFLOAT3 camPos = playerTf_.position;
    camPos.y += 1.6f;

    camera_.SetPosition(camPos);
    camera_.SetRotation(cameraRot_);
    camera_.Update();
}

void GameScene::Draw() {

    ctx_->model->PreDraw();

    for (auto &enemy : enemies_) {
        ctx_->model->Draw(enemyModelId_, enemy, camera_);
    }

    ctx_->model->PostDraw();
}