// Scene.h
#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <string>

namespace RYRayTracing {

// std430-compatible vertex for ray tracing SSBO
struct GPUVertex {
    alignas(16) glm::vec3 position;
    alignas(16) glm::vec3 normal;
    glm::vec2 texCoord;
    float _pad[2];
};
static_assert(sizeof(GPUVertex) == 48, "GPUVertex must be 48 bytes (std430)");

// GPU-compatible material (std430 layout in shader)
struct MaterialData {
    alignas(16) glm::vec3 diffuseColor;
    float transparency;
    alignas(16) glm::vec3 emission;
    float metallic;
    float roughness;
    float ior;
    float _pad[2];
};
static_assert(sizeof(MaterialData) == 48, "MaterialData must be 48 bytes (std430)");

// GPU-compatible sphere (std430 layout in shader)
struct SphereData {
    alignas(16) glm::vec3 center;
    float radius;
    uint32_t materialIndex;
    float _pad[3];
};
static_assert(sizeof(SphereData) == 32, "SphereData must be 32 bytes (std430)");

// One ModelRef = one renderable object in the ray tracing scene.
// References a range in the merged vertex/index SSBOs.
struct ModelRef {
    uint32_t firstIndex;               // offset into merged index buffer
    uint32_t indexCount;               // number of indices (triangles * 3)
    uint32_t vertexOffset;             // offset into merged vertex buffer
    uint32_t materialId;               // index into material array
    alignas(16) glm::mat4 invTransform; // world→object space transform
    alignas(16) glm::vec4 color;        // per-instance tint
    alignas(16) glm::vec3 boundingSphereCenter; // object space
    float boundingSphereRadius;
    int32_t textureIndex;              // -1 = use diffuse color, >=0 = texture array index
    int32_t bvhRoot;                   // index into bvhNodes SSBO; -1 = no BVH
    float _pad[2];
};
static_assert(sizeof(ModelRef) == 128, "ModelRef must be 128 bytes (std430)");

// Model source data (loaded from OBJ, used to build merged buffers)
struct ModelSource {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texCoords;
    std::vector<uint32_t> indices;
    glm::vec3 boundingSphereCenter;
    float boundingSphereRadius;
    uint32_t vertexOffset = 0;   // set once after merge
    uint32_t firstIndex  = 0;   // set once after merge
    uint32_t indexCount  = 0;
    int32_t  bvhRoot      = -1; // set once after merge
    std::string name;
    std::string texturePath;
};

} // namespace RYRayTracing
