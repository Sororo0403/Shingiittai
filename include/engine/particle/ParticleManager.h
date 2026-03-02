#pragma once
#include "ParticleEmitter.h"
#include <memory>
#include <string>
#include <unordered_map>

class ModelManager;
class Camera;

class ParticleManager {
  public:
    void Initialize(ModelManager *modelManager);

    void CreateEmitter(const std::string &name, uint32_t modelId);

    ParticleEmitter *GetEmitter(const std::string &name);

    void Update(float dt);
    void Draw(const Camera &camera);

  private:
    ModelManager *modelManager_ = nullptr;

    struct Entry {
        std::unique_ptr<ParticleSystem> system;
        std::unique_ptr<ParticleEmitter> emitter;
    };

    std::unordered_map<std::string, Entry> emitters_;
};