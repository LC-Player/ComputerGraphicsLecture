#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include "core/Exception.hpp"
#include "core/Logger.hpp"

namespace RYBlinnPhong {

/**
 * @brief Command pool configuration
 */
struct CommandPoolConfig {
    vk::CommandPoolCreateFlags flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    uint32_t queueFamilyIndex = 0;
};

/**
 * @brief Command manager using vk::raii
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
    CommandManager(vk::raii::Device& device, const CommandPoolConfig& config = CommandPoolConfig());

    /**
     * @brief Destroy the CommandManager object
     */
    ~CommandManager() = default;

    // Delete copy constructor and assignment operator
    CommandManager(const CommandManager&) = delete;
    CommandManager& operator=(const CommandManager&) = delete;

    /**
     * @brief Move constructor
     */
    CommandManager(CommandManager&& other) noexcept = default;

    /**
     * @brief Move assignment operator
     */
    CommandManager& operator=(CommandManager&& other) noexcept = default;

    /**
     * @brief Get the command pool handle
     *
     * @return vk::raii::CommandPool& Command pool
     */
    vk::raii::CommandPool& getPool() { return commandPool; }
    const vk::raii::CommandPool& getPool() const { return commandPool; }

    /**
     * @brief Allocate a single command buffer
     *
     * @param level Command buffer level
     * @return vk::raii::CommandBuffer Command buffer
     */
    vk::raii::CommandBuffer allocateCommandBuffer(vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);

    /**
     * @brief Allocate multiple command buffers
     *
     * @param count Number of command buffers to allocate
     * @param level Command buffer level
     * @return std::vector<vk::raii::CommandBuffer> Command buffers
     */
    std::vector<vk::raii::CommandBuffer> allocateCommandBuffers(
        size_t count, vk::CommandBufferLevel level = vk::CommandBufferLevel::ePrimary);

    /**
     * @brief Begin a command buffer
     *
     * @param commandBuffer Command buffer
     * @param flags Usage flags
     */
    void beginCommandBuffer(vk::raii::CommandBuffer& commandBuffer,
                            vk::CommandBufferUsageFlags flags = {});

    /**
     * @brief End a command buffer
     *
     * @param commandBuffer Command buffer
     */
    void endCommandBuffer(vk::raii::CommandBuffer& commandBuffer);

    /**
     * @brief Begin a single time command buffer
     *
     * @return vk::raii::CommandBuffer Command buffer
     */
    vk::raii::CommandBuffer beginSingleTimeCommands();

    /**
     * @brief End and submit a single time command buffer
     *
     * @param commandBuffer Command buffer
     * @param queue Queue to submit to
     */
    void endSingleTimeCommands(vk::raii::CommandBuffer& commandBuffer, vk::Queue queue);

    /**
     * @brief Reset command buffer
     *
     * @param commandBuffer Command buffer
     * @param flags Reset flags
     */
    void resetCommandBuffer(vk::raii::CommandBuffer& commandBuffer,
                            vk::CommandBufferResetFlags flags = {});

    /**
     * @brief Reset command pool
     *
     * @param flags Reset flags
     */
    void resetCommandPool(vk::CommandPoolResetFlags flags = {});

private:
    vk::raii::Device& device;
    vk::raii::CommandPool commandPool = nullptr;
    CommandPoolConfig config;

    /**
     * @brief Create the command pool
     */
    void createCommandPool();
};

} // namespace RYBlinnPhong
