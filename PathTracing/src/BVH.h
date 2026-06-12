// BVH.h
#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace RYRayTracing {

struct ModelSource;

// std430-compatible BVH node for GPU SSBO (48 bytes).
// Internal: leftOrFirst/rightOrCount = child node indices, splitAxis = 0/1/2 = x/y/z
// Leaf:     leftOrFirst = global offset into merged index buffer,
//           rightOrCount = triangle count, splitAxis = -1
struct BVHNode {
    alignas(16) glm::vec3 bboxMin;  // offset 0 (12 bytes)
    alignas(16) glm::vec3 bboxMax;  // offset 16 (12 bytes)
    int32_t leftOrFirst;            // offset 28
    int32_t rightOrCount;           // offset 32
    int32_t splitAxis;              // offset 36
    float   _pad;                   // offset 40 → 48
};
static_assert(sizeof(BVHNode) == 48, "BVHNode must be 48 bytes (std430)");

// Per-model-source BVH built from local triangle indices.
// triRemap[i] = global offset into merged index buffer for the i-th
// triangle in BVH leaf order (leaves reference this via leftOrFirst).
struct ModelSourceBVH {
    std::vector<BVHNode> nodes;
    std::vector<uint32_t> triRemap;
    int32_t rootIndex = -1;
};

int32_t buildSAHBVH(const ModelSource& src, ModelSourceBVH& outBVH);

} // namespace RYRayTracing
