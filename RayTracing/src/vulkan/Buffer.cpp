#include "Buffer.hpp"
#include "VulkanDevice.hpp"
#include <cstring>

namespace RYRayTracing {

Buffer::Buffer(VulkanDevice* device, const BufferConfig& config)
    : device(device)
    , buffer(VK_NULL_HANDLE)
    , memory(VK_NULL_HANDLE)
    , size(config.size)
    , usage(config.usage)
    , properties(config.properties)
    , mappedMemory(nullptr)
    , isMapped(false) {

    if (size == 0) {
        throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                            "Buffer size cannot be zero",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    LOG_DEBUG("Creating buffer: size=" + std::to_string(size) +
              ", usage=" + std::to_string(usage) +
              ", properties=" + std::to_string(properties));

    createBuffer();
    allocateMemory();

    LOG_INFO("Buffer created successfully");
}

Buffer::~Buffer() {
    LOG_DEBUG("Destroying buffer");

    if (isMapped) {
        unmap();
    }

    VkDevice vkDevice = device->get();

    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vkDevice, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }

    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(vkDevice, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }

    LOG_DEBUG("Buffer destroyed");
}

Buffer::Buffer(Buffer&& other) noexcept
    : device(other.device)
    , buffer(other.buffer)
    , memory(other.memory)
    , size(other.size)
    , usage(other.usage)
    , properties(other.properties)
    , mappedMemory(other.mappedMemory)
    , isMapped(other.isMapped) {
    other.buffer = VK_NULL_HANDLE;
    other.memory = VK_NULL_HANDLE;
    other.mappedMemory = nullptr;
    other.isMapped = false;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        if (isMapped) {
            unmap();
        }

        VkDevice vkDevice = device->get();

        if (buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(vkDevice, buffer, nullptr);
        }

        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(vkDevice, memory, nullptr);
        }

        // Move resources from other
        device = other.device;
        buffer = other.buffer;
        memory = other.memory;
        size = other.size;
        usage = other.usage;
        properties = other.properties;
        mappedMemory = other.mappedMemory;
        isMapped = other.isMapped;

        // Reset other
        other.buffer = VK_NULL_HANDLE;
        other.memory = VK_NULL_HANDLE;
        other.mappedMemory = nullptr;
        other.isMapped = false;
    }
    return *this;
}

void* Buffer::map(VkDeviceSize offset, VkDeviceSize size) {
    if (isMapped) {
        LOG_WARNING("Buffer is already mapped");
        return mappedMemory;
    }

    VkDevice vkDevice = device->get();
    VkDeviceSize mapSize = (size == VK_WHOLE_SIZE) ? this->size : size;

    VK_CHECK_RESULT(vkMapMemory(vkDevice, memory, offset, mapSize, 0, &mappedMemory));
    isMapped = true;

    LOG_DEBUG("Buffer memory mapped: offset=" + std::to_string(offset) +
              ", size=" + std::to_string(mapSize));

    return mappedMemory;
}

void Buffer::unmap() {
    if (!isMapped) {
        LOG_WARNING("Buffer is not mapped");
        return;
    }

    VkDevice vkDevice = device->get();
    vkUnmapMemory(vkDevice, memory);

    mappedMemory = nullptr;
    isMapped = false;

    LOG_DEBUG("Buffer memory unmapped");
}

