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

    // =========================
    // プレイヤー移動（そのまま）
    // =========================

    XMVECTOR forward = XMVectorSet(0, 0, 1, 0);
    XMVECTOR right = XMVectorSet(1, 0, 0, 0);

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

    // =========================
    // カメラ固定（プレイヤー追従のみ）
    // =========================

    XMFLOAT3 camPos = playerTf_.position;
    camPos.y += 1.6f;
    camPos.z += 1.6f;

    camera_.SetPosition(camPos);
    camera_.SetRotation({0, 0, 0}); // 回転なし
    camera_.Update();

    // =========================
    // 剣をコントローラー姿勢にする
    // =========================

    XMVECTOR controllerQ = ctx_->input->GetOrientation();

    // 剣位置（カメラ右下）
    XMVECTOR camPosVec = XMLoadFloat3(&camPos);
    XMVECTOR forwardVec = XMVectorSet(0, 0, 1, 0);
    XMVECTOR rightVec = XMVectorSet(1, 0, 0, 0);
    XMVECTOR upVec = XMVectorSet(0, 1, 0, 0);

    XMVECTOR offset = rightVec * 0.4f + upVec * -0.3f + forwardVec * 0.8f;

    XMVECTOR swordPos = camPosVec + offset;
    XMStoreFloat3(&swordTf_.position, swordPos);

    // ★ クォータニオンをオイラーに変換してTransformへ
    XMFLOAT4 qf;
    XMStoreFloat4(&qf, controllerQ);

    // 簡易変換（Yaw/Pitch/Roll）
    XMMATRIX rotM = XMMatrixRotationQuaternion(controllerQ);

    float pitch = asinf(-rotM.r[2].m128_f32[1]);
    float yaw = atan2f(rotM.r[2].m128_f32[0], rotM.r[2].m128_f32[2]);
    float roll = atan2f(rotM.r[0].m128_f32[1], rotM.r[1].m128_f32[1]);

    swordTf_.rotation = {pitch, yaw, roll};
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