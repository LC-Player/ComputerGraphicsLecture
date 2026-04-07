#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
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
    vk::Format colorFormat = vk::Format::eB8G8R8A8Srgb;
};

/**
 * @brief Frame data for synchronization
 */
struct FrameData {
    vk::raii::Semaphore imageAvailableSemaphore = nullptr;
    vk::raii::Semaphore renderFinishedSemaphore = nullptr;
    vk::raii::Fence inFlightFence = nullptr;
    vk::raii::CommandBuffer commandBuffer = nullptr;
};

/**
 * @brief Vulkan renderer using vk::raii
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
    ~VulkanRenderer() = default;

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
     * @return vk::raii::CommandBuffer& Current command buffer
     */
    vk::raii::CommandBuffer& getCurrentCommandBuffer();

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
    std::vector<vk::Fence> imagesInFlight;
    std::vector<vk::raii::Framebuffer> framebuffers;

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
     * @return std::vector<vk::ClearValue> Clear values
     */
    std::vector<vk::ClearValue> getClearValues() const;
};

} // namespace RYRayTracing