void Buffer::copyFrom(const void* data, VkDeviceSize size, VkDeviceSize offset) {
    if (size == 0) {
        LOG_WARNING("Attempting to copy zero bytes");
        return;
    }

    if (offset + size > this->size) {
        throw VulkanException(VK_ERROR_OUT_OF_DEVICE_MEMORY,
                            "Copy exceeds buffer size",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Map buffer memory
    void* mappedData = map(offset, size);

    // Copy data
    memcpy(mappedData, data, size);

    // If memory is not host coherent, flush the memory
    if (!(properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange memoryRange{};
        memoryRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        memoryRange.memory = memory;
        memoryRange.offset = offset;
        memoryRange.size = size;

        VkDevice vkDevice = device->get();
        VK_CHECK_RESULT(vkFlushMappedMemoryRanges(vkDevice, 1, &memoryRange));
    }

    // Unmap buffer memory
    unmap();

    LOG_DEBUG("Copied " + std::to_string(size) + " bytes to buffer at offset " + std::to_string(offset));
}

void Buffer::copyTo(void* data, VkDeviceSize size, VkDeviceSize offset) const {
    if (size == 0) {
        LOG_WARNING("Attempting to copy zero bytes");
        return;
    }

    if (offset + size > this->size) {
        throw VulkanException(VK_ERROR_OUT_OF_DEVICE_MEMORY,
                            "Copy exceeds buffer size",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Map buffer memory
    VkDevice vkDevice = device->get();
    void* mappedData;
    VK_CHECK_RESULT(vkMapMemory(vkDevice, memory, offset, size, 0, &mappedData));

    // If memory is not host coherent, invalidate the memory
    if (!(properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange memoryRange{};
        memoryRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        memoryRange.memory = memory;
        memoryRange.offset = offset;
        memoryRange.size = size;

        VK_CHECK_RESULT(vkInvalidateMappedMemoryRanges(vkDevice, 1, &memoryRange));
    }

    // Copy data
    memcpy(data, mappedData, size);

    // Unmap buffer memory
    vkUnmapMemory(vkDevice, memory);

    LOG_DEBUG("Copied " + std::to_string(size) + " bytes from buffer at offset " + std::to_string(offset));
}

Buffer Buffer::createVertexBuffer(VulkanDevice* device, const void* data, VkDeviceSize size) {
    BufferConfig config;
    config.size = size;
    config.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    config.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    Buffer buffer(device, config);

    // Create staging buffer for upload
    BufferConfig stagingConfig;
    stagingConfig.size = size;
    stagingConfig.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingConfig.properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    Buffer stagingBuffer(device, stagingConfig);
    stagingBuffer.copyFrom(data, size);

    // TODO: Copy from staging buffer to vertex buffer using command buffer
    // This requires a command buffer and queue submission
    // For now, we'll use host-visible memory for simplicity

    LOG_INFO("Vertex buffer created: size=" + std::to_string(size));
    return buffer;
}

Buffer Buffer::createIndexBuffer(VulkanDevice* device, const void* data, VkDeviceSize size) {
    BufferConfig config;
    config.size = size;
    config.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    config.properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    Buffer buffer(device, config);

    // Create staging buffer for upload
    BufferConfig stagingConfig;
    stagingConfig.size = size;
    stagingConfig.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingConfig.properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    Buffer stagingBuffer(device, stagingConfig);
    stagingBuffer.copyFrom(data, size);

    // TODO: Copy from staging buffer to index buffer using command buffer

    LOG_INFO("Index buffer created: size=" + std::to_string(size));
    return buffer;
}

Buffer Buffer::createUniformBuffer(VulkanDevice* device, VkDeviceSize size) {
    BufferConfig config;
    config.size = size;
    config.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    config.properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    LOG_INFO("Uniform buffer created: size=" + std::to_string(size));
    return Buffer(device, config);
}

Buffer Buffer::createStagingBuffer(VulkanDevice* device, VkDeviceSize size) {
    BufferConfig config;
    config.size = size;
    config.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    config.properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    LOG_INFO("Staging buffer created: size=" + std::to_string(size));
    return Buffer(device, config);
}

void Buffer::copyBuffer(VkCommandBuffer commandBuffer,
                       VkBuffer srcBuffer, VkBuffer dstBuffer,
                       VkDeviceSize size,
                       VkDeviceSize srcOffset,
                       VkDeviceSize dstOffset) {

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;

    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    LOG_DEBUG("Buffer copy recorded: size=" + std::to_string(size) +
              ", srcOffset=" + std::to_string(srcOffset) +
              ", dstOffset=" + std::to_string(dstOffset));
}

void Buffer::createBuffer() {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkDevice vkDevice = device->get();
    VK_CHECK_RESULT(vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &buffer));

    LOG_DEBUG("Vulkan buffer created");
}

void Buffer::allocateMemory() {
    VkDevice vkDevice = device->get();

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vkDevice, buffer, &memRequirements);

    // Allocate memory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    VK_CHECK_RESULT(vkAllocateMemory(vkDevice, &allocInfo, nullptr, &memory));

    // Bind memory to buffer
    VK_CHECK_RESULT(vkBindBufferMemory(vkDevice, buffer, memory, 0));

    LOG_DEBUG("Buffer memory allocated and bound: size=" + std::to_string(memRequirements.size) +
              ", typeIndex=" + std::to_string(allocInfo.memoryTypeIndex));
}

uint32_t Buffer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
    return device->findMemoryType(typeFilter, properties);
}

} // namespace RYRayTracing