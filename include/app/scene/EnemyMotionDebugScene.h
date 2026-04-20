#pragma once
#include "BaseScene.h"
#include "Camera.h"
#include "Enemy.h"
#include <DirectXMath.h>
#include <cstdint>
#include <string>

#ifdef _DEBUG
#include "DebugCamera.h"
#endif // _DEBUG

class EnemyMotionDebugScene : public BaseScene {
  public:
    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;

  private:
    void UpdateCamera();
    void UpdateEnemyObservation();
    void SyncEnemyAnimation();
    void DrawDebugUi();
    const char *GetActionKindName(ActionKind kind) const;
    const char *GetActionStepName(ActionStep step) const;
    const char *GetPhaseName(BossPhase phase) const;

  private:
    Camera camera_;
#ifdef _DEBUG
    DebugCamera debugCamera_;
    Camera tripodCamera_;
#endif // _DEBUG
    Camera *currentCamera_ = nullptr;

    Enemy enemy_;
    uint32_t enemyModelId_ = 0;
    std::string enemyAnimationName_{};
    bool enemyAnimationLoop_ = true;
    bool enemyIntroAnimationStarted_ = false;
    IntroPhase enemyIntroPhase_ = IntroPhase::SecondSlash;

    PlayerCombatObservation playerObs_{};
    DirectX::XMFLOAT3 previewPlayerPos_ = {0.0f, 0.0f, 0.0f};
    bool pauseEnemyUpdate_ = false;
    bool lockIdle_ = false;
    bool drawHitBoxes_ = true;
    float sceneLightTime_ = 0.0f;

#ifdef _DEBUG
    DirectX::XMFLOAT3 tripodPos_ = {0.0f, 2.0f, 1.0f};
    DirectX::XMFLOAT3 tripodTarget_ = {0.0f, 1.25f, 10.0f};
#endif // _DEBUG
};
