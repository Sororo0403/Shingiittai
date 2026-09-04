#pragma once
#include "SwordControllerState.h"
#include "SwordPose.h"
#include "Transform.h"

class Input;
/// <summary>
/// マウス入力からデバッグ用の剣姿勢を生成する
/// </summary>
class SwordMouseController {
  public:
    /// <summary>
    /// 入力と状態を1フレーム進める
    /// </summary>
    void Update(Input *input, float dt, const Transform &swordPos);

    /// <summary>
    /// GetPoseに対応する現在値を取得する
    /// </summary>
    SwordPose GetPose() const;

  private:
    void UpdateOrientation(Input *input, float dt);
    void UpdateSlash(Input *input, float dt);

    SwordControllerState state_{};

    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
};
