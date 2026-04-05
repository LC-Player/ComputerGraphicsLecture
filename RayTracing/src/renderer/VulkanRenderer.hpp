#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include "core/Exception.hpp"
#include "core/Logger.hpp"

namespace RYRayTracing {

// Forward declarations
class VulkanDevice;
class SwapChainManager;
class RenderPassManager;
class PipelineManager;
class CommandManager;

/**
 * @brief Renderer configuration
 */
struct RendererConfig {
    uint32_t maxFramesInFlight = 2;
    VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB;
};

/**
 * @brief Frame data for synchronization
 */
struct FrameData {
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;
    VkCommandBuffer commandBuffer;
};

/**
 * @brief Vulkan renderer
 *
 * High-level renderer that coordinates all Vulkan components.
 */
class VulkanRenderer {
public:
    /**
     * @brief Construct a new VulkanRenderer object
     *
     * @param device Vulkan device
     * @param swapChain Swap chain manager
     * @param renderPass Render pass manager
     * @param pipeline Pipeline manager
     * @param commandManager Command manager
     * @param config Renderer configuration
     */
    VulkanRenderer(VulkanDevice* device, SwapChainManager* swapChain,
                   RenderPassManager* renderPass, PipelineManager* pipeline,
                   CommandManager* commandManager, const RendererConfig& config = RendererConfig());

    /**
     * @brief Destroy the VulkanRenderer object
     */
    ~VulkanRenderer();

    // Delete copy constructor and assignment operator
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    /**
     * @brief Initialize the renderer
     */
    void initialize();

    /**
     * @brief Cleanup the renderer
     */
    void cleanup();

    /**
     * @brief Recreate swap chain and related resources
     */
    void recreateSwapChain();

    /**
     * @brief Begin a new frame
     *
     * @return uint32_t Image index for rendering
     */
    uint32_t beginFrame();

    /**
     * @brief End the current frame
     *
     * @param imageIndex Image index
     */
    void endFrame(uint32_t imageIndex);

    /**
     * @brief Begin rendering commands
     *
     * @param imageIndex Image index
     */
    void beginRenderPass(uint32_t imageIndex);

    /**
     * @brief End rendering commands
     */
    void endRenderPass();

    /**
     * @brief Get current frame index
     *
     * @return size_t Current frame index
     */
    size_t getCurrentFrame() const { return currentFrame; }

    /**
     * @brief Get the number of frames in flight
     *
     * @return size_t Number of frames in flight
     */
    size_t getFramesInFlight() const { return framesInFlight; }

    /**
     * @brief Get current command buffer
     *
     * @return VkCommandBuffer Current command buffer
     */
    VkCommandBuffer getCurrentCommandBuffer() const;

    /**
     * @brief Check if framebuffer needs resize
     *
     * @return true if resize is needed, false otherwise
     */
    bool needsResize() const { return framebufferResized; }

    /**
     * @brief Mark framebuffer as resized
     */
    void markResized() { framebufferResized = true; }

private:
    VulkanDevice* device;
    SwapChainManager* swapChain;
    RenderPassManager* renderPass;
    PipelineManager* pipeline;
    CommandManager* commandManager;
    RendererConfig config;

    std::vector<FrameData> frames;
    std::vector<VkFence> imagesInFlight;
    std::vector<VkFramebuffer> framebuffers;

    size_t currentFrame;
    size_t framesInFlight;
    bool framebufferResized;
    bool initialized;

    /**
     * @brief Create framebuffers
     */
    void createFramebuffers();

    /**
     * @brief Cleanup framebuffers
     */
    void cleanupFramebuffers();

    /**
     * @brief Create synchronization objects
     */
    void createSyncObjects();

    /**
     * @brief Cleanup synchronization objects
     */
    void cleanupSyncObjects();

    /**
     * @brief Create command buffers for each frame
     */
    void createCommandBuffers();

    /**
     * @brief Get clear values for render pass
     *
     * @return std::vector<VkClearValue> Clear values
     */
    std::vector<VkClearValue> getClearValues() const;
};

} // namespace RYRayTracing
