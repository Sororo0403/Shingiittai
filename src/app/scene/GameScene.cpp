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
    camera_.SetPosition(kCameraStartPos);
    camera_.UpdateMatrices();

#ifdef _DEBUG
    debugCamera_.Initialize(aspect);
    debugCamera_.SetPosition(kCameraStartPos);
    debugCamera_.UpdateMatrices();
#endif

    currentCamera_ = &camera_;

    DirectXCommon *dx = ctx_->dxCommon;
    ModelManager *model = ctx_->model;
    TextureManager *texture = ctx_->texture;

    dx->BeginUpload();

    uint32_t playerModel = model->Load(L"resources/model/player/player.glb");
    uint32_t swordModel = model->Load(L"resources/model/player/sword.glb");

    uint32_t enemyModel = model->Load(L"resources/model/enemy/enemy.glb");

    dx->EndUpload();

    texture->ReleaseUploadBuffers();

    player_.Initialize(playerModel, swordModel);
    enemy_.Initialize(enemyModel);
}

void GameScene::Update() {
    Input *input = ctx_->input;

    UpdateCamera(input);

#ifdef _DEBUG
    if (currentCamera_ == &debugCamera_) {
        return;
    }
#endif

    player_.Update(input, ctx_->deltaTime);

    enemy_.Update();

    // 当たり判定
    auto swordBox = player_.GetSword().GetOBB();
    auto enemyBox = enemy_.GetOBB();

    if (CollisionUtil::CheckOBB(swordBox, enemyBox)) {
        enemy_.TakeDamage(100);
    }
}

void GameScene::Draw() {
    ModelManager *model = ctx_->model;

#ifdef _DEBUG
    DebugDraw *debugDraw = ctx_->debugDraw;
#endif // _DEBUG

    model->PreDraw();

    player_.Draw(model, *currentCamera_);
    enemy_.Draw(model, *currentCamera_);

#ifdef _DEBUG
    // 当たり判定描画
    debugDraw->DrawOBB(model, player_.GetSword().GetOBB(), *currentCamera_);

    if (enemy_.IsAlive()) {
        debugDraw->DrawOBB(model, enemy_.GetOBB(), *currentCamera_);
    }
#endif // _DEBUG

    model->PostDraw();
}

void GameScene::UpdateCamera(Input *input) {
#ifdef _DEBUG
    if (input->IsKeyTrigger(DIK_F11)) {
        WinApp *win = ctx_->winApp;

        if (currentCamera_ == &camera_) {
            currentCamera_ = &debugCamera_;

            debugCamera_.SetPosition(camera_.GetPosition());
            debugCamera_.SetRotation(camera_.GetRotation());

            while (ShowCursor(FALSE) >= 0)
                ;
        } else {
            currentCamera_ = &camera_;

            while (ShowCursor(TRUE) < 0)
                ;

            int width = win->GetWidth();
            int height = win->GetHeight();
            HWND hwnd = win->GetHwnd();

            POINT center{width / 2, height / 2};
            ClientToScreen(hwnd, &center);
            SetCursorPos(center.x, center.y);
        }
    }

    if (currentCamera_ == &debugCamera_) {
        debugCamera_.Update(*input, ctx_->deltaTime);
    }
#else
    (void)input;
#endif // _DEBUG

    currentCamera_->UpdateMatrices();
}