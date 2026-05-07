#pragma once
#include <glm/glm.hpp>

#include <vulkan/vulkan.hpp>

struct Vertex {
    glm::vec3 local;
    glm::vec4 color;
};

struct QuadInstanceData {
    glm::mat4 transform;
};


inline vk::VertexInputBindingDescription getVertexBindingDescription() {
    vk::VertexInputBindingDescription description;
    description.binding = 0;
    description.stride = sizeof(Vertex);
    description.inputRate = vk::VertexInputRate::eVertex;
    return description;
}
inline vk::VertexInputBindingDescription getInstanceBindingDescription() {
    vk::VertexInputBindingDescription description;
    description.binding = 1;
    description.stride = sizeof(QuadInstanceData);
    description.inputRate = vk::VertexInputRate::eInstance;
    return description;
}

inline std::array<vk::VertexInputAttributeDescription, 6>  getVertexAttributeDescriptions() {
    std::array<vk::VertexInputAttributeDescription, 6> attributeDescriptions;

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = vk::Format::eR32G32B32Sfloat;
    attributeDescriptions[0].offset = offsetof(Vertex, local);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = vk::Format::eR32G32B32A32Sfloat;
    attributeDescriptions[1].offset = offsetof(Vertex, color);

    for (int i = 0; i < 4; i++) {
        attributeDescriptions[2 + i].binding = 1;
        attributeDescriptions[2 + i].location = 2 + i;
        attributeDescriptions[2 + i].format = vk::Format::eR32G32B32A32Sfloat;
        attributeDescriptions[2 + i].offset = offsetof(QuadInstanceData, transform) + sizeof(glm::vec4) * i;
    }

    return attributeDescriptions;
}

struct CameraData {
    glm::mat4 viewProj;
};
