#pragma once

#include "model/Vertex.h"

#include <cstdint>
#include <vector>

/// <summary>メッシュの頂点・インデックスバッファとビューを保持する</summary>
struct MeshGeometry {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
