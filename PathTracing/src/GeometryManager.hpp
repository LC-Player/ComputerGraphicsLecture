// GeometryManager.hpp
#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "Scene.h"
#include "Transform.h"
#include "vulkan/Texture.hpp"

namespace RYRayTracing {

class VulkanDevice;
class Buffer;

class GeometryManager {
public:
    GeometryManager();
    void setFramesInFlight(uint32_t fif);

    int addModelSource(const std::string& objPath, const std::string& texturePath = "");

    void createDummyTexture(VulkanDevice& device);
    void createTextures(VulkanDevice& device);
    void createEnvmap(VulkanDevice& device, const std::string& path = "assets/textures/envmap.jpg");

    // Builds merged vertex/index/modelRef GPU buffers with AS-build-compatible usage flags.
    void createGeometryBuffers(VulkanDevice& device,
                               std::vector<ModelRef>& modelRefs,
                               const std::vector<int>& modelRefSourceIdx,
                               const std::vector<Transform>& modelRefTransforms);

    void updateModelRefBuffer(size_t currentFrame, const std::vector<ModelRef>& modelRefs);

    std::vector<ModelSource>& modelSources() { return m_modelSources; }
    const std::vector<ModelSource>& modelSources() const { return m_modelSources; }

    const std::vector<Buffer>& vertexBuffers() const    { return m_vertexBuffers; }
    const std::vector<Buffer>& indexBuffers() const     { return m_indexBuffers; }
    const std::vector<Buffer>& modelRefBuffers() const  { return m_modelRefBuffers; }
    const std::vector<std::unique_ptr<Texture>>& textures() const { return m_textures; }
    const std::unique_ptr<Texture>& envmapTexture() const { return m_envmapTexture; }

    vk::ImageView dummyTextureView() const { return *m_dummyTextureView; }
    vk::Sampler textureSampler() const     { return *m_textureSampler; }

    static constexpr size_t kMaxVertices  = 1'000'000;
    static constexpr size_t kMaxIndices   = 2'000'000;
    static constexpr size_t kMaxModelRefs = 64;
    static constexpr size_t kMaxTextures  = 8;

private:
    void uploadImageWithStaging(VulkanDevice& device, vk::Image image,
                                uint32_t width, uint32_t height,
                                const void* data, size_t dataSize);

    uint32_t m_framesInFlight = 0;

    std::vector<ModelSource> m_modelSources;

    std::vector<std::unique_ptr<Texture>> m_textures;
    std::unique_ptr<Texture> m_envmapTexture;
    vk::raii::Image m_dummyTextureImage = nullptr;
    vk::raii::DeviceMemory m_dummyTextureMemory = nullptr;
    vk::raii::ImageView m_dummyTextureView = nullptr;
    vk::raii::Sampler m_textureSampler = nullptr;

    // Per-frame GPU buffers with AS-build-compatible usage
    std::vector<Buffer> m_vertexBuffers;
    std::vector<Buffer> m_indexBuffers;
    std::vector<Buffer> m_modelRefBuffers;
};

} // namespace RYRayTracing
