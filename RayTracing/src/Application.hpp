#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <chrono>
#include <vector>
#include <memory>
#include <array>

#include "Vertex.h"
#include "Camera.h"
#include "Transform.h"
#include "Model.h"
#include "Instance.h"
#include "Light.h"
#include "Scene.h"

namespace RYRayTracing {
    class WindowManager;
    class VulkanInstance;
    class VulkanDevice;
    class SwapChainManager;
    class Buffer;
    class ShaderModule;
    class RenderPassManager;
    class PipelineManager;
    class CommandManager;
    class Texture;
}

namespace RYRayTracing {

struct RTGlobalConstants {
    uint32_t sphereCount;
    uint32_t lightCount;
    uint32_t materialCount;
    float diffuseStrength;
    float specularStrength;
};

class Application {
public:
    Application();

    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();

private:
    std::unique_ptr<VulkanInstance> m_vulkanInstance;
    std::unique_ptr<WindowManager> m_windowManager;
    std::unique_ptr<VulkanDevice> m_vulkanDevice;
    std::unique_ptr<SwapChainManager> m_swapChainManager;
    std::unique_ptr<RenderPassManager> m_renderPassManager;
    std::unique_ptr<PipelineManager> m_pipelineManager;
    std::unique_ptr<CommandManager> m_commandManager;

    std::vector<Model> m_models;
    std::vector<Instance> m_instances;

    // ── Descriptor set layouts (organized by logical data group) ──
    /// Set 0 for "main" (Blinn-Phong) and "rt_main" (compute) pipelines.
    /// b0: CameraData UBO, b1: point lights SSBO
    vk::raii::DescriptorSetLayout m_perFrameSetLayout = nullptr;

    /// Shared combined-image-sampler layout used by:
    ///   - material textures (set 1 of "main" pipeline)
    ///   - fullscreen RT output display (set 0 of "fullscreen" pipeline)
    vk::raii::DescriptorSetLayout m_samplerSetLayout = nullptr;

    /// Set 1 for the "rt_main" compute pipeline.
    /// b0: sphere SSBO, b1: output storage image, b2: material SSBO
    vk::raii::DescriptorSetLayout m_rtResourceSetLayout = nullptr;

    // ── Single descriptor pool (ImGui has its own) ───────────────
    vk::raii::DescriptorPool m_descriptorPool = nullptr;
    vk::raii::DescriptorPool m_imguiPool = nullptr;

    // ── Descriptor sets ──────────────────────────────────────────
    /// Per-frame sets (one per frame-in-flight): camera + light UBO.
    std::vector<vk::raii::DescriptorSet> m_perFrameDescriptorSets;

    /// RT compute set: sphere SSBO + output storage image + material SSBO.
    std::vector<vk::raii::DescriptorSet> m_rtDescriptorSets;

    /// Fullscreen display set: RT output as combined image sampler.
    std::vector<vk::raii::DescriptorSet> m_fullscreenDescriptorSets;

    // ── Uniform buffers ──────────────────────────────────────────
    std::vector<std::unique_ptr<Buffer>> m_cameraUniformBuffers;
    std::vector<void*> m_mappedCameraUniformData;
    std::vector<std::unique_ptr<Buffer>> m_lightBuffers;
    int m_lightsDirty = m_framesInFlight;
    bool isLightsDirty() { bool positive = m_lightsDirty > 0; m_lightsDirty -= positive; return positive; }
    void setLightsDirty() { m_lightsDirty = m_framesInFlight; }
    int m_spheresDirty = m_framesInFlight;
    bool isSpheresDirty() { bool positive = m_spheresDirty > 0; m_spheresDirty -= positive; return positive; }
    void setSpheresDirty() { m_spheresDirty = m_framesInFlight; }

    // ── Shaders ──────────────────────────────────────────────────
    std::unique_ptr<ShaderModule> m_vertexShader;
    std::unique_ptr<ShaderModule> m_fragmentShader;
    std::unique_ptr<ShaderModule> m_rtComputeShader;
    std::unique_ptr<ShaderModule> m_fullscreenVertShader;
    std::unique_ptr<ShaderModule> m_fullscreenFragShader;

    // ── Pipeline layouts (derived from descriptor set layouts) ───
    vk::raii::PipelineLayout m_blinnPhongPipelineLayout = nullptr; // {perFrame, sampler}
    vk::raii::PipelineLayout m_rtPipelineLayout = nullptr;         // {perFrame, rtResource}
    vk::raii::PipelineLayout m_fullscreenPipelineLayout = nullptr; // {sampler}

