// SceneConfig.h
#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace RYRayTracing {

enum class LightType { Point, Spot, Directional };

struct ParsedLight {
    LightType type = LightType::Point;
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float maxDistance = 30.0f;
    float innerAngle = 15.0f;  // degrees, spot only
    float outerAngle = 30.0f;  // degrees, spot only
};

struct ParsedModel {
    std::string filename;
    bool display = true;
    bool normalInterpolation = false;

    glm::vec3 scale{1.0f};
    glm::vec3 rotation{0.0f};      // Euler angles, degrees
    glm::vec3 translation{0.0f};

    float ior = 1.0f;
    glm::vec4 albedo{1.0f, 0.0f, 0.0f, 0.0f}; // Whitted-style: diffuse, spec, reflect, refract
    glm::vec3 diffuseColor{0.5f};
    float shininess = 10.0f;

    // Converted PBR parameters (filled by convertToPBR)
    float metallic = 0.0f;
    float roughness = 0.5f;
    float transparency = 0.0f;
};

struct SceneConfig {
    int width = 256;
    int height = 192;
    std::string outputName;

    int maxDepth = 4;
    bool envMapDisplay = false;

    std::vector<ParsedModel> models;
    std::vector<ParsedLight> lights;

    float ambientStrength = 0.1f;
    glm::vec3 ambientColor{0.1f};
    float diffuseStrength = 0.5f;
    float specularStrength = 1.0f;
};

// Load scene from XML file. Uses default lights if XML has none.
SceneConfig loadSceneConfig(const std::string& xmlPath, const std::string& modelBasePath = "assets/models/");

// Convert Whitted albedo[4] + shininess → PBR metallic / roughness / transparency
void convertToPBR(ParsedModel& m);

} // namespace RYRayTracing
