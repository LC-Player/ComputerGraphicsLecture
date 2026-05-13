// Instance.cpp
#include "Instance.h"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/Buffer.hpp"
#include <cstring>

namespace RYRayTracing {

void Instance::createBuffer(VulkanDevice* device) {
    constexpr vk::DeviceSize bufferSize = sizeof(InstanceData);

    m_buffer = std::make_unique<Buffer>(
        Buffer::createBuffer(device, bufferSize,
            vk::BufferUsageFlagBits::eVertexBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));

    m_mapped = m_buffer->map(0, bufferSize);
    updateBuffer();
}

void Instance::updateBuffer() {
    InstanceData data;
    data.transform = transform();
    data.color = color;
    memcpy(m_mapped, &data, sizeof(InstanceData));
}

} // namespace RYRayTracing
