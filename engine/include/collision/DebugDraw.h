#pragma once
#include "AABB.h"
#include "OBB.h"
#include "Transform.h"
#include <cstdint>

class ModelManager;
class Camera;

class DebugDraw {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="boxModelId">1x1x1 boxモデルID</param>
    void Initialize(uint32_t boxModelId);

    /// <summary>
    /// OBB描画処理
    /// </summary>
    /// <param name="modelManager">ModelManagerインスタンス</param>
    /// <param name="obb">描画するOBB</param>
    /// <param name="camera">描画に使用するカメラ</param>
    void DrawOBB(ModelManager *modelManager, const OBB &obb,
                 const Camera &camera);

    /// <summary>
    /// AABB描画処理
    /// </summary>
    /// <param name="modelManager">ModelManagerインスタンス</param>
    /// <param name="aabb">描画するAABB</param>
    /// <param name="camera">描画に使用するカメラ</param>
    void DrawAABB(ModelManager *modelManager, const AABB &aabb,
                  const Camera &camera);

  private:
    uint32_t boxModelId_ = 0;
};