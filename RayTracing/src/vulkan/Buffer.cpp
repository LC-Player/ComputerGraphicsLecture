#include "Buffer.hpp"
#include "VulkanDevice.hpp"
#include <cstring>

namespace RYRayTracing {

Buffer::Buffer(VulkanDevice* device, const BufferConfig& config)
    : device(device)
    , size(config.size)
    , usage(config.usage)
    , properties(config.properties)
    , mappedMemory(nullptr)
    , isMapped(false) {

    if (size == 0) {
        throw VulkanException(vk::Result::eErrorInitializationFailed,
                            "Buffer size cannot be zero",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    LOG_DEBUG("Creating buffer: size=" + std::to_string(size) +
              ", usage=" + std::to_string(static_cast<uint32_t>(usage)) +
              ", properties=" + std::to_string(static_cast<uint32_t>(properties)));

    implementCreateBuffer();
    allocateMemory();

    LOG_INFO("Buffer created successfully");
}

void* Buffer::map(vk::DeviceSize offset, vk::DeviceSize size) {
    if (isMapped) {
        LOG_WARNING("Buffer is already mapped");
        return mappedMemory;
    }

    vk::DeviceSize mapSize = (size == VK_WHOLE_SIZE) ? this->size : size;

    try {
        mappedMemory = m_memory.mapMemory(offset, mapSize);
        isMapped = true;
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to map buffer memory: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    LOG_DEBUG("Buffer memory mapped: offset=" + std::to_string(offset) +
              ", size=" + std::to_string(mapSize));

    return mappedMemory;
}

void Buffer::unmap() {
    if (!isMapped) {
        LOG_WARNING("Buffer is not mapped");
        return;
    }

    m_memory.unmapMemory();
    mappedMemory = nullptr;
    isMapped = false;

    LOG_DEBUG("Buffer memory unmapped");
}

void Buffer::copyFrom(const void* data, vk::DeviceSize size, vk::DeviceSize offset) {
    if (size == 0) {
        LOG_WARNING("Attempting to copy zero bytes");
        return;
    }

    if (offset + size > this->size) {
        throw VulkanException(vk::Result::eErrorOutOfDeviceMemory,
                            "Copy exceeds buffer size",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Map buffer memory
    void* mappedData = map(offset, size);

    // Copy data
    memcpy(mappedData, data, size);

    // If memory is not host coherent, flush the memory
    if (!(properties & vk::MemoryPropertyFlagBits::eHostCoherent)) {
        vk::MappedMemoryRange memoryRange{
            *m_memory,
            offset,
            size
        };

        try {
            device->get().flushMappedMemoryRanges(memoryRange);
        } catch (const vk::SystemError& e) {
            throw VulkanException(e.code(), std::string("Failed to flush mapped memory: ") + e.what(),
                                __FUNCTION__, __FILE__, __LINE__);
        }
    }

    // Unmap buffer memory
    unmap();

    LOG_DEBUG("Copied " + std::to_string(size) + " bytes to buffer at offset " + std::to_string(offset));
}

void Buffer::copyTo(void* data, vk::DeviceSize size, vk::DeviceSize offset) const {
    if (size == 0) {
        LOG_WARNING("Attempting to copy zero bytes");
        return;
    }

    if (offset + size > this->size) {
        throw VulkanException(vk::Result::eErrorOutOfDeviceMemory,
                            "Copy exceeds buffer size",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Map buffer memory
    void* mappedData;
    try {
        mappedData = m_memory.mapMemory(offset, size);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to map buffer memory: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // If memory is not host coherent, invalidate the memory
    if (!(properties & vk::MemoryPropertyFlagBits::eHostCoherent)) {
        vk::MappedMemoryRange memoryRange{
            *m_memory,
            offset,
            size
        };

        try {
            device->get().invalidateMappedMemoryRanges(memoryRange);
        } catch (const vk::SystemError& e) {
            throw VulkanException(e.code(), std::string("Failed to invalidate mapped memory: ") + e.what(),
                                __FUNCTION__, __FILE__, __LINE__);
        }
    }

    // Copy data
    memcpy(data, mappedData, size);

    // Unmap buffer memory
    m_memory.unmapMemory();

    LOG_DEBUG("Copied " + std::to_string(size) + " bytes from buffer at offset " + std::to_string(offset));
}

Buffer Buffer::createDeviceLocalBuffer(VulkanDevice* device, const void* data,
                                       vk::DeviceSize size,
                                       vk::BufferUsageFlagBits usageFlag) {
    // Create staging buffer with host-visible memory
    BufferConfig stagingConfig;
    stagingConfig.size = size;
    stagingConfig.usage = vk::BufferUsageFlagBits::eTransferSrc;
    stagingConfig.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

    Buffer stagingBuffer(device, stagingConfig);
    stagingBuffer.copyFrom(data, size);

    // Create device-local buffer
    BufferConfig config;
    config.size = size;
    config.usage = usageFlag | vk::BufferUsageFlagBits::eTransferDst;
    config.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;

    Buffer deviceBuffer(device, config);

    // Copy from staging buffer to device buffer
    // Create a temporary command pool and command buffer for the copy operation
    vk::CommandPoolCreateInfo poolInfo;
    poolInfo.setFlags(vk::CommandPoolCreateFlagBits::eTransient);
    poolInfo.setQueueFamilyIndex(device->getGraphicsQueueFamily());

    vk::raii::CommandPool commandPool{device->get(), poolInfo};

    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.setCommandPool(*commandPool);
    allocInfo.setLevel(vk::CommandBufferLevel::ePrimary);
    allocInfo.setCommandBufferCount(1);

    vk::raii::CommandBuffers commandBuffers{device->get(), allocInfo};
    vk::raii::CommandBuffer& commandBuffer = commandBuffers[0];

    // Begin command buffer
    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    commandBuffer.begin(beginInfo);

    // Copy buffer
    vk::BufferCopy copyRegion;
    copyRegion.setSrcOffset(0);
    copyRegion.setDstOffset(0);
    copyRegion.setSize(size);
    commandBuffer.copyBuffer(*stagingBuffer.get(), *deviceBuffer.get(), copyRegion);

    // End command buffer
    commandBuffer.end();

    // Submit command buffer with fence for synchronization
    vk::FenceCreateInfo fenceInfo;
    vk::raii::Fence fence{device->get(), fenceInfo};

    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*commandBuffer);
    device->getGraphicsQueue().submit(submitInfo, *fence);

    // Wait for the copy to complete
    (void)device->get().waitForFences(*fence, true, UINT64_MAX);

    return deviceBuffer;
}

Buffer Buffer::createVertexBuffer(VulkanDevice* device, const void* data, vk::DeviceSize size) {
    LOG_INFO("Vertex buffer created: size=" + std::to_string(size));
    return createDeviceLocalBuffer(device, data, size, vk::BufferUsageFlagBits::eVertexBuffer);
}

Buffer Buffer::createIndexBuffer(VulkanDevice* device, const void* data, vk::DeviceSize size) {
    LOG_INFO("Index buffer created: size=" + std::to_string(size));
    return createDeviceLocalBuffer(device, data, size, vk::BufferUsageFlagBits::eIndexBuffer);
}

Buffer Buffer::createUniformBuffer(VulkanDevice* device, vk::DeviceSize size) {
    BufferConfig config;
    config.size = size;
    config.usage = vk::BufferUsageFlagBits::eUniformBuffer;
    config.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

    LOG_INFO("Uniform buffer created: size=" + std::to_string(size));
    return Buffer(device, config);
}

Buffer Buffer::createStagingBuffer(VulkanDevice* device, vk::DeviceSize size) {
    BufferConfig config;
    config.size = size;
    config.usage = vk::BufferUsageFlagBits::eTransferSrc;
    config.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

    LOG_INFO("Staging buffer created: size=" + std::to_string(size));
    return Buffer(device, config);
}

Buffer Buffer::createBuffer(VulkanDevice* device, vk::DeviceSize size,
                           vk::BufferUsageFlags usage,
                           vk::MemoryPropertyFlags properties) {
    BufferConfig config;
    config.size = size;
    config.usage = usage;
    config.properties = properties;

    LOG_INFO("Buffer created: size=" + std::to_string(size));
    return Buffer(device, config);
}

void Buffer::copyBuffer(vk::raii::CommandBuffer& commandBuffer,
                       vk::Buffer srcBuffer, vk::Buffer dstBuffer,
                       vk::DeviceSize size,
                       vk::DeviceSize srcOffset,
                       vk::DeviceSize dstOffset) {

    vk::BufferCopy copyRegion;
    copyRegion.setSrcOffset(srcOffset);
    copyRegion.setDstOffset(dstOffset);
    copyRegion.setSize(size);

    commandBuffer.copyBuffer(srcBuffer, dstBuffer, copyRegion);

    LOG_DEBUG("Buffer copy recorded: size=" + std::to_string(size) +
              ", srcOffset=" + std::to_string(srcOffset) +
              ", dstOffset=" + std::to_string(dstOffset));
}

void Buffer::implementCreateBuffer() {
    vk::BufferCreateInfo bufferInfo;
    bufferInfo.setSize(size);
    bufferInfo.setUsage(usage);
    bufferInfo.setSharingMode(vk::SharingMode::eExclusive);

    try {
        buffer = device->get().createBuffer(bufferInfo);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to create buffer: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    LOG_DEBUG("Vulkan buffer created");
}

void Buffer::allocateMemory() {
    // Get memory requirements
    auto memRequirements = buffer.getMemoryRequirements();

    // Allocate memory
    vk::MemoryAllocateInfo allocInfo;
    allocInfo.setAllocationSize(memRequirements.size);
    allocInfo.setMemoryTypeIndex(findMemoryType(memRequirements.memoryTypeBits, properties));

    try {
        m_memory = device->get().allocateMemory(allocInfo);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(),
                            std::string("Failed to allocate buffer memory: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Bind memory to buffer
    try {
        buffer.bindMemory(*m_memory, 0);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(),
                            std::string("Failed to bind buffer memory: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    LOG_DEBUG("Buffer memory allocated and bound: size=" + std::to_string(memRequirements.size) +
              ", typeIndex=" + std::to_string(allocInfo.memoryTypeIndex));
}

uint32_t Buffer::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const {
    return device->findMemoryType(typeFilter, properties);
}

} // namespace RYRayTracing
