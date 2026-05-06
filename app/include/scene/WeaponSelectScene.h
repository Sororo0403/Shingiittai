#pragma once
#include "BaseScene.h"
#include "PlayerWeaponType.h"

class WeaponSelectScene : public BaseScene {
  public:
    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;

  private:
    void StartGame(PlayerWeaponType weaponType);
    void DrawDebugUi();

  private:
    int selectedIndex_ = 0;
    bool startRequested_ = false;
    PlayerWeaponType requestedWeaponType_ = PlayerWeaponType::Standard;
};
