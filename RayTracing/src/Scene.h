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
    float refractivity;   // 0.0 = opaque, 1.0 = pure glass
    float indexOfRefraction;            // index of refraction (e.g. 1.5 for glass)
    float _pad;
};

// GPU-compatible plane (infinite plane defined by point + normal)
struct PlaneData {
    alignas(16) glm::vec4 point;
    alignas(16) glm::vec4 normal;
    alignas(16) glm::vec4 color;
};

// Light types passed to RT shader (reuse from Light.h for rasterization,
// but GPU-packed here for compute shader SSBO)
struct RTLight {
    enum Type : int { POINT = 0, DIRECTIONAL = 1, SPOT = 2 };
    alignas(16) glm::vec3 position;   // or direction for directional
    int type;
    alignas(16) glm::vec3 color;
    float intensity;
    float maxDistance;
    float _pad[3];
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
