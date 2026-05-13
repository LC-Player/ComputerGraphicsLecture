// Model.cpp
#include "Model.h"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/Buffer.hpp"
#include "vulkan/Texture.hpp"
#include "tiny_obj_loader.h"
#include <glm/glm.hpp>
#include <stdexcept>
#include <unordered_map>

namespace RYRayTracing {

void Model::loadFromObj(const std::string& objPath) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, objPath.c_str())) {
        throw std::runtime_error(warn + err);
    }
    std::unordered_map<Vertex, uint32_t> uniqueVertexToIndexMap;
    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};
            vertex.local = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };

            vertex.texCoord = {
                attrib.texcoords[2 * index.texcoord_index + 0],
                1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
            };

            if (index.normal_index >= 0) {
                vertex.normal = {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
            };
            } else {
                vertex.normal = {0.0f, 0.0f, 0.0f};
            }

            if (!uniqueVertexToIndexMap.contains(vertex)) {
                uniqueVertexToIndexMap[vertex] = static_cast<uint32_t>(sourceVertices.size());
                sourceVertices.push_back(vertex);
            }
            indices.push_back(uniqueVertexToIndexMap[vertex]);
        }
    }
}

void Model::createBuffers(VulkanDevice* device) {
    m_vertexBuffer = std::make_unique<Buffer>(
        Buffer::createVertexBuffer(device, sourceVertices.data(),
            sourceVertices.size() * sizeof(Vertex)));

    m_indexBuffer = std::make_unique<Buffer>(
        Buffer::createIndexBuffer(device, indices.data(),
            sizeof(uint32_t) * indices.size()));
}

void Model::createTexture(VulkanDevice* device, const std::string& texturePath) {
    TextureConfig config;
    config.filepath = texturePath;
    m_texture = std::make_unique<Texture>(device, config);
}

void Model::createTextureDescriptorSet(
    const vk::raii::Device& device,
    const vk::raii::DescriptorPool& pool,
    const vk::raii::DescriptorSetLayout& layout) {

    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = pool;
    allocInfo.setSetLayouts(*layout);

    std::vector<vk::raii::DescriptorSet> sets = device.allocateDescriptorSets(allocInfo);
    m_textureDescriptorSet = std::move(sets[0]);

    vk::DescriptorImageInfo imageInfo = m_texture->getDescriptorInfo();

    vk::WriteDescriptorSet textureWrite;
    textureWrite.dstSet = *m_textureDescriptorSet;
    textureWrite.dstBinding = 0;
    textureWrite.dstArrayElement = 0;
    textureWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    textureWrite.setImageInfo(imageInfo);

    device.updateDescriptorSets(textureWrite, nullptr);
}

} // namespace RYRayTracing
