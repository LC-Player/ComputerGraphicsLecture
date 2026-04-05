#include "CommandManager.hpp"

namespace RYRayTracing {

CommandManager::CommandManager(VkDevice device, const CommandPoolConfig& config)
    : device(device), commandPool(VK_NULL_HANDLE), config(config), initialized(false) {
    createCommandPool();
    initialized = true;
    Logger::info("CommandManager initialized successfully");
}

CommandManager::~CommandManager() {
    if (initialized && commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, commandPool, nullptr);
        Logger::info("CommandManager destroyed");
    }
}

CommandManager::CommandManager(CommandManager&& other) noexcept
    : device(other.device), commandPool(other.commandPool),
      config(other.config), initialized(other.initialized) {
    other.commandPool = VK_NULL_HANDLE;
    other.initialized = false;
}

CommandManager& CommandManager::operator=(CommandManager&& other) noexcept {
    if (this != &other) {
        if (initialized && commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, commandPool, nullptr);
        }

        device = other.device;
        commandPool = other.commandPool;
        config = other.config;
        initialized = other.initialized;

        other.commandPool = VK_NULL_HANDLE;
        other.initialized = false;
    }
    return *this;
}

void CommandManager::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = config.flags;
    poolInfo.queueFamilyIndex = config.queueFamilyIndex;

    VkResult result = vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);
    if (result != VK_SUCCESS) {
        throw VulkanException(result, "Failed to create command pool", __FUNCTION__, __FILE__, __LINE__);
    }

    Logger::debug("Command pool created successfully");
}

VkCommandBuffer CommandManager::allocateCommandBuffer(VkCommandBufferLevel level) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = level;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VkResult result = vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
    if (result != VK_SUCCESS) {
        throw VulkanException(result, "Failed to allocate command buffer", __FUNCTION__, __FILE__, __LINE__);
    }

    return commandBuffer;
}

std::vector<VkCommandBuffer> CommandManager::allocateCommandBuffers(
    size_t count, VkCommandBufferLevel level) {

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = level;
    allocInfo.commandBufferCount = static_cast<uint32_t>(count);

    std::vector<VkCommandBuffer> commandBuffers(count);
    VkResult result = vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data());
    if (result != VK_SUCCESS) {
        throw VulkanException(result, "Failed to allocate command buffers", __FUNCTION__, __FILE__, __LINE__);
    }

    return commandBuffers;
}

void CommandManager::freeCommandBuffer(VkCommandBuffer commandBuffer) {
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void CommandManager::freeCommandBuffers(const std::vector<VkCommandBuffer>& commandBuffers) {
    vkFreeCommandBuffers(device, commandPool,
                         static_cast<uint32_t>(commandBuffers.size()),
                         commandBuffers.data());
}

void CommandManager::beginCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferUsageFlags flags) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = flags;

    VkResult result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        throw VulkanException(result, "Failed to begin command buffer", __FUNCTION__, __FILE__, __LINE__);
    }
}

void CommandManager::endCommandBuffer(VkCommandBuffer commandBuffer) {
    VkResult result = vkEndCommandBuffer(commandBuffer);
    if (result != VK_SUCCESS) {
        throw VulkanException(result, "Failed to end command buffer", __FUNCTION__, __FILE__, __LINE__);
    }
}

VkCommandBuffer CommandManager::beginSingleTimeCommands() {
    VkCommandBuffer commandBuffer = allocateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        freeCommandBuffer(commandBuffer);
        throw VulkanException(result, "Failed to begin single time command buffer", __FUNCTION__, __FILE__, __LINE__);
    }

    return commandBuffer;
}

void CommandManager::endSingleTimeCommands(VkCommandBuffer commandBuffer, VkQueue queue) {
    VkResult result = vkEndCommandBuffer(commandBuffer);
    if (result != VK_SUCCESS) {
        freeCommandBuffer(commandBuffer);
        throw VulkanException(result, "Failed to end single time command buffer", __FUNCTION__, __FILE__, __LINE__);
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    result = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        freeCommandBuffer(commandBuffer);
        throw VulkanException(result, "Failed to submit command buffer", __FUNCTION__, __FILE__, __LINE__);
    }

    vkQueueWaitIdle(queue);
    freeCommandBuffer(commandBuffer);
}

void CommandManager::resetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) {
    VkResult result = vkResetCommandBuffer(commandBuffer, flags);
    if (result != VK_SUCCESS) {
        throw VulkanException(result, "Failed to reset command buffer", __FUNCTION__, __FILE__, __LINE__);
    }
}

void CommandManager::resetCommandPool(VkCommandPoolResetFlags flags) {
    VkResult result = vkResetCommandPool(device, commandPool, flags);
    if (result != VK_SUCCESS) {
        throw VulkanException(result, "Failed to reset command pool", __FUNCTION__, __FILE__, __LINE__);
    }
}

} // namespace RYRayTracing
