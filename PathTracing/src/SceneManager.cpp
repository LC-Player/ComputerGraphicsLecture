// SceneManager.cpp

#include "SceneManager.hpp"
#include "SceneConfig.h"
#include "core/Logger.hpp"
#include "vulkan/Buffer.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <glm/gtc/constants.hpp>
#include <cmath>

namespace RYRayTracing {

SceneManager::SceneManager(uint32_t windowWidth, uint32_t windowHeight) {
    m_camera.SetAspectRatio(static_cast<float>(windowWidth) / windowHeight);
    m_camera.SetPerspective(glm::radians(60.0f), 0.1f, 200.0f);
    m_cameraTransform.translation = {0.0f, 0.0f, 0.0f};
    m_cameraTransform.rotation = {0, 0, 0};
}

void SceneManager::setFramesInFlight(uint32_t fif) {
    m_framesInFlight = fif;
}

void SceneManager::loadCameraAndLights(const SceneConfig& cfg) {
    m_ambientStrength = cfg.ambientStrength;

    for (const auto& pl : cfg.lights) {
        LightData l{};
        l.color = pl.color;
        l.intensity = pl.intensity;
        if (pl.type == LightType::Directional) {
            l.type = 2;
            l.direction = pl.direction;
        } else if (pl.type == LightType::Spot) {
            l.type = 1;
            l.position = pl.position;
            l.direction = pl.direction;
            l.maxDistance = pl.maxDistance;
            l.innerCos = std::cos(glm::radians(pl.innerAngle));
            l.outerCos = std::cos(glm::radians(pl.outerAngle));
        } else {
            l.type = 0;
            l.position = pl.position;
            l.maxDistance = pl.maxDistance;
        }
        m_lights.push_back(l);
    }
    setLightsDirty();
}

int SceneManager::addMaterial(const glm::vec3& diffuseColor, float metallic,
                               float roughness, float transparency, float ior) {
    MaterialData mat{};
    mat.diffuseColor = diffuseColor;
    mat.metallic = metallic;
    mat.roughness = roughness;
    mat.transparency = transparency;
    mat.ior = ior;
    int idx = static_cast<int>(m_materials.size());
    m_materials.push_back(mat);
    return idx;
}

void SceneManager::addModelRef(int srcIdx, int matIdx, const Transform& transform,
                                const glm::vec3& diffuseColor) {
    ModelRef ref{};
    ref.materialId = static_cast<uint32_t>(matIdx);
    ref.color = glm::vec4(diffuseColor, 1.0f);
    m_modelRefs.push_back(ref);
    m_modelRefSourceIdx.push_back(srcIdx);
    m_modelRefTransforms.push_back(transform);
}

void SceneManager::createLightBuffers(VulkanDevice& device) {
    constexpr size_t kMaxLights = 16;
    if (m_lights.empty()) {
        m_lights.push_back({});
    }
    m_lightBuffers.clear();
    m_lightBuffers.reserve(m_framesInFlight);
    for (size_t i = 0; i < m_framesInFlight; i++) {
        m_lightBuffers.push_back(Buffer::createBuffer(&device,
            kMaxLights * sizeof(LightData),
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
        m_lightBuffers.back().copyFrom(m_lights.data(), m_lights.size() * sizeof(LightData));
    }
    m_lightsDirty = 0;
    LOG_INFO("Light SSBO created (max " + std::to_string(kMaxLights) + " lights)");
}

void SceneManager::createSphereBuffers(VulkanDevice& device) {
    m_sphereBuffers.clear();
    m_sphereBuffers.reserve(m_framesInFlight);
    for (size_t i = 0; i < m_framesInFlight; i++) {
        m_sphereBuffers.push_back(Buffer::createBuffer(&device,
            kMaxSpheres * sizeof(SphereData),
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
        if (!m_spheres.empty())
            m_sphereBuffers.back().copyFrom(m_spheres.data(), m_spheres.size() * sizeof(SphereData));
    }
    m_spheresDirty = 0;
    LOG_INFO("Sphere buffer created with " + std::to_string(m_spheres.size()) + " spheres");
}

void SceneManager::createMaterialBuffers(VulkanDevice& device) {
    m_materialBuffers.clear();
    m_materialBuffers.reserve(m_framesInFlight);
    for (size_t i = 0; i < m_framesInFlight; i++) {
        m_materialBuffers.push_back(Buffer::createBuffer(&device,
            kMaxMaterials * sizeof(MaterialData),
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
        if (!m_materials.empty())
            m_materialBuffers.back().copyFrom(m_materials.data(), m_materials.size() * sizeof(MaterialData));
    }
    m_materialsDirty = 0;
    LOG_INFO("Material buffer created with " + std::to_string(m_materials.size()) + " materials");
}

void SceneManager::updateLightBuffer(size_t currentFrame) {
    if (!isLightsDirty() || m_lights.empty()) return;
    m_lightBuffers[currentFrame].copyFrom(m_lights.data(), m_lights.size() * sizeof(LightData));
}

void SceneManager::updateSphereBuffer(size_t currentFrame) {
    if (!isSpheresDirty() || m_spheres.empty()) return;
    m_sphereBuffers[currentFrame].copyFrom(m_spheres.data(), m_spheres.size() * sizeof(SphereData));
}

void SceneManager::updateMaterialBuffer(size_t currentFrame) {
    if (!isMaterialsDirty() || m_materials.empty()) return;
    m_materialBuffers[currentFrame].copyFrom(m_materials.data(), m_materials.size() * sizeof(MaterialData));
}

bool SceneManager::isLightsDirty()    { bool v = m_lightsDirty > 0;    m_lightsDirty -= v;    return v; }
bool SceneManager::isSpheresDirty()   { bool v = m_spheresDirty > 0;   m_spheresDirty -= v;   return v; }
bool SceneManager::isMaterialsDirty() { bool v = m_materialsDirty > 0; m_materialsDirty -= v; return v; }
bool SceneManager::isModelRefsDirty() { bool v = m_modelRefsDirty > 0; m_modelRefsDirty -= v; return v; }

} // namespace RYRayTracing
