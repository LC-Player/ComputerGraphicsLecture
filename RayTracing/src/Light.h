// Light.h
#pragma once
#include <glm/glm.hpp>

namespace RYRayTracing {

// Unified light struct (std430, 64 bytes).
// type: 0=point, 1=spot, 2=directional
struct LightData {
    alignas(16) glm::vec3 position; // offset 0
    int type; // offset 12
    alignas(16) glm::vec3 color; // offset 16
    float intensity; // offset 28
    alignas(16) glm::vec3 direction; // offset 32
    float maxDistance; // offset 44
    float innerCos; // offset 48 (spot only)
    float outerCos; // offset 52 (spot only)
    float _pad[2]; // offset 56 → 64
};

} // namespace RYRayTracing
