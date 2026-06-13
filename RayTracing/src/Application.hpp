// Application.hpp
#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <chrono>
#include <vector>
#include <memory>
#include <array>

#include "Camera.h"
#include "Transform.h"
#include "EditorUI.h"
#include "DescriptorManager.hpp"
#include "SceneManager.hpp"
#include "GeometryManager.hpp"

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
}

namespace RYRayTracing {

struct RTGlobalConstants {
    uint32_t sphereCount;
    uint32_t lightCount;
    uint32_t materialCount;
    uint32_t modelRefCount;
    float ambientStrength;
};

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();

private:
    // Core Vulkan — unique_ptr because construction order is externally constrained:
    // instance → window surface → device → swapchain must be created in that order
    // during initVulkan(), not in the constructor initializer list.
    std::unique_ptr<VulkanInstance>     m_vulkanInstance;
    std::unique_ptr<WindowManager>      m_windowManager;
    std::unique_ptr<VulkanDevice>       m_vulkanDevice;
    std::unique_ptr<SwapChainManager>   m_swapChainManager;
    std::unique_ptr<RenderPassManager>  m_renderPassManager;
    std::unique_ptr<PipelineManager>    m_pipelineManager;
    std::unique_ptr<CommandManager>     m_commandManager;

    size_t m_currentFrame;
    size_t m_framesInFlight;
    uint32_t m_windowWidth;
    uint32_t m_windowHeight;

    SceneManager      m_sceneManager;
    GeometryManager   m_geometryManager;
    DescriptorManager m_descriptorManager;

    // Shaders
    std::unique_ptr<ShaderModule> m_rtComputeShader;
    std::unique_ptr<ShaderModule> m_fullscreenVertShader;
    std::unique_ptr<ShaderModule> m_fullscreenFragShader;

    // Pipeline layouts
    vk::raii::PipelineLayout m_rtPipelineLayout = nullptr;
    vk::raii::PipelineLayout m_fullscreenPipelineLayout = nullptr;

    // RT output
    std::vector<vk::raii::Image> m_rtOutputImages;
    std::vector<vk::raii::DeviceMemory> m_rtOutputImagesMemory;
    std::vector<vk::raii::ImageView> m_rtOutputImageViews;
    vk::raii::Sampler m_rtOutputSampler = nullptr;

    // Camera UBO — persistent mapped memory requires raw void* storage
    std::vector<std::unique_ptr<Buffer>> m_cameraUniformBuffers;
    std::vector<void*> m_mappedCameraUniformData;

    std::unique_ptr<EditorUI> m_editorUI;

    // Framebuffers + depth
    std::vector<vk::raii::Framebuffer> m_swapChainFramebuffers;
    vk::raii::Image m_depthImage = nullptr;
    vk::raii::DeviceMemory m_depthImageMemory = nullptr;
    vk::raii::ImageView m_depthImageView = nullptr;
    vk::Format m_depthFormat;

    // Command buffers + sync
    std::vector<vk::raii::CommandBuffer> m_commandBuffers;
    std::vector<vk::raii::Semaphore> m_imageAvailableSemaphores;
    std::vector<vk::raii::Semaphore> m_renderFinishedSemaphores;
    std::vector<vk::raii::Fence> m_inFlightFences;
    std::vector<vk::Fence> m_imagesInFlight;

    bool m_framebufferResized;
    bool m_imguiInitialized = false;

    std::chrono::steady_clock::time_point m_fpsLastTime{};
    int m_fpsFrameCount = 0;
    float m_currentFps = 0.0f;

    // ── Initialization ──
    void initVulkan();
    void initImGui();
    void initComponents();

    // ── Cleanup ──
    void cleanup();
    void recreateSwapChain();
    void cleanupSwapChain();
    void cleanupSyncObjects();
    void cleanupRtResources();
    void cleanupRtSwapChainResources();

    // ── Vulkan sub-steps ──
    void createInstance();
    void createDevice();
    void createSwapChain();
    void createRenderPass();
    void createFramebuffers();
    void createCommandPool();
    void createUniformBuffers();
    void createUniformBuffersImpl(vk::DeviceSize bufferSize,
                                  std::vector<std::unique_ptr<Buffer>>& bufferOut,
                                  std::vector<void*>& mappedDataOut) const;
    void createDepthResources();
    void createCommandBuffers();
    void createSyncObjects();
    void createRtStorageImage();
    void createRtComputePipeline();
    void createFullscreenPipeline();

    // ── Descriptor helpers ──
    void setupPerFrameDescriptors();
    void setupRtResourceDescriptors();
    void setupFullscreenDescriptors();

    // ── Per-frame ──
    void updateUniformBuffer(size_t currentFrame);
    void drawFrame();
    bool beginFrame(uint32_t& outImageIndex);
    void recordComputePass();
    void recordGraphicsPass(uint32_t imageIndex);
    void submitFrame(uint32_t imageIndex);
    void updateFPS();
    void mainLoop();

    vk::Format findDepthFormat(const std::vector<vk::Format>& candidates) const;
};

} // namespace RYRayTracing
