// Resources.cpp
#include "Application.h"

#include "stb_image.h"

#include <cstring>

uint32_t Application::findMemoryType(const uint32_t typeFilter, const vk::MemoryPropertyFlags properties) const {
    const auto memProperties = m_physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties
            ) return i;
    }
    throw std::runtime_error("failed to find suitable memory type!");
    return 0; // optional
}

void Application::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& bufferMemory) {
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

void Application::copyBuffer(const vk::raii::Buffer& srcBuffer, const vk::raii::Buffer& dstBuffer, const vk::DeviceSize size) const {
    const vk::raii::CommandBuffer commandBuffer = beginSingleTimeCommands();

    vk::BufferCopy copyRegion;
    copyRegion.size = size;
    commandBuffer.copyBuffer(srcBuffer, dstBuffer, copyRegion);

    endSingleTimeCommands(commandBuffer);
}

void Application::createVertexBuffer() {
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

void Application::createInstanceBuffers() {
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

void Application::createIndexBuffer() {
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

void Application::createUniformBuffers() {
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

void Application::transitionImageLayout(
    const vk::raii::Image& image,
    const vk::Format format,
    const vk::ImageLayout oldLayout,
    const vk::ImageLayout newLayout
) const {
    const vk::raii::CommandBuffer commandBuffer = beginSingleTimeCommands();

    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vk::PipelineStageFlagBits sourceStage;
    vk::PipelineStageFlagBits destinationStage;

    if (oldLayout == vk::ImageLayout::eUndefined &&
        newLayout == vk::ImageLayout::eTransferDstOptimal
        ) {
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
        destinationStage = vk::PipelineStageFlagBits::eTransfer;
    }
    else if (
        oldLayout == vk::ImageLayout::eTransferDstOptimal &&
        newLayout == vk::ImageLayout::eShaderReadOnlyOptimal
        ) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        sourceStage = vk::PipelineStageFlagBits::eTransfer;
        destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
    }
    else {
        throw std::invalid_argument("unsupported layout transition!");
    }

    commandBuffer.pipelineBarrier(
        sourceStage,        // srcStageMask
        destinationStage,   // dstStageMask
        {},         // dependencyFlags
        nullptr,    // memoryBarriers
        nullptr,    // bufferMemoryBarriers
        barrier     // imageMemoryBarriers
    );

    endSingleTimeCommands(commandBuffer);
}

void Application::copyBufferToImage(
    const vk::raii::Buffer& buffer,
    const vk::raii::Image& image,
    const uint32_t width,
    const uint32_t height
) const {
    const vk::raii::CommandBuffer commandBuffer = beginSingleTimeCommands();
    vk::BufferImageCopy region;
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = vk::Offset3D{ 0, 0, 0 };
    region.imageExtent = vk::Extent3D{ width, height, 1 };

    commandBuffer.copyBufferToImage(
        buffer,
        image,
        vk::ImageLayout::eTransferDstOptimal,
        region
    );

    endSingleTimeCommands(commandBuffer);
}

void Application::createImage(
    const uint32_t width,
    const uint32_t height,
    const vk::Format format,
    const vk::ImageTiling tiling,
    const vk::ImageUsageFlags usage,
    const vk::MemoryPropertyFlags properties,
    vk::raii::Image& image,
    vk::raii::DeviceMemory& imageMemory
) const {
    vk::ImageCreateInfo imageInfo;
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = usage;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;

    image = m_device.createImage(imageInfo);

    const vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    imageMemory = m_device.allocateMemory(allocInfo);

    image.bindMemory(imageMemory, 0);
}

void Application::createTextureImage() {
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load("assets/textures/rust_cpp.png", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) throw std::runtime_error("failed to load texture image!");
    const vk::DeviceSize imageSize = texWidth * texHeight * 4;
    vk::raii::DeviceMemory stagingBufferMemory{ nullptr };
    vk::raii::Buffer stagingBuffer{ nullptr };
    createBuffer(
        imageSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent,
        stagingBuffer,
        stagingBufferMemory
    );
    void* data = stagingBufferMemory.mapMemory(0, imageSize);
    memcpy(data, pixels, imageSize);
    stagingBufferMemory.unmapMemory();
    stbi_image_free(pixels);

    createImage(
        texWidth,
        texHeight,
        vk::Format::eR8G8B8A8Srgb,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst |
        vk::ImageUsageFlagBits::eSampled,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        m_textureImage,
        m_textureImageMemory
    );

    transitionImageLayout(
        m_textureImage,
        vk::Format::eR8G8B8A8Srgb,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal
    );

    copyBufferToImage(
        stagingBuffer,
        m_textureImage,
        static_cast<uint32_t>(texWidth),
        static_cast<uint32_t>(texHeight)
    );

    transitionImageLayout(
        m_textureImage,
        vk::Format::eR8G8B8A8Srgb,
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal
    );
}

void Application::createDescriptorPool() {
    vk::DescriptorPoolSize poolSize(vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT);
    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    m_descriptorPool = vk::raii::DescriptorPool(m_device, poolInfo);
}

void Application::createDescriptorSets() {
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
