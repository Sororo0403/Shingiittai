#include "scene/SceneManager.h"
#include "graphics/DirectXCommon.h"
#include "scene/BaseScene.h"
#include "texture/TextureManager.h"
#include <stdexcept>

namespace {

class UploadPassScope {
  public:
    UploadPassScope(DirectXCommon *dxCommon, TextureManager *textureManager,
                    bool active)
        : dxCommon_(dxCommon), textureManager_(textureManager), active_(active) {}

    ~UploadPassScope() {
        if (active_ && dxCommon_ != nullptr) {
            dxCommon_->AbortFrame();
            if (textureManager_ != nullptr) {
                textureManager_->ReleaseUploadBuffers();
            }
        }
    }

    void Finish() {
        if (!active_) {
            return;
        }
        dxCommon_->EndUpload();
        if (textureManager_ != nullptr) {
            textureManager_->ReleaseUploadBuffers();
        }
        active_ = false;
    }

  private:
    DirectXCommon *dxCommon_ = nullptr;
    TextureManager *textureManager_ = nullptr;
    bool active_ = false;
};

class BoolFlagScope {
  public:
    explicit BoolFlagScope(bool &flag) : flag_(flag), previous_(flag) {
        flag_ = true;
    }
    ~BoolFlagScope() { flag_ = previous_; }

    BoolFlagScope(const BoolFlagScope &) = delete;
    BoolFlagScope &operator=(const BoolFlagScope &) = delete;

  private:
    bool &flag_;
    bool previous_ = false;
};

} // namespace

void SceneManager::Initialize(const SceneContext &ctx) { ctx_ = &ctx; }

void SceneManager::Finalize() {
    if (ctx_ != nullptr && ctx_->rendering.dxCommon != nullptr) {
        ctx_->rendering.dxCommon->WaitForGpuIfPossible();
    }
    pendingScene_.reset();
    currentScene_.reset();
    isUpdating_ = false;
    isDrawing_ = false;
}

void SceneManager::SetSceneFactory(AbstractSceneFactory *sceneFactory) {
    sceneFactory_ = sceneFactory;
}

void SceneManager::ChangeScene(const std::string &sceneName) {
    if (!sceneFactory_) {
        throw std::runtime_error(
            "SceneManager::ChangeScene requires a scene factory: " +
            sceneName);
    }

    std::unique_ptr<BaseScene> nextScene =
        sceneFactory_->CreateScene(sceneName);
    if (!nextScene) {
        throw std::runtime_error(
            "Scene factory returned null scene: " + sceneName);
    }

    ChangeScene(std::move(nextScene));
}

void SceneManager::ChangeScene(std::unique_ptr<BaseScene> nextScene) {
    if (isUpdating_ || isDrawing_) {
        pendingScene_ = std::move(nextScene);
        return;
    }

    ApplySceneChange(std::move(nextScene));
}

void SceneManager::ApplySceneChange(std::unique_ptr<BaseScene> nextScene) {
    if (!ctx_) {
        throw std::runtime_error(
            "SceneManager::ApplySceneChange called before Initialize");
    }
    if (!nextScene) {
        throw std::runtime_error("SceneManager received null scene");
    }

    DirectXCommon *dxCommon = ctx_->rendering.dxCommon;
    TextureManager *textureManager = ctx_->rendering.texture;

    if (dxCommon != nullptr) {
        dxCommon->WaitForGpu();
    }

    nextScene->SetSceneManager(this);

    const bool ownsUploadPass =
        dxCommon != nullptr && !dxCommon->IsCommandListRecording();
    if (ownsUploadPass) {
        dxCommon->BeginUpload();
    }

    UploadPassScope uploadPass(dxCommon, textureManager, ownsUploadPass);
    nextScene->Initialize(*ctx_);

    uploadPass.Finish();

    currentScene_.reset();
    currentScene_ = std::move(nextScene);
}

void SceneManager::Update() {
    if (pendingScene_) {
        ApplySceneChange(std::move(pendingScene_));
    }

    if (currentScene_) {
        BoolFlagScope updating(isUpdating_);
        currentScene_->Update();
    }

    if (pendingScene_) {
        ApplySceneChange(std::move(pendingScene_));
    }
}

void SceneManager::Draw() {
    if (currentScene_) {
        BoolFlagScope drawing(isDrawing_);
        currentScene_->Draw();
    }
}

bool SceneManager::UsesForeground3DPass() const {
    return currentScene_ && currentScene_->UsesForeground3DPass();
}

void SceneManager::DrawForeground3D() {
    if (currentScene_) {
        BoolFlagScope drawing(isDrawing_);
        currentScene_->DrawForeground3D();
    }
}

void SceneManager::DrawTransparent() {
    if (currentScene_) {
        BoolFlagScope drawing(isDrawing_);
        currentScene_->DrawTransparent();
    }
}

void SceneManager::DrawPostProcessOverlay() {
    if (currentScene_) {
        BoolFlagScope drawing(isDrawing_);
        currentScene_->DrawPostProcessOverlay();
    }
}

void SceneManager::DrawShadow() {
    if (currentScene_) {
        BoolFlagScope drawing(isDrawing_);
        currentScene_->DrawShadow();
    }
}

