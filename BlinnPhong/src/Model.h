// Model.h
#pragma once

#include "Vertex.h"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/Buffer.hpp"
#include "vulkan/Texture.hpp"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <memory>
#include <string>

namespace RYBlinnPhong {

class Model {
public:
    std::string name;
    std::vector<Vertex> sourceVertices;
    std::vector<uint32_t> indices;

    void loadFromObj(const std::string& objPath);

    void createBuffers(VulkanDevice* device);

    void createTexture(VulkanDevice* device, const std::string& texturePath);

    void createTextureDescriptorSet(
        const vk::raii::Device& device,
        const vk::raii::DescriptorPool& pool,
        const vk::raii::DescriptorSetLayout& layout);

    void resetTextureDescriptorSet() { m_textureDescriptorSet = nullptr; }

    const std::unique_ptr<Buffer>& getVertexBuffer() const { return m_vertexBuffer; }
    const std::unique_ptr<Buffer>& getIndexBuffer() const { return m_indexBuffer; }
    const vk::raii::DescriptorSet& getTextureDescriptorSet() const { return m_textureDescriptorSet; }

private:
    std::unique_ptr<Buffer> m_vertexBuffer;
    std::unique_ptr<Buffer> m_indexBuffer;
    std::unique_ptr<Texture> m_texture;
    vk::raii::DescriptorSet m_textureDescriptorSet = nullptr;
};

} // namespace RYBlinnPhong
