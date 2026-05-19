#include "SceneManager.h"
#include "BaseScene.h"
#include "DirectXCommon.h"

void SceneManager::Initialize(const SceneContext &ctx) { ctx_ = &ctx; }

void SceneManager::ChangeScene(std::unique_ptr<BaseScene> nextScene) {
    if (isUpdating_ || isDrawing_) {
        pendingScene_ = std::move(nextScene);
        return;
    }

    ApplySceneChange(std::move(nextScene));
}

void SceneManager::ApplySceneChange(std::unique_ptr<BaseScene> nextScene) {
    if (ctx_ != nullptr && ctx_->dxCommon != nullptr && currentScene_) {
        ctx_->dxCommon->WaitForGpu();
    }

    currentScene_.reset();

    currentScene_ = std::move(nextScene);
    currentScene_->SetSceneManager(this);
    currentScene_->Initialize(*ctx_);
}

void SceneManager::Update() {
    if (pendingScene_) {
        ApplySceneChange(std::move(pendingScene_));
    }

    if (currentScene_) {
        isUpdating_ = true;
        currentScene_->Update();
        isUpdating_ = false;
    }

    if (pendingScene_) {
        ApplySceneChange(std::move(pendingScene_));
    }
}

void SceneManager::Draw() {
    if (currentScene_) {
        isDrawing_ = true;
        currentScene_->Draw();
        isDrawing_ = false;
    }
}

void SceneManager::DrawOverlay() {
    if (currentScene_) {
        isDrawing_ = true;
        currentScene_->DrawOverlay();
        isDrawing_ = false;
    }
}
