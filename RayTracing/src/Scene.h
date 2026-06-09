// Scene.h
#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace RYRayTracing {

// GPU-compatible material (std430 layout in shader)
struct MaterialData {
    alignas(16) glm::vec3 diffuseColor;
    float transparency; // 1 for fully transparent, transparency ~ 1 - alpha
    alignas(16) glm::vec3 emission;
    float metallic; // 1 like metal
    float roughness; // 0 = mirror, 1 = very rough
    float ior; // index of refraction
};

// GPU-compatible sphere (std430 layout in shader)
struct SphereData {
    alignas(16) glm::vec3 center;
    float radius;
    uint32_t materialIndex;
    float _pad[3];            // std430 alignment (struct size = 32)
};

// GPU-compatible plane (infinite plane defined by point + normal)
struct PlaneData {
    alignas(16) glm::vec4 point;
    alignas(16) glm::vec4 normal;
    alignas(16) glm::vec4 color;
};

// Model reference for triangle intersection (maps to vertex/index SSBOs)
struct ModelRef {
    uint32_t firstIndex;    // offset into index buffer
    uint32_t indexCount;    // number of indices (triangles * 3)
    uint32_t vertexOffset;  // offset into vertex buffer
    uint32_t materialId;    // index into material array
    alignas(16) glm::vec3 boundingSphereCenter;
    float boundingSphereRadius;
};

} // namespace RYRayTracing
