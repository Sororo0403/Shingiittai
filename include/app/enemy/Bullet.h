#pragma once
#include "Camera.h"
#include "OBB.h"
#include "Transform.h"
#include <cstdint>

class ModelManager;

class Bullet {
public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="modelId">球のモデルId</param>
    void Initialize(uint32_t modelId);

    /// <summary>
    /// 更新処理
    /// </summary>
    void Update();

    /// <summary>
    /// 描画処理
    /// </summary>
    void Draw(ModelManager *modelManager, const Camera &camera);

    const Transform &GetTransform() const { tf_; }
    void SetTransform(Transform &tf) { tf_ = tf; }

    OBB GetOBB() const;

private:
    Transform tf_;
    uint32_t modelId_ = 0;

    bool isShot_ = false;

    DirectX::XMFLOAT3 size_ = {1.0f, 1.0f, 1.0f};
};
