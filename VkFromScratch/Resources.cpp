// Resources.cpp
#include "Application.h"

#include <cstring>

uint32_t HelloTriangleApplication::findMemoryType(const uint32_t typeFilter, const vk::MemoryPropertyFlags properties) const {
    const auto memProperties = m_physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties
            ) return i;
    }
    throw std::runtime_error("failed to find suitable memory type!");
    return 0; // optional
}

void HelloTriangleApplication::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& bufferMemory) {
    vk::BufferCreateInfo bufferInfo;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;
    buffer = vk::raii::Buffer(m_device, bufferInfo);
    vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
    bufferMemory = vk::raii::DeviceMemory(m_device, allocInfo);
    buffer.bindMemory(*bufferMemory, 0);
}

void HelloTriangleApplication::copyBuffer(const vk::raii::Buffer& srcBuffer, const vk::raii::Buffer& dstBuffer, const vk::DeviceSize size) const {
    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    auto commandBuffers = m_device.allocateCommandBuffers(allocInfo);
    const vk::raii::CommandBuffer commandBuffer = std::move(commandBuffers.at(0));

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    commandBuffer.begin(beginInfo);

    vk::BufferCopy copyRegion;
    copyRegion.srcOffset = 0; // optional
    copyRegion.dstOffset = 0; // optional
    copyRegion.size = size;
    commandBuffer.copyBuffer(srcBuffer, dstBuffer, copyRegion);

    commandBuffer.end();

    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*commandBuffer);

    m_graphicsQueue.submit(submitInfo);
    m_graphicsQueue.waitIdle();
}

void HelloTriangleApplication::createVertexBuffer() {
    vk::DeviceSize bufferSize = sizeof(m_vertices[0]) * m_vertices.size();

    vk::raii::DeviceMemory stagingBufferMemory{ nullptr };
    vk::raii::Buffer stagingBuffer{ nullptr };
    createBuffer(bufferSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent,
        stagingBuffer,
        stagingBufferMemory
    );

    void* mappedStaging = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(mappedStaging, m_vertices.data(), sizeof(m_vertices[0]) * m_vertices.size());
    stagingBufferMemory.unmapMemory();

    createBuffer(bufferSize,
        vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        m_vertexBuffer,
        m_vertexBufferMemory
    );

    copyBuffer(stagingBuffer, m_vertexBuffer, bufferSize);
}

void HelloTriangleApplication::createInstanceBuffers() {
    vk::DeviceSize instanceBufferSize = sizeof(m_quadInstances[0]) * m_quadInstances.size();

    m_instanceBuffers.reserve(MAX_FRAMES_IN_FLIGHT);
    m_instanceBufferMemory.reserve(MAX_FRAMES_IN_FLIGHT);
    m_mappedInstanceData.reserve(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_instanceBuffers.emplace_back(nullptr);
        m_instanceBufferMemory.emplace_back(nullptr);
        m_mappedInstanceData.emplace_back(nullptr);
        createBuffer(
            instanceBufferSize,
            vk::BufferUsageFlagBits::eVertexBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
            m_instanceBuffers[i],
            m_instanceBufferMemory[i]
        );
        m_mappedInstanceData[i] = m_instanceBufferMemory[i].mapMemory(0, instanceBufferSize);
    }
}

void HelloTriangleApplication::createIndexBuffer() {
    auto bufferSize = sizeof(m_indices[0]) * m_indices.size();
    createBuffer(
        bufferSize,
        vk::BufferUsageFlagBits::eIndexBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        m_indexBuffer,
        m_indexBufferMemory
    );
    auto mappedIndexData = m_indexBufferMemory.mapMemory(0, bufferSize);
    memcpy(mappedIndexData, m_indices.data(), bufferSize);
    m_indexBufferMemory.unmapMemory();
}

void HelloTriangleApplication::createUniformBuffers() {
    constexpr vk::DeviceSize bufferSize = sizeof(CameraData);

    m_uniformBuffers.reserve(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMemory.reserve(MAX_FRAMES_IN_FLIGHT);
    m_uniformBuffersMapped.reserve(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_uniformBuffers.emplace_back(nullptr);
        m_uniformBuffersMemory.emplace_back(nullptr);
        m_uniformBuffersMapped.emplace_back(nullptr);
        createBuffer(bufferSize,
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
            m_uniformBuffers[i],
            m_uniformBuffersMemory[i]
        );

        m_uniformBuffersMapped[i] = m_uniformBuffersMemory[i].mapMemory(0, bufferSize);
    }
}

void HelloTriangleApplication::createDescriptorPool() {
    vk::DescriptorPoolSize poolSize(vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT);
    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    m_descriptorPool = vk::raii::DescriptorPool(m_device, poolInfo);
}

void HelloTriangleApplication::createDescriptorSets() {
    std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *m_descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.setSetLayouts(layouts);
    m_descriptorSets = m_device.allocateDescriptorSets(allocInfo);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vk::DescriptorBufferInfo bufferInfo;
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(CameraData);
        vk::WriteDescriptorSet descriptorWrite;
        descriptorWrite.dstSet = m_descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        descriptorWrite.setBufferInfo(bufferInfo);
        m_device.updateDescriptorSets(descriptorWrite, nullptr);
    }
}
