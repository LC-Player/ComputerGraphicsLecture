// type.h
#pragma once
#include <glm/glm.hpp>

#include <vulkan/vulkan.hpp>

struct Vertex {
    glm::vec3 local;
    glm::vec4 color;
    glm::vec2 texCoord;
    glm::mat4 transform;
};

inline vk::VertexInputBindingDescription getVertexBindingDescription() {
    vk::VertexInputBindingDescription description;
    description.binding = 0;
    description.stride = sizeof(Vertex);
    description.inputRate = vk::VertexInputRate::eVertex;
    return description;
}

inline std::array<vk::VertexInputAttributeDescription, 7> getVertexAttributeDescriptions() {
    std::array<vk::VertexInputAttributeDescription, 7> attributeDescriptions;

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = vk::Format::eR32G32B32Sfloat;
    attributeDescriptions[0].offset = offsetof(Vertex, local);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = vk::Format::eR32G32B32A32Sfloat;
    attributeDescriptions[1].offset = offsetof(Vertex, color);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = vk::Format::eR32G32Sfloat;
    attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

    for (int i = 0; i < 4; i++) {
        attributeDescriptions[3 + i].binding = 0;
        attributeDescriptions[3 + i].location = 3 + i;
        attributeDescriptions[3 + i].format = vk::Format::eR32G32B32A32Sfloat;
        attributeDescriptions[3 + i].offset = offsetof(Vertex, transform) + sizeof(glm::vec4) * i;
    }

    return attributeDescriptions;
}

struct CameraData {
    glm::mat4 viewProj;
};
