#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <memory>

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
    std::unique_ptr<WindowManager> windowManager;
    std::unique_ptr<VulkanInstance> vulkanInstance;
    vk::raii::SurfaceKHR surface = nullptr;
    std::unique_ptr<VulkanDevice> vulkanDevice;
    std::unique_ptr<SwapChainManager> swapChainManager;
    std::unique_ptr<RenderPassManager> renderPassManager;
    std::unique_ptr<PipelineManager> pipelineManager;
    std::unique_ptr<CommandManager> commandManager;

    // Vertex buffer for triangle
    std::unique_ptr<Buffer> vertexBuffer;

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

    /**
     * @brief Initialize Vulkan components
     */
    void initVulkan();

    /**
     * @brief Cleanup Vulkan resources
     */
    void cleanup();

    /**
     * @brief Recreate swap chain when window is resized
     */
    void recreateSwapChain();

    /**
     * @brief Cleanup swap chain resources
     */
    void cleanupSwapChain();

    /**
     * @brief Cleanup sync objects
     */
    void cleanupSyncObjects();

    /**
     * @brief Create Vulkan instance
     */
    void createInstance();

    /**
     * @brief Create Vulkan device and queues
     */
    void createDevice();

    /**
     * @brief Create swap chain
     */
    void createSwapChain();

    /**
     * @brief Create render pass
     */
    void createRenderPass();

    /**
     * @brief Create graphics pipeline
     */
    void createGraphicsPipeline();

    /**
     * @brief Create framebuffers
     */
    void createFramebuffers();

    /**
     * @brief Create command pool
     */
    void createCommandPool();

    /**
     * @brief Create vertex buffer
     */
    void createVertexBuffer();

    /**
     * @brief Create command buffers
     */
    void createCommandBuffers();

    /**
     * @brief Create sync objects
     */
    void createSyncObjects();

    /**
     * @brief Draw a frame
     */
    void drawFrame();

    /**
     * @brief Main render loop
     */
    void mainLoop();

    /**
     * @brief Callback for window resize events
     */
    static void onWindowResize(int width, int height, void* userData);
};

} // namespace RYRayTracing
