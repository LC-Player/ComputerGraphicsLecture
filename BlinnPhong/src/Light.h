#pragma once
#include "Transform.h"

namespace RYBlinnPhong {
    struct PointLightInfo {
        alignas(16) glm::vec3 pos;
        alignas(16) glm::vec3 color;
        float intensity;
        float maxDistance;
        char padding[8];
    };
    struct SpotLightInfo {
        alignas(16) glm::vec3 pos;
        alignas(16) glm::vec3 color;
        alignas(16) glm::vec3 dir;
        float cosineInclinationAngle;
        float cosineExclusivityAngle;
        float intensity;
        float maxDistance;
    };
    struct DirectionalLight {
        alignas(16) glm::vec3 dir;
        alignas(16) glm::vec3 color;
        float intensity;
        char padding[12];
    };
    struct LightInfo
    {
        alignas(16) PointLightInfo pointLight1;
        alignas(16) PointLightInfo pointLight2;
        alignas(16) SpotLightInfo spotLight;
        alignas(16) DirectionalLight directionalLight;

        alignas(16) glm::vec4 ambientArgs;  // ambientColor = (x, y, z), w : strength
        float diffuseStrength; // 漫反射
        float specularStrength; // 镜面反射
        char padding[8];
    };
}
