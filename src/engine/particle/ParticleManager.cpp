#include "ParticleManager.h"
#include "ModelManager.h"

void ParticleManager::Initialize(ModelManager *modelManager) {
    modelManager_ = modelManager;
}

void ParticleManager::CreateSystem(const std::string &name, uint32_t modelId) {
    auto system = std::make_unique<ParticleSystem>();
    system->Initialize(modelManager_, modelId);

    systems_[name] = std::move(system);
}

void ParticleManager::Update(float dt) {
    for (auto &[name, system] : systems_) {
        system->Update(dt);
    }
}

void ParticleManager::Draw(const Camera &camera) {
    for (auto &[name, system] : systems_) {
        system->Draw(camera);
    }
}

void ParticleManager::Emit(const std::string &name,
                           const DirectX::XMFLOAT3 &pos,
                           const DirectX::XMFLOAT3 &dir) {
    auto it = systems_.find(name);
    if (it != systems_.end()) {
        it->second->Emit(pos, dir);
    }
}