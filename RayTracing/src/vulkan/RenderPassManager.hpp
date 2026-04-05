#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include "core/Exception.hpp"
#include "core/Logger.hpp"

namespace RYRayTracing {

/**
 * @brief Render pass configuration
 */
struct RenderPassConfig {
    VkFormat colorFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED; // No depth by default
    bool clearColors = true;
    bool clearDepth = true;
    VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
};

/**
 * @brief Render pass manager
 *
 * Manages Vulkan render pass creation and configuration.
 */
class RenderPassManager {
public:
    /**
     * @brief Construct a new RenderPassManager object
     *
     * @param device Vulkan logical device
     * @param config Render pass configuration
     */
    RenderPassManager(VkDevice device, const RenderPassConfig& config = RenderPassConfig());

    /**
     * @brief Destroy the RenderPassManager object
     */
    ~RenderPassManager();

    // Delete copy constructor and assignment operator
    RenderPassManager(const RenderPassManager&) = delete;
    RenderPassManager& operator=(const RenderPassManager&) = delete;

    /**
     * @brief Move constructor
     */
    RenderPassManager(RenderPassManager&& other) noexcept;

    /**
     * @brief Move assignment operator
     */
    RenderPassManager& operator=(RenderPassManager&& other) noexcept;

    /**
     * @brief Get the render pass handle
     *
     * @return VkRenderPass Render pass
     */
    VkRenderPass get() const { return renderPass; }

    /**
     * @brief Get the render pass configuration
     *
     * @return const RenderPassConfig& Render pass configuration
     */
    const RenderPassConfig& getConfig() const { return config; }

    /**
     * @brief Begin the render pass
     *
     * @param commandBuffer Command buffer
     * @param framebuffer Framebuffer to render to
     * @param renderArea Render area
     * @param clearValues Clear values (optional)
     */
    void begin(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer,
               VkRect2D renderArea, const std::vector<VkClearValue>& clearValues = {});

    /**
     * @brief End the render pass
     *
     * @param commandBuffer Command buffer
     */
    void end(VkCommandBuffer commandBuffer);

private:
    VkDevice device;
    VkRenderPass renderPass;
    RenderPassConfig config;
    bool initialized;

    /**
     * @brief Create the render pass
     */
    void createRenderPass();
};

} // namespace RYRayTracing
