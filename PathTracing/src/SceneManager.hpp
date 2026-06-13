// SceneManager.hpp
#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include "Camera.h"
#include "Transform.h"
#include "Light.h"
#include "Scene.h"

namespace RYRayTracing {

struct SceneConfig;
class VulkanDevice;
class Buffer;

class SceneManager {
public:
    SceneManager(uint32_t windowWidth, uint32_t windowHeight);

    void setFramesInFlight(uint32_t fif);

    void loadCameraAndLights(const SceneConfig& cfg);
    int addMaterial(const glm::vec3& diffuseColor, float metallic, float roughness,
                    float transparency, float ior);
    void addModelRef(int srcIdx, int matIdx, const Transform& transform,
                     const glm::vec3& diffuseColor);

    void createLightBuffers(VulkanDevice& device);
    void createSphereBuffers(VulkanDevice& device);
    void createMaterialBuffers(VulkanDevice& device);

    void updateLightBuffer(size_t currentFrame);
    void updateSphereBuffer(size_t currentFrame);
    void updateMaterialBuffer(size_t currentFrame);

    bool isLightsDirty();
    bool isSpheresDirty();
    bool isMaterialsDirty();
    bool isModelRefsDirty();
    void setLightsDirty()       { m_lightsDirty    = static_cast<int>(m_framesInFlight); }
    void setSpheresDirty()      { m_spheresDirty   = static_cast<int>(m_framesInFlight); }
    void setMaterialsDirty()    { m_materialsDirty = static_cast<int>(m_framesInFlight); }
    void setModelRefsDirty()    { m_modelRefsDirty = static_cast<int>(m_framesInFlight); }

    Transform& cameraTransform()          { return m_cameraTransform; }
    SceneCamera& camera()                 { return m_camera; }
    std::vector<LightData>& lights()      { return m_lights; }
    std::vector<SphereData>& spheres()    { return m_spheres; }
    std::vector<MaterialData>& materials(){ return m_materials; }
    std::vector<ModelRef>& modelRefs()    { return m_modelRefs; }
    std::vector<int>& modelRefSourceIdx() { return m_modelRefSourceIdx; }
    std::vector<Transform>& modelRefTransforms() { return m_modelRefTransforms; }
    float& ambientStrength()              { return m_ambientStrength; }

    const std::vector<Buffer>& lightBuffers() const    { return m_lightBuffers; }
    const std::vector<Buffer>& sphereBuffers() const   { return m_sphereBuffers; }
    const std::vector<Buffer>& materialBuffers() const { return m_materialBuffers; }

    static constexpr size_t kMaxSpheres   = 32;
    static constexpr size_t kMaxMaterials = 32;
    static constexpr size_t kMaxModelRefs = 64;

private:
    uint32_t m_framesInFlight = 0;

    Transform m_cameraTransform;
    SceneCamera m_camera;
    std::vector<LightData> m_lights;
    std::vector<SphereData> m_spheres;
    std::vector<MaterialData> m_materials;
    std::vector<ModelRef> m_modelRefs;
    std::vector<int> m_modelRefSourceIdx;
    std::vector<Transform> m_modelRefTransforms;
    float m_ambientStrength = 0.1f;

    std::vector<Buffer> m_lightBuffers;
    std::vector<Buffer> m_sphereBuffers;
    std::vector<Buffer> m_materialBuffers;

    int m_lightsDirty    = 0;
    int m_spheresDirty   = 0;
    int m_materialsDirty = 0;
    int m_modelRefsDirty = 0;
};

} // namespace RYRayTracing
