#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <memory>
#include <array>

#include "type.h"
#include "Camera.h"
#include "Transform.h"

// Forward declarations
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

/**
 * @brief Main application class for rendering a triangle
 *
 * This class creates a window, initializes Vulkan, and renders a simple triangle.
 */
class Application {
public:
    /**
     * @brief Construct a new Application object
     */
    Application();

    /**
     * @brief Destroy the Application object
     */
    ~Application();

    // Delete copy constructor and assignment operator
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /**
     * @brief Run the application
     *
     * This is the main entry point for the application logic.
     * It initializes all components, runs the main loop, and cleans up.
     */
    void run();

private:
    // Vulkan objects
    std::unique_ptr<VulkanInstance> vulkanInstance;
    std::unique_ptr<WindowManager> windowManager;
    std::unique_ptr<VulkanDevice> vulkanDevice;
    std::unique_ptr<SwapChainManager> swapChainManager;
    std::unique_ptr<RenderPassManager> renderPassManager;
    std::unique_ptr<PipelineManager> pipelineManager;
    std::unique_ptr<CommandManager> commandManager;

    // Vertex buffer for triangle
    std::unique_ptr<Buffer> vertexBuffer;

    // Index buffer for triangle
    std::unique_ptr<Buffer> indexBuffer;

    // Instance buffers for instanced rendering
    std::vector<std::unique_ptr<Buffer>> instanceBuffers;
    std::vector<void*> mappedInstanceData;

    // Uniform buffers for camera data
    std::vector<std::unique_ptr<Buffer>> uniformBuffers;
    std::vector<void*> mappedUniformData;

    // Descriptor set layout and pool
    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::DescriptorPool descriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> descriptorSets;

    // ImGui descriptor pool
    vk::raii::DescriptorPool imguiPool = nullptr;

    // Shaders
    std::unique_ptr<ShaderModule> vertexShader;
    std::unique_ptr<ShaderModule> fragmentShader;

    // Pipeline layout
    vk::raii::PipelineLayout pipelineLayout = nullptr;

    // Framebuffers
    std::vector<vk::raii::Framebuffer> swapChainFramebuffers;

    // Command buffers
    std::vector<vk::raii::CommandBuffer> commandBuffers;

    // Sync objects - one set per swapchain image
    std::vector<vk::raii::Semaphore> imageAvailableSemaphores;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
    std::vector<vk::raii::Fence> inFlightFences;
    std::vector<vk::Fence> imagesInFlight; // Track which fence is used for each swapchain image

    // Current frame index
    size_t currentFrame;

    // Actual number of frames in flight (equals swapchain image count)
    size_t framesInFlight;

    // Window dimensions
    uint32_t windowWidth;
    uint32_t windowHeight;

    // Flag to track if window was resized
    bool framebufferResized;

    // Quad instance data
    std::array<QuadInstanceData, 2> quadInstances;

    // Transform data for two quads and camera
    Transform transform1;
    Transform transform2;
    Transform cameraTransform;
    SceneCamera camera;

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

    void createVertexBuffer();

    void createIndexBuffer();

    void createInstanceBuffers();

    void createUniformBuffers();

    void createDescriptorSetLayout();

    void createDescriptorPool();

    void createDescriptorSets();

    void createCommandBuffers();

    void createSyncObjects();

    void updateUniformBuffer(size_t currentFrame);

    void updateInstanceBuffer(size_t currentFrame);

    void drawFrame();

    void mainLoop();
};

} // namespace RYRayTracing
