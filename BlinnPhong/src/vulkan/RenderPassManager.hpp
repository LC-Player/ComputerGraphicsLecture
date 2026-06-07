#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include "core/Exception.hpp"
#include "core/Logger.hpp"

namespace RYBlinnPhong {

/**
 * @brief Render pass configuration
 */
struct RenderPassConfig {
    vk::Format colorFormat = vk::Format::eB8G8R8A8Srgb;
    vk::Format depthFormat = vk::Format::eUndefined; // No depth by default
    bool clearColors = true;
    bool clearDepth = true;
    vk::ImageLayout initialLayout = vk::ImageLayout::eUndefined;
    vk::ImageLayout finalLayout = vk::ImageLayout::ePresentSrcKHR;
};

/**
 * @brief Render pass manager using vk::raii
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
    RenderPassManager(vk::raii::Device& device, const RenderPassConfig& config = RenderPassConfig());

    /**
     * @brief Destroy the RenderPassManager object
     */
    ~RenderPassManager() = default;

    // Delete copy constructor and assignment operator
    RenderPassManager(const RenderPassManager&) = delete;
    RenderPassManager& operator=(const RenderPassManager&) = delete;

    /**
     * @brief Move constructor
     */
    RenderPassManager(RenderPassManager&& other) noexcept = default;

    /**
     * @brief Move assignment operator
     */
    RenderPassManager& operator=(RenderPassManager&& other) noexcept = default;

    /**
     * @brief Get the render pass handle
     *
     * @return vk::raii::RenderPass& Render pass
     */
    vk::raii::RenderPass& get() { return renderPass; }
    const vk::raii::RenderPass& get() const { return renderPass; }

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
    void begin(vk::raii::CommandBuffer& commandBuffer, vk::Framebuffer framebuffer,
               vk::Rect2D renderArea, const std::vector<vk::ClearValue>& clearValues = {});

    /**
     * @brief End the render pass
     *
     * @param commandBuffer Command buffer
     */
    void end(vk::raii::CommandBuffer& commandBuffer);

private:
    vk::raii::Device& device; // device reference
    vk::raii::RenderPass renderPass = nullptr;
    RenderPassConfig config;

    /**
     * @brief Create the render pass
     */
    void createRenderPass();
};

} // namespace RYBlinnPhong