    // ── Ray-tracing output resources ─────────────────────────────
    std::vector<vk::raii::Image> m_rtOutputImages;
    std::vector<vk::raii::DeviceMemory> m_rtOutputImagesMemory;
    std::vector<vk::raii::ImageView> m_rtOutputImageViews;
    vk::raii::Sampler m_rtOutputSampler = nullptr;

    // ── Scene primitives for RT ──────────────────────────────────
    static constexpr size_t kMaxSpheres = 32;
    static constexpr size_t kMaxMaterials = 32;
    std::vector<std::unique_ptr<Buffer>> m_sphereBuffers;
    std::vector<SphereData> m_spheres;
    std::vector<std::unique_ptr<Buffer>> m_materialBuffers;
    std::vector<MaterialData> m_materials;
    int m_materialsDirty = m_framesInFlight;
    bool isMaterialsDirty() { bool positive = m_materialsDirty > 0; m_materialsDirty -= positive; return positive; }
    void setMaterialsDirty() { m_materialsDirty = m_framesInFlight; }

    float m_diffuseStrength = 0.5f;
    float m_specularStrength = 1.0f;

    // ── Framebuffers & depth ──────────────────────────────────
    std::vector<vk::raii::Framebuffer> m_swapChainFramebuffers;
    vk::raii::Image m_depthImage = nullptr;
    vk::raii::DeviceMemory m_depthImageMemory = nullptr;
    vk::raii::ImageView m_depthImageView = nullptr;
    vk::Format m_depthFormat;

    // ── Command buffers ───────────────────────────────────────
    std::vector<vk::raii::CommandBuffer> m_commandBuffers;

    // ── Synchronization ────────────────────────────────────────
    std::vector<vk::raii::Semaphore> m_imageAvailableSemaphores;
    std::vector<vk::raii::Semaphore> m_renderFinishedSemaphores;
    std::vector<vk::raii::Fence> m_inFlightFences;
    std::vector<vk::Fence> m_imagesInFlight;

    size_t m_currentFrame;
    size_t m_framesInFlight;

    uint32_t m_windowWidth;
    uint32_t m_windowHeight;

    bool m_framebufferResized;
    bool m_imguiInitialized = false;

    // FPS tracking
    std::chrono::steady_clock::time_point m_fpsLastTime{};
    int m_fpsFrameCount = 0;
    float m_currentFps = 0.0f;

    Transform m_cameraTransform;
    SceneCamera m_camera;
    std::vector<PointLightData> m_pointLights;

    void createModels();
    void initVulkan();
    void initImGui();
    void initComponents();

    void initSpheres();

    void cleanup();
    void recreateSwapChain();
    void cleanupSwapChain();
    void cleanupSyncObjects();
    void cleanupRtResources();
    void cleanupRtSwapChainResources();

    void createInstance();
    void createDevice();
    void createSwapChain();
    void createRenderPass();

    void createDescriptorSetLayouts(); // all layouts in one place
    void createDescriptorPool();       // single pool covering all set types
    void createDescriptorSets();       // per-frame + material sets

    void createGraphicsPipeline();
    void createFramebuffers();
    void createCommandPool();

    void createUniformBuffers();
    void createUniformBuffersImpl(vk::DeviceSize bufferSize,
                                  std::vector<std::unique_ptr<Buffer>>& bufferOut,
                                  std::vector<void*>& mappedDataOut) const;
    void createLightBuffer();

    void createDepthResources();
    void createCommandBuffers();
    void createSyncObjects();

    // RT methods
    void createRtStorageImage();
    void createRtComputePipeline();
    void createRtResourceDescriptorSet();  // sphere SSBO + output image
    void createSphereBuffer();
    void createMaterialBuffer();
    void createFullscreenPipeline();
    void createFullscreenDescriptorSet();  // RT output as sampler

    void updateUniformBuffer(size_t currentFrame);
    void updateLightBuffer(size_t currentFrame);
    void updateSphereBuffer(size_t currentFrame);
    void updateMaterialBuffer(size_t currentFrame);

    void drawFrame();
    void mainLoop();

    vk::Format findDepthFormat(const std::vector<vk::Format>& candidates) const;
};

} // namespace RYRayTracing
