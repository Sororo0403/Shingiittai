#include "GameScene.h"
#include "Input.h"
#include "ModelManager.h"
#include "ParticleEmitter.h"
#include "ParticleManager.h"
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

    // =============================
    // ParticleEmitter作成
    // =============================
    uint32_t particleModelId =
        ctx_->model->Load(L"resources/model/particle/particle.obj");

    ctx_->particle->CreateEmitter("slash", particleModelId);

    // エミッタ取得
    ParticleEmitter *emitter = ctx_->particle->GetEmitter("slash");

    // =============================
    // ParticleParams 設定
    // =============================
    ParticleParams params;

    params.emissionRate = 40.0f;
    params.lifeTime = 0.4f;
    params.speed = 4.0f;
    params.startScale = 0.3f;
    params.baseDirection = {0, 0, 1};
    params.startColor = {1, 0.6f, 0.2f, 1};

    emitter->SetParams(params);
}

void GameScene::Update() {

    camera_.Update();

    // =============================
    // 剣の姿勢更新
    // =============================
    XMVECTOR q = ctx_->input->GetOrientation();
    q = XMQuaternionConjugate(q);
    XMStoreFloat4(&swordTf_.rotation, q);

    float dt = ctx_->deltaTime;

    // =============================
    // エミッタを剣に追従
    // =============================
    ctx_->particle->GetEmitter("slash")->SetTransform(swordTf_);

    // =============================
    // Particle更新
    // =============================
    ctx_->particle->Update(dt);
}

void GameScene::Draw() {

    ctx_->model->PreDraw();

    ctx_->model->Draw(swordModelId_, swordTf_, camera_);

    ctx_->particle->Draw(camera_);

    ctx_->model->PostDraw();
}