#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>
#include <vector>

class GameScene : public BaseScene {
  public:
    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;

  private:
    // モデルID
    uint32_t enemyModelId_ = 0;
    uint32_t playerModelId_ = 0;
    uint32_t swordModelId_ = 0;

    // Transform
    std::vector<Transform> enemies_;
    Transform playerTf_;
    Transform swordTf_;

    // カメラ
    Camera camera_;
    DirectX::XMFLOAT3 cameraRot_{0, 0, 0};

    // パラメータ
    float moveSpeed_ = 8.0f;
    float gyroSensitivity_ = 1.0f;
};