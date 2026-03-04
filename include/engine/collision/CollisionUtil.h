#pragma once
#include "AABB.h"
#include "OBB.h"
#include <DirectXMath.h>

namespace CollisionUtil {

/// <summary>
///
/// </summary>
/// <param name="a"></param>
/// <param name="b"></param>
/// <returns></returns>
bool CheckOBB(const OBB &a, const OBB &b);

/// <summary>
/// 矩形の当たり判定
/// </summary>
/// <param name="a">当たり判定を行う矩形a</param>
/// <param name="b">当たり判定を行う矩形b</param>
/// <returns>当たっていたらtrue,当たっていなかったらfalse</returns>
bool CheckAABB(const AABB &a, const AABB &b);

} // namespace CollisionUtil