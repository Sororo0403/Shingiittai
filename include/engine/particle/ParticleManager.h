#pragma once
#include "ParticleSystem.h"
#include <memory>
#include <string>
#include <unordered_map>

class ModelManager;
class Camera;

class ParticleManager {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="modelManager">ModelManagerインスタンス</param>
    void Initialize(ModelManager *modelManager);

    /// <summary>
    /// パーティクルシステムを追加
    /// </summary>
    /// <param name="name">識別名</param>
    /// <param name="modelId">使用するモデルID</param>
    void CreateSystem(const std::string &name, uint32_t modelId);

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="deltaTime">前フレームからの経過時間(秒)</param>
    void Update(float deltaTime);

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="camera">描画に使用するカメラ</param>
    void Draw(const Camera &camera);

    /// <summary>
    /// パーティクル生成
    /// </summary>
    void Emit(const std::string &name, const DirectX::XMFLOAT3 &position,
              const DirectX::XMFLOAT3 &direction);

  private:
    ModelManager *modelManager_ = nullptr;

    std::unordered_map<std::string, std::unique_ptr<ParticleSystem>> systems_;
};