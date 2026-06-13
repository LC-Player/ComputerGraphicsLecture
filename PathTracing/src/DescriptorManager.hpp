// DescriptorManager.hpp
#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <array>
#include <cstdint>

namespace RYRayTracing {

// Aggregates buffer/image references for per-frame and scene descriptor set writes.
// All pointer members are non-owning. Lifetime: data must outlive create*Sets() calls.
struct PtDescriptorBindings {
    // ── Per-frame set (set 1) ──
    const std::vector<vk::Buffer>* cameraBuffers = nullptr;
    const std::vector<vk::Buffer>* lightBuffers = nullptr;
    const std::vector<vk::ImageView>* rtImageViews = nullptr;
    vk::Buffer instanceDataBuffer = nullptr;       // single, shared across frames
    vk::ImageView accumImageView = nullptr;         // single, shared across frames

    // ── Scene set (set 2) ──
    const std::vector<vk::Buffer>* materialBuffers = nullptr;
    const std::vector<vk::Buffer>* vertexBuffers = nullptr;
    const std::vector<vk::Buffer>* indexBuffers = nullptr;
    const std::vector<vk::Buffer>* modelRefBuffers = nullptr;
    uint32_t maxTextures = 0;
    const std::vector<vk::ImageView>* textureViews = nullptr;
    vk::ImageView dummyTextureView = nullptr;
    vk::Sampler textureSampler = nullptr;
    vk::DescriptorImageInfo envmapInfo{};
};

class DescriptorManager {
public:
    DescriptorManager() = default;
    void init(vk::raii::Device& device) { m_device = &device; }

    void createLayouts(uint32_t maxTextures = 8);

    void createDescriptorPool(uint32_t framesInFlight, uint32_t maxTextures);
    void createImGuiPool();

    // TLAS sets — only allocated here; TLAS handle is written per-frame in drawFrame
    void createTLASSets(uint32_t framesInFlight);

    // Per-frame set 1: camera UBO(b0), lights SSBO(b1), output image(u2), instance data(b3), accum image(u4)
    void createPerFrameSets(uint32_t framesInFlight, const PtDescriptorBindings& b);

    // Scene set 2: materials(b0), vertices(b1), indices(b2), modelRefs(b3), textures(t4), envmap(t5)
    void createSceneSets(uint32_t framesInFlight, const PtDescriptorBindings& b);

    void createFullscreenSets(uint32_t framesInFlight,
                              const std::vector<vk::ImageView>& rtImageViews,
                              vk::Sampler rtSampler);

    // Swapchain-recreation image rebinding
    void updatePerFrameImageBindings(size_t frame, vk::ImageView rtView, vk::ImageView accumView);
    void updateFullscreenBinding(size_t frame, vk::ImageView newView, vk::Sampler sampler);

    void writeTLASBinding(size_t frame, vk::AccelerationStructureKHR tlas);

    // Accessors
    const vk::DescriptorSetLayout& tlasLayout()     const { return *m_tlasSetLayout; }
    const vk::DescriptorSetLayout& perFrameLayout() const { return *m_perFrameSetLayout; }
    const vk::DescriptorSetLayout& sceneLayout()    const { return *m_sceneSetLayout; }
    const vk::DescriptorSetLayout& samplerLayout()  const { return *m_samplerSetLayout; }

    const std::vector<vk::raii::DescriptorSet>& tlasSets()        const { return m_tlasSets; }
    const std::vector<vk::raii::DescriptorSet>& perFrameSets()    const { return m_perFrameSets; }
    const std::vector<vk::raii::DescriptorSet>& sceneSets()       const { return m_sceneSets; }
    const std::vector<vk::raii::DescriptorSet>& fullscreenSets()  const { return m_fullscreenSets; }

    vk::DescriptorPool imguiPool() const { return *m_imguiPool; }

private:
    // Non-owning. Set via init() before any create*() call.
    vk::raii::Device* m_device = nullptr;

    vk::raii::DescriptorSetLayout m_tlasSetLayout = nullptr;
    vk::raii::DescriptorSetLayout m_perFrameSetLayout = nullptr;
    vk::raii::DescriptorSetLayout m_sceneSetLayout = nullptr;
    vk::raii::DescriptorSetLayout m_samplerSetLayout = nullptr;

    vk::raii::DescriptorPool m_descriptorPool = nullptr;
    vk::raii::DescriptorPool m_imguiPool = nullptr;

    std::vector<vk::raii::DescriptorSet> m_tlasSets;
    std::vector<vk::raii::DescriptorSet> m_perFrameSets;
    std::vector<vk::raii::DescriptorSet> m_sceneSets;
    std::vector<vk::raii::DescriptorSet> m_fullscreenSets;

    void createTLASLayout();
    void createPerFrameLayout();
    void createSceneLayout(uint32_t maxTextures);
    void createSamplerLayout();
};

} // namespace RYRayTracing
