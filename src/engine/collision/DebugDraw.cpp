#include "DebugDraw.h"
#include "Camera.h"
#include "ModelManager.h"

void DebugDraw::Initialize(uint32_t boxModelId) { boxModelId_ = boxModelId; }

void DebugDraw::DrawOBB(ModelManager *modelManager, const OBB &obb,
                        const Camera &camera) {
    Transform tf;

    tf.position = obb.center;
    tf.rotation = obb.rotation;

    tf.scale = {obb.size.x * 2.0f, obb.size.y * 2.0f, obb.size.z * 2.0f};

    modelManager->Draw(boxModelId_, tf, camera);
}

void DebugDraw::DrawAABB(ModelManager *modelManager, const AABB &aabb,
                         const Camera &camera) {
    Transform tf;

    tf.position = {(aabb.min.x + aabb.max.x) * 0.5f,
                   (aabb.min.y + aabb.max.y) * 0.5f,
                   (aabb.min.z + aabb.max.z) * 0.5f};

    tf.rotation = {0, 0, 0, 1};

    tf.scale = {aabb.max.x - aabb.min.x, aabb.max.y - aabb.min.y,
                aabb.max.z - aabb.min.z};

    modelManager->Draw(boxModelId_, tf, camera);
}
