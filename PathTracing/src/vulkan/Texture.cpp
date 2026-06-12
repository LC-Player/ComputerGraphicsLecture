// Texture.cpp
#include "Texture.hpp"
#include "VulkanDevice.hpp"
#include "Buffer.hpp"

#include <stb_image.h>

#include <cstring>

namespace RYRayTracing {

Texture::Texture(VulkanDevice* device, const TextureConfig& config)
    : device(device) {

    LOG_INFO("Creating texture from: " + config.filepath);

    createTextureImage(config.filepath, config.format);
    createTextureImageView(config.format);
    createTextureSampler(config);

    LOG_INFO("Texture created successfully");
}

vk::DescriptorImageInfo Texture::getDescriptorInfo() const {
    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.imageView = *textureImageView;
    imageInfo.sampler = *textureSampler;
    return imageInfo;
}

void Texture::createTextureImage(const std::string& filepath, vk::Format format) {
    int texWidth, texHeight, texChannels;
    stbi_set_flip_vertically_on_load(true);
    stbi_uc* pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    if (!pixels) {
        throw VulkanException(vk::Result::eErrorInitializationFailed,
                            "Failed to load texture image: " + filepath,
                            __FUNCTION__, __FILE__, __LINE__);
    }

    width = static_cast<uint32_t>(texWidth);
    height = static_cast<uint32_t>(texHeight);
    const vk::DeviceSize imageSize = width * height * 4;

    LOG_DEBUG("Texture loaded: " + std::to_string(width) + "x" + std::to_string(height));

    BufferConfig stagingConfig;
    stagingConfig.size = imageSize;
    stagingConfig.usage = vk::BufferUsageFlagBits::eTransferSrc;
    stagingConfig.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

    Buffer stagingBuffer(device, stagingConfig);
    stagingBuffer.copyFrom(pixels, imageSize);
    stbi_image_free(pixels);

    createImage(width, height, format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal);

    transitionImageLayout(format, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);

    copyBufferToImage(*stagingBuffer.get(), width, height);

    transitionImageLayout(format, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
}

void Texture::createTextureImageView(vk::Format format) {
    textureImageView = createImageView(*textureImage, format, vk::ImageAspectFlagBits::eColor);
}

void Texture::createTextureSampler(const TextureConfig& config) {
    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.magFilter = config.magFilter;
    samplerInfo.minFilter = config.minFilter;
    samplerInfo.addressModeU = config.addressMode;
    samplerInfo.addressModeV = config.addressMode;
    samplerInfo.addressModeW = config.addressMode;
    samplerInfo.anisotropyEnable = config.enableAnisotropy;

    auto properties = device->getPhysical().getProperties();
    samplerInfo.maxAnisotropy = config.enableAnisotropy ? properties.limits.maxSamplerAnisotropy : 1.0f;
    samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.unnormalizedCoordinates = false;
    samplerInfo.compareEnable = false;
    samplerInfo.compareOp = vk::CompareOp::eAlways;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    try {
        textureSampler = device->get().createSampler(samplerInfo);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to create texture sampler: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }
}

void Texture::createImage(uint32_t width, uint32_t height, vk::Format format,
                          vk::ImageTiling tiling, vk::ImageUsageFlags usage,
                          vk::MemoryPropertyFlags properties) {
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
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.samples = vk::SampleCountFlagBits::e1;

    try {
        textureImage = device->get().createImage(imageInfo);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to create image: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    auto memRequirements = textureImage.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    try {
        textureImageMemory = device->get().allocateMemory(allocInfo);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to allocate image memory: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    textureImage.bindMemory(*textureImageMemory, 0);
}

void Texture::transitionImageLayout(vk::Format format, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
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

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    commandBuffer.begin(beginInfo);

    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = *textureImage;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vk::PipelineStageFlags srcStage;
    vk::PipelineStageFlags dstStage;

    if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
        dstStage = vk::PipelineStageFlagBits::eTransfer;
    } else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        srcStage = vk::PipelineStageFlagBits::eTransfer;
        dstStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else {
        throw VulkanException(vk::Result::eErrorInitializationFailed,
                            "Unsupported layout transition",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    commandBuffer.pipelineBarrier(srcStage, dstStage, {}, {}, {}, barrier);

    commandBuffer.end();

    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*commandBuffer);

    vk::FenceCreateInfo fenceInfo;
    vk::raii::Fence fence{device->get(), fenceInfo};
    device->getGraphicsQueue().submit(submitInfo, *fence);

    (void)device->get().waitForFences(*fence, true, UINT64_MAX);
}

void Texture::copyBufferToImage(vk::Buffer buffer, uint32_t width, uint32_t height) {
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

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    commandBuffer.begin(beginInfo);

    vk::BufferImageCopy region;
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{width, height, 1};

    commandBuffer.copyBufferToImage(buffer, *textureImage, vk::ImageLayout::eTransferDstOptimal, region);

    commandBuffer.end();

    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*commandBuffer);

    vk::FenceCreateInfo fenceInfo;
    vk::raii::Fence fence{device->get(), fenceInfo};
    device->getGraphicsQueue().submit(submitInfo, *fence);

    (void)device->get().waitForFences(*fence, true, UINT64_MAX);
}

uint32_t Texture::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const {
    return device->findMemoryType(typeFilter, properties);
}

vk::raii::ImageView Texture::createImageView(vk::Image image, vk::Format format, vk::ImageAspectFlags aspectFlags) {
    vk::ImageViewCreateInfo viewInfo;
    viewInfo.image = image;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    try {
        return device->get().createImageView(viewInfo);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to create image view: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }
}

} // namespace RYRayTracing
