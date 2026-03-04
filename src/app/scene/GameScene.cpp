#include "GameScene.h"
#include "CollisionUtil.h"
#include "Input.h"
#include "ModelManager.h"
#include "WinApp.h"
#include "imgui.h"

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

    uint32_t enemyModel = ctx_->model->Load(L"resources/model/enemy/enemy.obj");

    player_.Initialize(playerModel, swordModel);

    enemy_.Initialize(enemyModel);
}

void GameScene::Update() {
    camera_.Update();

    player_.Update(ctx_->input, ctx_->deltaTime);

    enemy_.Update();

    auto swordPos = player_.GetSword().GetTransform().position;
    auto enemyPos = enemy_.GetTransform().position;

    if (CollisionUtil::CheckSphere(swordPos, enemyPos, 2.0f)) {
        enemy_.TakeDamage(100.0f);
    }
}

void GameScene::Draw() {
    ctx_->model->PreDraw();

    player_.Draw(ctx_->model, camera_);

    enemy_.Draw(ctx_->model, camera_);

    ctx_->model->PostDraw();

    ImGui::Begin("Camera");

    auto pos = camera_.GetPosition();
    auto rot = camera_.GetRotation();

    float p[3] = {pos.x, pos.y, pos.z};
    float r[3] = {rot.x, rot.y, rot.z};

    if (ImGui::DragFloat3("Position", p, 0.1f)) {
        camera_.SetPosition({p[0], p[1], p[2]});
    }

    if (ImGui::DragFloat3("Rotation", r, 0.01f)) {
        camera_.SetRotation({r[0], r[1], r[2]});
    }

    ImGui::End();
}