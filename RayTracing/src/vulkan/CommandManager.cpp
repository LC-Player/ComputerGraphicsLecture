#include "CommandManager.hpp"

namespace RYRayTracing {

CommandManager::CommandManager(vk::raii::Device& device, const CommandPoolConfig& config)
    : device(&device), config(config) {
    createCommandPool();
    LOG_INFO("CommandManager initialized successfully");
}

void CommandManager::createCommandPool() {
    vk::CommandPoolCreateInfo poolInfo{
        config.flags,
        config.queueFamilyIndex
    };

    try {
        commandPool = device->createCommandPool(poolInfo);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to create command pool: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    LOG_DEBUG("Command pool created successfully");
}

vk::raii::CommandBuffer CommandManager::allocateCommandBuffer(vk::CommandBufferLevel level) {
    vk::CommandBufferAllocateInfo allocInfo{
        *commandPool,
        level,
        1
    };

    try {
        auto buffers = device->allocateCommandBuffers(allocInfo);
        return std::move(buffers[0]);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(),
                            std::string("Failed to allocate command buffer: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }
}

std::vector<vk::raii::CommandBuffer> CommandManager::allocateCommandBuffers(
    size_t count, vk::CommandBufferLevel level) {

    vk::CommandBufferAllocateInfo allocInfo{
        *commandPool,
        level,
        static_cast<uint32_t>(count)
    };

    try {
        auto buffers = device->allocateCommandBuffers(allocInfo);
        // Convert to vector of raii::CommandBuffer
        std::vector<vk::raii::CommandBuffer> result;
        for (auto& buf : buffers) {
            result.emplace_back(std::move(buf));
        }
        return result;
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(),
                            std::string("Failed to allocate command buffers: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }
}

void CommandManager::beginCommandBuffer(vk::raii::CommandBuffer& commandBuffer,
                                        vk::CommandBufferUsageFlags flags) {
    vk::CommandBufferBeginInfo beginInfo{
        flags
    };

    try {
        commandBuffer.begin(beginInfo);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(),
                            std::string("Failed to begin command buffer: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }
}

void CommandManager::endCommandBuffer(vk::raii::CommandBuffer& commandBuffer) {
    try {
        commandBuffer.end();
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(),
                            std::string("Failed to end command buffer: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }
}

vk::raii::CommandBuffer CommandManager::beginSingleTimeCommands() {
    auto commandBuffer = allocateCommandBuffer(vk::CommandBufferLevel::ePrimary);

    vk::CommandBufferBeginInfo beginInfo{
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    };

    try {
        commandBuffer.begin(beginInfo);
        return commandBuffer;
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(),
                            std::string("Failed to begin single time command buffer: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }
}

void CommandManager::endSingleTimeCommands(vk::raii::CommandBuffer& commandBuffer, vk::Queue queue) {
    try {
        commandBuffer.end();
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(),
                            std::string("Failed to end single time command buffer: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    vk::SubmitInfo submitInfo{
        {},
        {},
        *commandBuffer
    };

    try {
        queue.submit(submitInfo, nullptr);
        queue.waitIdle();
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(),
                            std::string("Failed to submit command buffer: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }
}

void CommandManager::resetCommandBuffer(vk::raii::CommandBuffer& commandBuffer,
                                        vk::CommandBufferResetFlags flags) {
    try {
        commandBuffer.reset(flags);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(),
                            std::string("Failed to reset command buffer: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }
}

void CommandManager::resetCommandPool(vk::CommandPoolResetFlags flags) {
    try {
        commandPool.reset(flags);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to reset command pool: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }
}

} // namespace RYRayTracing
