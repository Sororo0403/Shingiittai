#include "ModelManager.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "SrvManager.h"
#include <filesystem>

void ModelManager::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager) {
    dxCommon_ = dxCommon;

    meshManager_.Initialize(dxCommon_);
    textureManager_.Initialize(dxCommon_, srvManager);

    assimpLoader_.Initialize(&textureManager_, &meshManager_);

    modelRenderer_.Initialize(dxCommon_, srvManager, &meshManager_,
                              &textureManager_);
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