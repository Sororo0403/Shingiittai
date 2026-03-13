#include "ModelManager.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "MaterialManager.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include <filesystem>

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

    Model model = assimpLoader_.Load(pathStr);
    models_.push_back(model);

    return static_cast<uint32_t>(models_.size() - 1);
}

void ModelManager::Draw(uint32_t modelId, const Transform &transform,
                        const Camera &camera) {
    if (modelId >= models_.size()) {
        return;
    }

    modelRenderer_.Draw(models_[modelId], transform, camera);
}

void ModelManager::PreDraw() { modelRenderer_.PreDraw(); }

void ModelManager::PostDraw() { modelRenderer_.PostDraw(); }

void ModelManager::UpdateAnimation(uint32_t modelId, float deltaTime) {
    if (modelId >= models_.size()) {
        return;
    }

    animator_.Update(models_[modelId], deltaTime);
}