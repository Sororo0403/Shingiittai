#pragma once
#include "OBB.h"
#include "Transform.h"
#include <DirectXMath.h>

class ModelManager;
class Camera;

class Sword {
  public:
    void Initialize(uint32_t modelId);
    void Update(const Transform &transform);

    void Draw(ModelManager *modelManager, const Camera &camera);

    const Transform &GetTransform() const { return tf_; }
    OBB GetOBB() const;

    void ImGuiDraw();

  private:
    static constexpr float kSwordLength = 1.2f;
    DirectX::XMFLOAT3 size_{0.2f, 0.2f, 0.6f};

    uint32_t modelId_ = 0;
    Transform tf_;
};
