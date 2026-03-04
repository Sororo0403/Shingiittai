#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "Player.h"

class GameScene : public BaseScene {
  public:
    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;

  private:
    Camera camera_;

    Player player_;
};