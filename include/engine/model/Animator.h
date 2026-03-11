#pragma once
#include "Model.h"

class Animator {
  public:
    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="model">対象モデル</param>
    /// <param name="deltaTime">経過秒</param>
    void Update(Model &model, float deltaTime);

  private:
    float currentTime_ = 0.0f;

  private:
    DirectX::XMFLOAT3 SampleVec3(const std::vector<AnimationKeyVec3> &keys,
                                 float time);

    DirectX::XMFLOAT4 SampleQuat(const std::vector<AnimationKeyQuat> &keys,
                                 float time);

    DirectX::XMMATRIX MakeLocalMatrix(const BoneAnimation &anim, float time);
};