#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <memory>
#include <array>

#include "Vertex.h"
#include "Camera.h"
#include "Transform.h"
#include "Model.h"
#include "Instance.h"
#include "Light.h"

namespace RYBlinnPhong {
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

namespace RYBlinnPhong {

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

    std::vector<std::unique_ptr<Buffer>> m_cameraUniformBuffers;
    std::vector<void*> m_mappedCameraUniformData;
    std::vector<std::unique_ptr<Buffer>> m_lightUniformBuffers; //
    std::vector<void*> m_mappedLightUniformData; //

    vk::raii::DescriptorSetLayout m_uboDescriptorSetLayout = nullptr;
    vk::raii::DescriptorSetLayout m_textureDescriptorSetLayout = nullptr;

    vk::raii::DescriptorPool m_descriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> m_uboDescriptorSets;

    vk::raii::DescriptorPool m_imguiPool = nullptr;

    std::unique_ptr<ShaderModule> m_vertexShader;
    std::unique_ptr<ShaderModule> m_fragmentShader;

    vk::raii::PipelineLayout m_pipelineLayout = nullptr;

    std::vector<vk::raii::Framebuffer> m_swapChainFramebuffers;

    vk::raii::Image m_depthImage = nullptr;
    vk::raii::DeviceMemory m_depthImageMemory = nullptr;
    vk::raii::ImageView m_depthImageView = nullptr;
    vk::Format m_depthFormat;

    std::vector<vk::raii::CommandBuffer> m_commandBuffers;

    std::vector<vk::raii::Semaphore> m_imageAvailableSemaphores;
    std::vector<vk::raii::Semaphore> m_renderFinishedSemaphores;
    std::vector<vk::raii::Fence> m_inFlightFences;
    std::vector<vk::Fence> m_imagesInFlight;

    size_t m_currentFrame;

    size_t m_framesInFlight;

    uint32_t m_windowWidth;
    uint32_t m_windowHeight;

    bool m_framebufferResized;

    Transform m_cameraTransform;
    SceneCamera m_camera;
    LightInfo m_lights;

    void createModels();
    void initVulkan();

    void initImGui();

    void initComponents();

    void cleanup();

    void recreateSwapChain();

    void cleanupSwapChain();

    void cleanupSyncObjects();

    void createInstance();

    void createDevice();

    void createSwapChain();

    void createRenderPass();

    void createGraphicsPipeline();

    void createFramebuffers();

    void createCommandPool();

    void createUniformBuffers();
    void createUniformBuffersImpl(vk::DeviceSize bufferSize, std::vector<std::unique_ptr<Buffer>>& bufferOut,
                                  std::vector<void*>& mappedDataOut) const;

    void createDepthResources();

    void createDescriptorSetLayout();

    void createDescriptorPool();

    void createDescriptorSets();

    void createCommandBuffers();

    void createSyncObjects();

    void updateUniformBuffer(size_t currentFrame);

    void drawFrame();

    void mainLoop();

    vk::Format findDepthFormat(const std::vector<vk::Format>& candidates) const;
};

} // namespace RYBlinnPhong
