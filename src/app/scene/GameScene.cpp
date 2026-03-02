#include "GameScene.h"
#include "Input.h"
#include "ModelManager.h"
#include "WinApp.h"
#include <DirectXMath.h>

using namespace DirectX;

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

    // =============================
    // Camera 初期化
    // =============================
    float aspect = static_cast<float>(ctx_->winApp->GetWidth()) /
                   static_cast<float>(ctx_->winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.SetPosition({0.0f, 1.0f, 0.0f});
    camera_.SetRotation({0.0f, 0.0f, 0.0f});
    camera_.Update();

    // =============================
    // Sword 初期化
    // =============================
    swordModelId_ = ctx.model->Load(L"resources/model/sword/sword.obj");

    swordTf_.position = {0.0f, 0.0f, 3.0f};
    swordTf_.scale = {1.0f, 1.0f, 1.0f};
    swordTf_.rotation = {0.0f, 0.0f, 0.0f, 1.0f};

    // =============================
    // Particle 初期化
    // =============================
    particleModelId_ =
        ctx.model->Load(L"resources/model/particle/particle.obj");

    particle_.Initialize(ctx.model, particleModelId_);

    emitTimer_ = 0.0f;
}

void GameScene::Update() {
    camera_.Update();

    // =============================
    // 剣の姿勢更新（ジャイロ）
    // =============================
    XMVECTOR q = ctx_->input->GetOrientation();
    q = XMQuaternionConjugate(q);
    XMStoreFloat4(&swordTf_.rotation, q);

    const float dt = 1.0f / 60.0f;

    // =============================
    // Particle 更新
    // =============================
    particle_.Update(dt);

    // =============================
    // 剣の向き取得
    // =============================
    XMVECTOR swordQ = XMLoadFloat4(&swordTf_.rotation);
    XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), swordQ);

    XMFLOAT3 f;
    XMStoreFloat3(&f, forward);

    // 剣の先端位置
    XMFLOAT3 emitPos = swordTf_.position;
    const float tipOffset = 1.0f;

    emitPos.x += f.x * tipOffset;
    emitPos.y += f.y * tipOffset;
    emitPos.z += f.z * tipOffset;

    // =============================
    // 一定間隔でEmit
    // =============================
    emitTimer_ += dt;
    if (emitTimer_ >= emitInterval_) {
        emitTimer_ = 0.0f;
        particle_.Emit(emitPos, f);
    }
}

void GameScene::Draw() {
    ctx_->model->PreDraw();

    // Sword描画
    ctx_->model->Draw(swordModelId_, swordTf_, camera_);

    // Particle描画
    particle_.Draw(camera_);

    ctx_->model->PostDraw();
}