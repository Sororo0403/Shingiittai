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

    // モデルロード
    enemyModelId_ = ctx.model->Load(L"resources/model/enemy/enemy.obj");
    playerModelId_ = ctx.model->Load(L"resources/model/player/player.obj");
    swordModelId_ = ctx.model->Load(L"resources/model/sword/sword.obj");

    // プレイヤー初期化
    playerTf_.position = {0, 0, 0};
    playerTf_.scale = {1.5f, 1.5f, 1.5f};

    // 剣スケール
    swordTf_.scale = {0.5f, 0.5f, 0.5f};

    // 敵配置
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

    // ===== ジャイロ回転 =====
    float gyroX = ctx_->input->GetGyroX();
    float gyroY = ctx_->input->GetGyroY();

    cameraRot_.y += gyroY * gyroSensitivity_ * dt;
    cameraRot_.x -= gyroX * gyroSensitivity_ * dt;

    cameraRot_.x =
        std::clamp(cameraRot_.x, -XM_PIDIV2 + 0.1f, XM_PIDIV2 - 0.1f);

    // ===== 移動方向ベクトル =====
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

    // ===== カメラを頭位置へ =====
    XMFLOAT3 camPos = playerTf_.position;
    camPos.y += 1.6f;
    camPos.z += 1.6f;

    camera_.SetPosition(camPos);
    camera_.SetRotation(cameraRot_);
    camera_.Update();

    // ===== 剣をカメラ右下へ =====
    XMVECTOR camPosVec = XMLoadFloat3(&camPos);
    XMVECTOR up = XMVectorSet(0, 1, 0, 0);

    XMVECTOR offset = right * 0.4f + up * -0.3f + forward * 0.8f;

    XMVECTOR swordPos = camPosVec + offset;

    XMStoreFloat3(&swordTf_.position, swordPos);
    swordTf_.rotation = cameraRot_;
}

void GameScene::Draw() {

    ctx_->model->PreDraw();

    // 敵描画
    for (auto &enemy : enemies_) {
        ctx_->model->Draw(enemyModelId_, enemy, camera_);
    }

    ctx_->model->Draw(playerModelId_, playerTf_, camera_);

    // 剣描画（最後）
    ctx_->model->Draw(swordModelId_, swordTf_, camera_);

    ctx_->model->PostDraw();
}