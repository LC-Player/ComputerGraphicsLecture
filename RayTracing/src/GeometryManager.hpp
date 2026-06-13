// GeometryManager.hpp
#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "Scene.h"
#include "BVH.h"
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

    // Builds merged vertex/index/BVH buffers and model ref GPU SSBO.
    // modelRefs & modelRefSourceIdx: from SceneManager, mutated to fill source-derived fields.
    void createGeometryBuffers(VulkanDevice& device,
                               std::vector<ModelRef>& modelRefs,
                               const std::vector<int>& modelRefSourceIdx,
                               const std::vector<Transform>& modelRefTransforms);

    void updateModelRefBuffer(size_t currentFrame, const std::vector<ModelRef>& modelRefs);
    void syncBVHBuffers(size_t currentFrame);

    bool isBVHDirty();
    void setBVHDirty();

    std::vector<ModelSource>& modelSources() { return m_modelSources; }
    const std::vector<ModelSource>& modelSources() const { return m_modelSources; }

    // GPU buffer access (for DescriptorManager and Application)
    const std::vector<Buffer>& vertexBuffers() const      { return m_vertexBuffers; }
    const std::vector<Buffer>& indexBuffers() const       { return m_indexBuffers; }
    const std::vector<Buffer>& modelRefBuffers() const    { return m_modelRefBuffers; }
    const std::vector<Buffer>& bvhBuffers() const         { return m_bvhBuffers; }
    const std::vector<Buffer>& bvhTriRemapBuffers() const { return m_bvhTriRemapBuffers; }
    const std::vector<std::unique_ptr<Texture>>& textures() const      { return m_textures; }
    const std::unique_ptr<Texture>& envmapTexture() const              { return m_envmapTexture; }

    // Raw handles needed for descriptor writes (non-owning)
    vk::ImageView dummyTextureView() const { return *m_dummyTextureView; }
    vk::Sampler textureSampler() const     { return *m_textureSampler; }

    static constexpr size_t kMaxVertices  = 1'000'000;
    static constexpr size_t kMaxIndices   = 2'000'000;
    static constexpr size_t kMaxModelRefs = 64;
    static constexpr size_t kMaxBVHNodes  = 500'000;
    static constexpr size_t kMaxTextures  = 8;

private:
    void mergeVerticesAndIndices(std::vector<GPUVertex>& outVerts,
                                 std::vector<uint32_t>& outIndices);
    void postProcessBVH(std::vector<ModelRef>& modelRefs,
                        const std::vector<int>& modelRefSourceIdx);
    void createVertexIndexMRBuffers(VulkanDevice& device,
                                    const std::vector<GPUVertex>& mergedVerts,
                                    const std::vector<uint32_t>& mergedIndices,
                                    const std::vector<ModelRef>& modelRefs);
    void createBVHBuffers(VulkanDevice& device,
                          const std::vector<uint32_t>& mergedTriRemap);
    void uploadImageWithStaging(VulkanDevice& device, vk::Image image,
                                uint32_t width, uint32_t height,
                                const void* data, size_t dataSize);

    uint32_t m_framesInFlight = 0;

    std::vector<ModelSource> m_modelSources;
    std::vector<ModelSourceBVH> m_modelBVHs;

    std::vector<std::unique_ptr<Texture>> m_textures;   // parallel to modelSources
    std::unique_ptr<Texture> m_envmapTexture;
    vk::raii::Image m_dummyTextureImage = nullptr;
    vk::raii::DeviceMemory m_dummyTextureMemory = nullptr;
    vk::raii::ImageView m_dummyTextureView = nullptr;
    vk::raii::Sampler m_textureSampler = nullptr;

    std::vector<BVHNode> m_flatBVHNodes;

    // Per-frame GPU buffers (Buffer is movable)
    std::vector<Buffer> m_vertexBuffers;
    std::vector<Buffer> m_indexBuffers;
    std::vector<Buffer> m_modelRefBuffers;
    std::vector<Buffer> m_bvhBuffers;
    std::vector<Buffer> m_bvhTriRemapBuffers;

    int m_bvhDirty = 0;
};

} // namespace RYRayTracing
