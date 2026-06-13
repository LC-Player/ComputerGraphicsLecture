// DescriptorManager.hpp
#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <array>
#include <cstdint>

namespace RYRayTracing {

// Aggregates buffer/image handles for the 10 RT resource descriptor bindings.
// All pointer members are non-owning and nullable (some buffers are optional, e.g. BVH).
// Lifetime: the pointed-to data must outlive the createRtResourceSets() call.
struct RtResourceBindings {
    const std::vector<vk::Buffer>* sphereBuffers = nullptr;
    const std::vector<vk::ImageView>* rtImageViews = nullptr;
    const std::vector<vk::Buffer>* materialBuffers = nullptr;
    const std::vector<vk::Buffer>* vertexBuffers = nullptr;
    const std::vector<vk::Buffer>* indexBuffers = nullptr;
    const std::vector<vk::Buffer>* modelRefBuffers = nullptr;
    uint32_t maxTextures = 0;
    const std::vector<vk::ImageView>* textureViews = nullptr;
    vk::ImageView dummyTextureView = nullptr;
    vk::Sampler textureSampler = nullptr;
    vk::DescriptorImageInfo envmapInfo{};
    const std::vector<vk::Buffer>* bvhBuffers = nullptr;
    const std::vector<vk::Buffer>* bvhTriRemapBuffers = nullptr;
};

class DescriptorManager {
public:
    DescriptorManager() = default;

    // Must be called before any create*() method. device must outlive this object.
    void init(vk::raii::Device& device) { m_device = &device; }

    // ── Layouts (called once during init) ──
    void createLayouts();

    // ── Pools ──
    void createDescriptorPool(uint32_t framesInFlight, uint32_t maxTextures);
    void createImGuiPool();

    // ── Descriptor set allocation + writing ──
    void createPerFrameSets(uint32_t framesInFlight,
                            const std::vector<vk::Buffer>& cameraBuffers,
                            const std::vector<vk::Buffer>& lightBuffers);
    void createRtResourceSets(uint32_t framesInFlight,
                              const RtResourceBindings& b);
    void createFullscreenSets(uint32_t framesInFlight,
                              const std::vector<vk::ImageView>& rtImageViews,
                              vk::Sampler rtSampler);

    // ── Swapchain-recreation image rebinding (in-place descriptor updates) ──
    void updateRtOutputBinding(size_t frame, vk::ImageView newView);
    void updateFullscreenBinding(size_t frame, vk::ImageView newView, vk::Sampler sampler);

    // ── Accessors ──
    const vk::DescriptorSetLayout& perFrameLayout()   const { return *m_perFrameSetLayout; }
    const vk::DescriptorSetLayout& samplerLayout()    const { return *m_samplerSetLayout; }
    const vk::DescriptorSetLayout& rtResourceLayout() const { return *m_rtResourceSetLayout; }
    const vk::DescriptorSetLayout& imguiLayout()      const { return *m_samplerSetLayout; }

    const std::vector<vk::raii::DescriptorSet>& perFrameSets()    const { return m_perFrameSets; }
    const std::vector<vk::raii::DescriptorSet>& rtSets()          const { return m_rtSets; }
    const std::vector<vk::raii::DescriptorSet>& fullscreenSets()  const { return m_fullscreenSets; }

    vk::DescriptorPool imguiPool() const { return *m_imguiPool; }

private:
    // Non-owning. Set via init() before any create*() call.
    // Lifetime: Application owns the VulkanDevice which owns this vk::raii::Device.
    vk::raii::Device* m_device = nullptr;

    vk::raii::DescriptorSetLayout m_perFrameSetLayout = nullptr;
    vk::raii::DescriptorSetLayout m_samplerSetLayout = nullptr;
    vk::raii::DescriptorSetLayout m_rtResourceSetLayout = nullptr;

    vk::raii::DescriptorPool m_descriptorPool = nullptr;
    vk::raii::DescriptorPool m_imguiPool = nullptr;

    std::vector<vk::raii::DescriptorSet> m_perFrameSets;
    std::vector<vk::raii::DescriptorSet> m_rtSets;
    std::vector<vk::raii::DescriptorSet> m_fullscreenSets;

    // Layout creation helpers
    void createPerFrameLayout();
    void createSamplerLayout();
    void createRtResourceLayout();

    // Descriptor write helpers (each writes a single binding for a given frame)
    void writeBufferBinding(size_t frame, uint32_t binding, vk::Buffer buffer,
                            vk::DescriptorType type = vk::DescriptorType::eStorageBuffer);
    void writeImageBinding(size_t frame, uint32_t binding, vk::ImageView view,
                           vk::ImageLayout layout, vk::DescriptorType type);
    void writeSamplerArrayBinding(size_t frame, uint32_t binding, uint32_t count,
                                  const std::array<vk::DescriptorImageInfo, 8>& infos);
    void writeSamplerBinding(size_t frame, uint32_t binding,
                             vk::DescriptorImageInfo info);
};

} // namespace RYRayTracing
