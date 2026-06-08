#pragma once
#include <glm/glm.hpp>

namespace RYRayTracing {

struct PointLightData {
    alignas(16) glm::vec3 position;
    float intensity;
    alignas(16) glm::vec3 color;
    float maxDistance;
};

} // namespace RYRayTracing
