// Texture.hpp
#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <string>
#include "core/Exception.hpp"
#include "core/Logger.hpp"

namespace RYBlinnPhong {

class VulkanDevice;

struct TextureConfig {
    std::string filepath;
    vk::Format format = vk::Format::eR8G8B8A8Srgb;
    vk::Filter magFilter = vk::Filter::eLinear;
    vk::Filter minFilter = vk::Filter::eLinear;
    vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eRepeat;
    bool enableAnisotropy = true;
};

class Texture {
public:
    Texture(VulkanDevice* device, const TextureConfig& config);
    ~Texture() = default;

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept = default;
    Texture& operator=(Texture&& other) noexcept = default;

    vk::raii::ImageView& getImageView() { return textureImageView; }
    const vk::raii::ImageView& getImageView() const { return textureImageView; }

    vk::raii::Sampler& getSampler() { return textureSampler; }
    const vk::raii::Sampler& getSampler() const { return textureSampler; }

    vk::raii::Image& getImage() { return textureImage; }
    const vk::raii::Image& getImage() const { return textureImage; }

    vk::DescriptorImageInfo getDescriptorInfo() const;

private:
    VulkanDevice* device = nullptr;
    vk::raii::Image textureImage = nullptr;
    vk::raii::DeviceMemory textureImageMemory = nullptr;
    vk::raii::ImageView textureImageView = nullptr;
    vk::raii::Sampler textureSampler = nullptr;
    uint32_t width;
    uint32_t height;

    void createTextureImage(const std::string& filepath, vk::Format format);
    void createTextureImageView(vk::Format format);
    void createTextureSampler(const TextureConfig& config);

    void createImage(uint32_t width, uint32_t height, vk::Format format,
                     vk::ImageTiling tiling, vk::ImageUsageFlags usage,
                     vk::MemoryPropertyFlags properties);

    void transitionImageLayout(vk::Format format, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);

    void copyBufferToImage(vk::Buffer buffer, uint32_t width, uint32_t height);

    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;

    vk::raii::ImageView createImageView(vk::Image image, vk::Format format, vk::ImageAspectFlags aspectFlags);
};

} // namespace RYBlinnPhong
