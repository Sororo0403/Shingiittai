#pragma once
#include "Model.h"
#include <string>

class TextureManager;
class MeshManager;
class MaterialManager;

class TinyGltfLoader {
  public:
    void Initialize(TextureManager *textureManager, MeshManager *meshManager,
                    MaterialManager *materialManager);

    Model Load(const std::string &path);

  private:
    TextureManager *textureManager_ = nullptr;
    MeshManager *meshManager_ = nullptr;
    MaterialManager *materialManager_ = nullptr;
};
