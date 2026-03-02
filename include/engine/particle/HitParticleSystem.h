#pragma once
#include "BaseParticleSystem.h"

class HitParticleSystem : public BaseParticleSystem {
  public:
    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="deltaTime">前フレームからの経過時間(秒)</param>
    void Update(float deltaTime) override;

    /// <summary>
    /// 指定したTransform位置を基準にヒットエフェクトを生成
    /// </summary>
    /// <param
    /// name="baseTf">エフェクトを発生させる基準Transform</param>
    void Spawn(const Transform &baseTf);

  private:
    static constexpr int kParticleCount = 12;
};