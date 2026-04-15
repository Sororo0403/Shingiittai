#include "ModelManager.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "MaterialManager.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include <filesystem>
#include <sstream>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

void DebugLog(const std::string &message) {
#ifdef _WIN32
    OutputDebugStringA((message + "\n").c_str());
#endif
}

}

void ModelManager::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                              TextureManager *textureManager) {
    dxCommon_ = dxCommon;
    textureManager_ = textureManager;

    meshManager_.Initialize(dxCommon_);
    materialManager_.Initialize(dxCommon_);

    assimpLoader_.Initialize(textureManager_, &meshManager_, &materialManager_);

    modelRenderer_.Initialize(dxCommon_, srvManager, &meshManager_,
                              textureManager_, &materialManager_);
}

uint32_t ModelManager::Load(const std::wstring &path) {
    std::filesystem::path p = path;
    std::string pathStr = p.string();

    {
        std::ostringstream oss;
        oss << "[ModelManager] Load begin path='" << pathStr << "'";
        DebugLog(oss.str());
    }

    Model model = assimpLoader_.Load(pathStr);
    modelRenderer_.CreateSkinClusters(model);

    if (!model.animations.empty()) {
        model.currentAnimation = model.animations.begin()->first;
        model.animationTime = 0.0f;
        model.isLoop = true;
        model.isPlaying = true;
        model.animationFinished = false;
    }

    animator_.Update(model, 0.0f);
    modelRenderer_.UpdateSkinClusters(model);

    models_.push_back(model);
    uint32_t modelId = static_cast<uint32_t>(models_.size() - 1);

    {
        std::ostringstream oss;
        oss << "[ModelManager] Load success modelId=" << modelId
            << " meshes=" << model.subMeshes.size()
            << " bones=" << model.bones.size()
            << " anims=" << model.animations.size();
        DebugLog(oss.str());
    }

    return modelId;
}

void ModelManager::Draw(uint32_t modelId, const Transform &transform,
                        const Camera &camera) {
    if (modelId >= models_.size()) {
        return;
    }

    modelRenderer_.Draw(models_[modelId], transform, camera);
}

void ModelManager::SetDrawEffect(const ModelDrawEffect &effect) {
    modelRenderer_.SetDrawEffect(effect);
}

void ModelManager::ClearDrawEffect() { modelRenderer_.ClearDrawEffect(); }

void ModelManager::SetSceneLighting(const SceneLighting &lighting) {
    modelRenderer_.SetSceneLighting(lighting);
}

void ModelManager::PreDraw() { modelRenderer_.PreDraw(); }

void ModelManager::PostDraw() { modelRenderer_.PostDraw(); }

void ModelManager::UpdateAnimation(uint32_t modelId, float deltaTime) {
    if (modelId >= models_.size()) {
        std::ostringstream oss;
        oss << "[ModelManager] UpdateAnimation invalid modelId=" << modelId
            << " size=" << models_.size();
        DebugLog(oss.str());
        return;
    }

    animator_.Update(models_[modelId], deltaTime);
    modelRenderer_.UpdateSkinClusters(models_[modelId]);
}

void ModelManager::PlayAnimation(uint32_t modelId,
                                 const std::string &animationName, bool loop) {
    if (modelId >= models_.size()) {
        std::ostringstream oss;
        oss << "[ModelManager] PlayAnimation invalid modelId=" << modelId
            << " size=" << models_.size();
        DebugLog(oss.str());
        return;
    }

    {
        std::ostringstream oss;
        oss << "[ModelManager] PlayAnimation modelId=" << modelId
            << " name='" << animationName << "' loop=" << (loop ? 1 : 0);
        DebugLog(oss.str());
    }

    animator_.Play(models_[modelId], animationName, loop);
}

bool ModelManager::IsAnimationFinished(uint32_t modelId) const {
    if (modelId >= models_.size()) {
        return false;
    }

    return animator_.IsFinished(models_[modelId]);
}

Model *ModelManager::GetModel(uint32_t modelId) {
    if (modelId >= models_.size()) {
        return nullptr;
    }

    return &models_[modelId];
}
