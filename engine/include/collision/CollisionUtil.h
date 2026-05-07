#pragma once
#include "AABB.h"
#include "OBB.h"
#include <DirectXMath.h>

/// <summary>
/// 各種当たり判定関数を提供する
/// </summary>
namespace CollisionUtil {

struct CollisionResult {
    bool hit = false;
    DirectX::XMFLOAT3 normal = {0.0f, 0.0f, 0.0f};
    float penetration = 0.0f;
};

/// <summary>
/// 2つのOBBの衝突情報を取得
/// </summary>
/// <param name="a">判定対象となる1つ目のOBB</param>
/// <param name="b">判定対象となる2つ目のOBB</param>
/// <returns>衝突有無、aからbへ向かう法線、めり込み量</returns>
CollisionResult TestOBB(const OBB &a, const OBB &b);

/// <summary>
/// 2つのOBBが衝突しているかを判定
/// </summary>
/// <param name="a">判定対象となる1つ目のOBB</param>
/// <param name="b">判定対象となる2つ目のOBB</param>
/// <returns>2つのOBBが交差している場合はtrue、交差していない場合はfalse</returns>
bool CheckOBB(const OBB &a, const OBB &b);

/// <summary>
/// 矩形の当たり判定
/// </summary>
/// <param name="a">当たり判定を行う矩形a</param>
/// <param name="b">当たり判定を行う矩形b</param>
/// <returns>当たっていたらtrue,当たっていなかったらfalse</returns>
bool CheckAABB(const AABB &a, const AABB &b);

} // namespace CollisionUtil
