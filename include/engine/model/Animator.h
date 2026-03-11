#pragma once
#include "Model.h"

class Animator {
  public:
    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="model">アニメーションを更新するモデル</param>
    /// <param name="deltaTime">前フレームからの経過時間(秒)</param>
    void Update(Model &model, float deltaTime);

  private:
    // Sample
    DirectX::XMFLOAT3 SampleVec3(const std::vector<AnimationKeyVec3> &keys,
                                 float time);
    DirectX::XMFLOAT4 SampleQuat(const std::vector<AnimationKeyQuat> &keys,
                                 float time);

    // Make
    DirectX::XMMATRIX MakeAnimatedLocalMatrix(const BoneInfo &bone,
                                              const Model &model, float time);

  private:
    float currentTime_ = 0.0f;
};