// Scene.h
#pragma once

#include <glm/glm.hpp>

namespace RYRayTracing {

// GPU-compatible sphere (std430 layout in shader)
struct SphereData {
    alignas(16) glm::vec3 center;
    float radius;
    alignas(16) glm::vec4 color;
    float reflectivity;   // 0.0 = diffuse, 1.0 = perfect mirror
    float indexOfRefraction;            // index of refraction (e.g. 1.5 for glass)
    float _pad;
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
