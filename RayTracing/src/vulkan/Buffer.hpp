#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstring>
#include "core/Exception.hpp"
#include "core/Logger.hpp"

namespace RYRayTracing {

// Forward declaration
class VulkanDevice;

/**
 * @brief Buffer creation parameters
 */
struct BufferConfig {
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    VkMemoryPropertyFlags properties = 0;
    VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;
};

/**
 * @brief Vulkan buffer wrapper
 *
 * Manages the lifecycle of Vulkan buffers and their associated memory.
 */
class Buffer {
public:
    /**
     * @brief Construct a new Buffer object
     *
     * @param device Vulkan device
     * @param config Buffer configuration
     */
    Buffer(VulkanDevice* device, const BufferConfig& config);

    /**
     * @brief Destroy the Buffer object
     */
    ~Buffer();

    // Delete copy constructor and assignment operator
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    /**
     * @brief Move constructor
     */
    Buffer(Buffer&& other) noexcept;

    /**
     * @brief Move assignment operator
     */
    Buffer& operator=(Buffer&& other) noexcept;

    /**
     * @brief Get the buffer handle
     *
     * @return VkBuffer Buffer handle
     */
    VkBuffer get() const { return buffer; }

    /**
     * @brief Get the buffer size
     *
     * @return VkDeviceSize Buffer size in bytes
     */
    VkDeviceSize getSize() const { return size; }

    /**
     * @brief Get the buffer usage flags
     *
     * @return VkBufferUsageFlags Buffer usage flags
     */
    VkBufferUsageFlags getUsage() const { return usage; }

    /**
     * @brief Get the memory properties
     *
     * @return VkMemoryPropertyFlags Memory properties
     */
    VkMemoryPropertyFlags getProperties() const { return properties; }

    /**
     * @brief Map buffer memory for CPU access
     *
     * @param offset Memory offset
     * @param size Memory size (use VK_WHOLE_SIZE for entire buffer)
     * @return void* Mapped memory pointer
     * @throws VulkanException if mapping fails
     */
    void* map(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);

    /**
     * @brief Unmap buffer memory
     */
    void unmap();

    /**
     * @brief Copy data to buffer
     *
     * @param data Source data
     * @param size Data size
     * @param offset Buffer offset
     * @throws VulkanException if copy fails
     */
    void copyFrom(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

    /**
     * @brief Copy data from buffer
     *
     * @param data Destination data
     * @param size Data size
     * @param offset Buffer offset
     * @throws VulkanException if copy fails
     */
    void copyTo(void* data, VkDeviceSize size, VkDeviceSize offset = 0) const;

    /**
     * @brief Create a vertex buffer
     *
     * @param device Vulkan device
     * @param data Vertex data
     * @param size Data size
     * @return Buffer Vertex buffer
     */
    static Buffer createVertexBuffer(VulkanDevice* device, const void* data, VkDeviceSize size);

    /**
     * @brief Create an index buffer
     *
     * @param device Vulkan device
     * @param data Index data
     * @param size Data size
     * @return Buffer Index buffer
     */
    static Buffer createIndexBuffer(VulkanDevice* device, const void* data, VkDeviceSize size);

    /**
     * @brief Create a uniform buffer
     *
     * @param device Vulkan device
     * @param size Buffer size
     * @return Buffer Uniform buffer
     */
    static Buffer createUniformBuffer(VulkanDevice* device, VkDeviceSize size);

    /**
     * @brief Create a staging buffer
     *
     * @param device Vulkan device
     * @param size Buffer size
     * @return Buffer Staging buffer
     */
    static Buffer createStagingBuffer(VulkanDevice* device, VkDeviceSize size);

    /**
     * @brief Copy data from one buffer to another
     *
     * @param commandBuffer Command buffer to record the copy command
     * @param srcBuffer Source buffer
     * @param dstBuffer Destination buffer
     * @param size Size to copy
     * @param srcOffset Source offset
     * @param dstOffset Destination offset
     */
    static void copyBuffer(VkCommandBuffer commandBuffer,
                          VkBuffer srcBuffer, VkBuffer dstBuffer,
                          VkDeviceSize size,
                          VkDeviceSize srcOffset = 0,
                          VkDeviceSize dstOffset = 0);

private:
    VulkanDevice* device;
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize size;
    VkBufferUsageFlags usage;
    VkMemoryPropertyFlags properties;
    void* mappedMemory;
    bool isMapped;

    /**
     * @brief Create the Vulkan buffer
     *
     * @throws VulkanException if buffer creation fails
     */
    void createBuffer();

    /**
     * @brief Allocate and bind memory for the buffer
     *
     * @throws VulkanException if memory allocation fails
     */
    void allocateMemory();

    /**
     * @brief Find a suitable memory type
     *
     * @param typeFilter Memory type filter
     * @param properties Memory properties
     * @return uint32_t Memory type index
     * @throws VulkanException if no suitable memory type is found
     */
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
};

} // namespace RYRayTracing