#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
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
    vk::DeviceSize size = 0;
    vk::BufferUsageFlags usage = {};
    vk::MemoryPropertyFlags properties = {};
    vk::SharingMode sharingMode = vk::SharingMode::eExclusive;
};

/**
 * @brief Vulkan buffer wrapper using vk::raii
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
    ~Buffer() = default;

    // Delete copy constructor and assignment operator
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    /**
     * @brief Move constructor
     */
    Buffer(Buffer&& other) noexcept = default;

    /**
     * @brief Move assignment operator
     */
    Buffer& operator=(Buffer&& other) noexcept = default;

    /**
     * @brief Get the buffer handle
     *
     * @return vk::raii::Buffer& Buffer handle
     */
    vk::raii::Buffer& get() { return buffer; }
    const vk::raii::Buffer& get() const { return buffer; }

    /**
     * @brief Get the buffer size
     *
     * @return vk::DeviceSize Buffer size in bytes
     */
    vk::DeviceSize getSize() const { return size; }

    /**
     * @brief Get the buffer usage flags
     *
     * @return vk::BufferUsageFlags Buffer usage flags
     */
    vk::BufferUsageFlags getUsage() const { return usage; }

    /**
     * @brief Get the memory properties
     *
     * @return vk::MemoryPropertyFlags Memory properties
     */
    vk::MemoryPropertyFlags getProperties() const { return properties; }

    /**
     * @brief Map buffer memory for CPU access
     *
     * @param offset Memory offset
     * @param size Memory size (use VK_WHOLE_SIZE for entire buffer)
     * @return void* Mapped memory pointer
     * @throws VulkanException if mapping fails
     */
    void* map(vk::DeviceSize offset = 0, vk::DeviceSize size = VK_WHOLE_SIZE);

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
    void copyFrom(const void* data, vk::DeviceSize size, vk::DeviceSize offset = 0);

    /**
     * @brief Copy data from buffer
     *
     * @param data Destination data
     * @param size Data size
     * @param offset Buffer offset
     * @throws VulkanException if copy fails
     */
    void copyTo(void* data, vk::DeviceSize size, vk::DeviceSize offset = 0) const;

    /**
     * @brief Create a vertex buffer
     *
     * @param device Vulkan device
     * @param data Vertex data
     * @param size Data size
     * @return Buffer Vertex buffer
     */
    static Buffer createVertexBuffer(VulkanDevice* device, const void* data, vk::DeviceSize size);

    /**
     * @brief Create an index buffer
     *
     * @param device Vulkan device
     * @param data Index data
     * @param size Data size
     * @return Buffer Index buffer
     */
    static Buffer createIndexBuffer(VulkanDevice* device, const void* data, vk::DeviceSize size);

    /**
     * @brief Create a uniform buffer
     *
     * @param device Vulkan device
     * @param size Buffer size
     * @return Buffer Uniform buffer
     */
    static Buffer createUniformBuffer(VulkanDevice* device, vk::DeviceSize size);

    /**
     * @brief Create a staging buffer
     *
     * @param device Vulkan device
     * @param size Buffer size
     * @return Buffer Staging buffer
     */
    static Buffer createStagingBuffer(VulkanDevice* device, vk::DeviceSize size);

    /**
     * @brief Create a buffer with specified parameters
     *
     * @param device Vulkan device
     * @param size Buffer size
     * @param usage Buffer usage flags
     * @param properties Memory properties
     * @return Buffer Created buffer
     */
    static Buffer createBuffer(VulkanDevice* device, vk::DeviceSize size,
                              vk::BufferUsageFlags usage,
                              vk::MemoryPropertyFlags properties);

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
    static void copyBuffer(vk::raii::CommandBuffer& commandBuffer,
                          vk::Buffer srcBuffer, vk::Buffer dstBuffer,
                          vk::DeviceSize size,
                          vk::DeviceSize srcOffset = 0,
                          vk::DeviceSize dstOffset = 0);

private:
    VulkanDevice* device = nullptr;  // Non-owning pointer, lifetime managed externally
    vk::raii::Buffer buffer = nullptr;
    vk::raii::DeviceMemory m_memory = nullptr;
    vk::DeviceSize size;
    vk::BufferUsageFlags usage;
    vk::MemoryPropertyFlags properties;
    void* mappedMemory;
    bool isMapped;

    /**
     * @brief Create the Vulkan buffer
     *
     * @throws VulkanException if buffer creation fails
     */
    void implementCreateBuffer();

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
    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;

    /**
     * @brief Create a device-local buffer with staging
     *
     * @param device Vulkan device
     * @param data Data to copy
     * @param size Data size
     * @param usageFlag Buffer usage flag (eVertexBuffer or eIndexBuffer)
     * @return Buffer Device-local buffer
     */
    static Buffer createDeviceLocalBuffer(VulkanDevice* device, const void* data,
                                          vk::DeviceSize size,
                                          vk::BufferUsageFlagBits usageFlag);
};

} // namespace RYRayTracing
