#include "ParticleManager.h"
#include "ModelManager.h"

void ParticleManager::Initialize(ModelManager *modelManager) {
    modelManager_ = modelManager;
}

void ParticleManager::CreateEmitter(const std::string &name, uint32_t modelId) {

    auto system = std::make_unique<ParticleSystem>();
    system->Initialize(modelManager_, modelId);

    auto emitter = std::make_unique<ParticleEmitter>();
    emitter->Initialize(system.get());

    emitters_[name] = {std::move(system), std::move(emitter)};
}

ParticleEmitter *ParticleManager::GetEmitter(const std::string &name) {
    return emitters_[name].emitter.get();
}

void ParticleManager::Update(float dt) {
    for (auto &[name, entry] : emitters_) {
        entry.emitter->Update(dt);
        entry.system->Update(dt);
    }
}

void ParticleManager::Draw(const Camera &camera) {
    for (auto &[name, entry] : emitters_) {
        entry.system->Draw(camera);
    }
}