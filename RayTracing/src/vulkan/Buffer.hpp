// Buffer.hpp
#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <cstring>
#include "core/Exception.hpp"
#include "core/Logger.hpp"

namespace RYRayTracing {

class VulkanDevice;

struct BufferConfig {
    vk::DeviceSize size = 0;
    vk::BufferUsageFlags usage = {};
    vk::MemoryPropertyFlags properties = {};
    vk::SharingMode sharingMode = vk::SharingMode::eExclusive;
};

class Buffer {
public:
    Buffer(VulkanDevice* device, const BufferConfig& config);
    ~Buffer() = default;

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept = default;
    Buffer& operator=(Buffer&& other) noexcept = default;

    vk::raii::Buffer& get() { return buffer; }
    const vk::raii::Buffer& get() const { return buffer; }

    vk::DeviceSize getSize() const { return size; }
    vk::BufferUsageFlags getUsage() const { return usage; }
    vk::MemoryPropertyFlags getProperties() const { return properties; }

    void* map(vk::DeviceSize offset = 0, vk::DeviceSize size = VK_WHOLE_SIZE);
    void unmap();
    void copyFrom(const void* data, vk::DeviceSize size, vk::DeviceSize offset = 0);
    void copyTo(void* data, vk::DeviceSize size, vk::DeviceSize offset = 0) const;

    static Buffer createVertexBuffer(VulkanDevice* device, const void* data, vk::DeviceSize size);
    static Buffer createIndexBuffer(VulkanDevice* device, const void* data, vk::DeviceSize size);
    static Buffer createUniformBuffer(VulkanDevice* device, vk::DeviceSize size);
    static Buffer createStagingBuffer(VulkanDevice* device, vk::DeviceSize size);
    static Buffer createBuffer(VulkanDevice* device, vk::DeviceSize size,
                               vk::BufferUsageFlags usage,
                               vk::MemoryPropertyFlags properties);

    static void copyBuffer(vk::raii::CommandBuffer& commandBuffer,
                           vk::Buffer srcBuffer, vk::Buffer dstBuffer,
                           vk::DeviceSize size,
                           vk::DeviceSize srcOffset = 0,
                           vk::DeviceSize dstOffset = 0);

private:
    // Non-owning. Buffer is movable (=default move ctor), so a reference cannot be
    // rebound by move assignment. Lifetime: VukanDevice outlives all Buffer instances.
    VulkanDevice* device = nullptr;

    vk::raii::Buffer buffer = nullptr;
    vk::raii::DeviceMemory m_memory = nullptr;
    vk::DeviceSize size;
    vk::BufferUsageFlags usage;
    vk::MemoryPropertyFlags properties;
    void* mappedMemory;
    bool isMapped;

    void implementCreateBuffer();
    void allocateMemory();
    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;

    static Buffer createDeviceLocalBuffer(VulkanDevice* device, const void* data,
                                          vk::DeviceSize size,
                                          vk::BufferUsageFlagBits usageFlag);
};

} // namespace RYRayTracing
