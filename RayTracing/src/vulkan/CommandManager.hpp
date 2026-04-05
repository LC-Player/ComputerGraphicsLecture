#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include "core/Exception.hpp"
#include "core/Logger.hpp"

namespace RYRayTracing {

/**
 * @brief Command pool configuration
 */
struct CommandPoolConfig {
    VkCommandPoolCreateFlags flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    uint32_t queueFamilyIndex = 0;
};

/**
 * @brief Command manager
 *
 * Manages Vulkan command pools and command buffers.
 */
class CommandManager {
public:
    /**
     * @brief Construct a new CommandManager object
     *
     * @param device Vulkan logical device
     * @param config Command pool configuration
     */
    CommandManager(VkDevice device, const CommandPoolConfig& config = CommandPoolConfig());

    /**
     * @brief Destroy the CommandManager object
     */
    ~CommandManager();

    // Delete copy constructor and assignment operator
    CommandManager(const CommandManager&) = delete;
    CommandManager& operator=(const CommandManager&) = delete;

    /**
     * @brief Move constructor
     */
    CommandManager(CommandManager&& other) noexcept;

    /**
     * @brief Move assignment operator
     */
    CommandManager& operator=(CommandManager&& other) noexcept;

    /**
     * @brief Get the command pool handle
     *
     * @return VkCommandPool Command pool
     */
    VkCommandPool getPool() const { return commandPool; }

    /**
     * @brief Allocate a single command buffer
     *
     * @param level Command buffer level
     * @return VkCommandBuffer Command buffer
     */
    VkCommandBuffer allocateCommandBuffer(VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    /**
     * @brief Allocate multiple command buffers
     *
     * @param count Number of command buffers to allocate
     * @param level Command buffer level
     * @return std::vector<VkCommandBuffer> Command buffers
     */
    std::vector<VkCommandBuffer> allocateCommandBuffers(
        size_t count, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    /**
     * @brief Free a command buffer
     *
     * @param commandBuffer Command buffer to free
     */
    void freeCommandBuffer(VkCommandBuffer commandBuffer);

    /**
     * @brief Free multiple command buffers
     *
     * @param commandBuffers Command buffers to free
     */
    void freeCommandBuffers(const std::vector<VkCommandBuffer>& commandBuffers);

    /**
     * @brief Begin a command buffer
     *
     * @param commandBuffer Command buffer
     * @param flags Usage flags
     */
    void beginCommandBuffer(VkCommandBuffer commandBuffer,
                            VkCommandBufferUsageFlags flags = 0);

    /**
     * @brief End a command buffer
     *
     * @param commandBuffer Command buffer
     */
    void endCommandBuffer(VkCommandBuffer commandBuffer);

    /**
     * @brief Begin a single time command buffer
     *
     * @return VkCommandBuffer Command buffer
     */
    VkCommandBuffer beginSingleTimeCommands();

    /**
     * @brief End and submit a single time command buffer
     *
     * @param commandBuffer Command buffer
     * @param queue Queue to submit to
     */
    void endSingleTimeCommands(VkCommandBuffer commandBuffer, VkQueue queue);

    /**
     * @brief Reset command buffer
     *
     * @param commandBuffer Command buffer
     * @param flags Reset flags
     */
    void resetCommandBuffer(VkCommandBuffer commandBuffer,
                            VkCommandBufferResetFlags flags = 0);

    /**
     * @brief Reset command pool
     *
     * @param flags Reset flags
     */
    void resetCommandPool(VkCommandPoolResetFlags flags = 0);

private:
    VkDevice device;
    VkCommandPool commandPool;
    CommandPoolConfig config;
    bool initialized;

    /**
     * @brief Create the command pool
     */
    void createCommandPool();
};

} // namespace RYRayTracing
