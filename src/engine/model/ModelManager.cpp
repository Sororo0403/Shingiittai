#include "ModelManager.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "MeshManager.h"
#include "Model.h"
#include "ModelRenderer.h"
#include "ObjLoader.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include <stdexcept>

void ModelManager::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager) {
    dxCommon_ = dxCommon;

    meshManager_.Initialize(dxCommon_);
    textureManager_.Initialize(dxCommon_, srvManager);
    objLoader_.Initialize(&textureManager_, &meshManager_);
    modelRenderer_.Initialize(dxCommon_, srvManager, &meshManager_,
                              &textureManager_);
}

uint32_t ModelManager::Load(const std::wstring &path) {
    Model model = objLoader_.Load(path);
    models_.push_back(model);
    return static_cast<uint32_t>(models_.size() - 1);
}

void ModelManager::Draw(uint32_t modelId, const Camera &camera) {
    if (modelId >= models_.size()) {
        return;
    }

    modelRenderer_.Draw(models_[modelId], camera);
}

void ModelManager::PreDraw() { modelRenderer_.PreDraw(); }

void ModelManager::PostDraw() { modelRenderer_.PostDraw(); }
