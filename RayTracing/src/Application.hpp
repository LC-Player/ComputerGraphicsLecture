#pragma once

#include <vulkan/vulkan.h>
#include <vector>

// Forward declarations
namespace RYRayTracing {
    class WindowManager;
    class VulkanInstance;
    class VulkanDevice;
    class SwapChainManager;
    class Buffer;
    class ShaderModule;
}

namespace RYRayTracing {

// Maximum number of frames that can be processed concurrently
// We'll set this to the swapchain image count at runtime
static constexpr int MAX_FRAMES_IN_FLIGHT = 0; // Will be set dynamically

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
    WindowManager* windowManager;
    VulkanInstance* vulkanInstance;
    VulkanDevice* vulkanDevice;
    SwapChainManager* swapChainManager;
    VkSurfaceKHR surface;

    // Vertex buffer for triangle
    Buffer* vertexBuffer;

    // Shaders
    ShaderModule* vertexShader;
    ShaderModule* fragmentShader;

    // Rendering resources
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;
    VkRenderPass renderPass;
    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkFramebuffer> swapChainFramebuffers;

    // Sync objects - one set per swapchain image
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight; // Track which fence is used for each swapchain image

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
     * @brief Internal graphics pipeline creation
     */
    void createGraphicsPipelineInternal();

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