// Vertex.h
#pragma once
#include <glm/glm.hpp>

#include <vulkan/vulkan.hpp>

#include <compare>

struct Vertex {
    glm::vec3 local;
    glm::vec3 normal;
    glm::vec2 texCoord;

    bool operator==(const Vertex& other) const = default;

    auto operator<=>(const Vertex& other) const {
        if (auto cmp = local.x <=> other.local.x; cmp != 0) return cmp;
        if (auto cmp = local.y <=> other.local.y; cmp != 0) return cmp;
        if (auto cmp = local.z <=> other.local.z; cmp != 0) return cmp;
        if (auto cmp = normal.x <=> other.normal.x; cmp != 0) return cmp;
        if (auto cmp = normal.y <=> other.normal.y; cmp != 0) return cmp;
        if (auto cmp = normal.z <=> other.normal.z; cmp != 0) return cmp;
        if (auto cmp = texCoord.x <=> other.texCoord.x; cmp != 0) return cmp;
        return texCoord.y <=> other.texCoord.y;
    }
};

namespace std {
    template<>
    struct hash<glm::vec2> {
        std::size_t operator()(const glm::vec2& v) const noexcept {
            std::size_t hx = hash<float>()(v.x);
            std::size_t hy = hash<float>()(v.y);
            return hx ^ (hy << 1);
        }
    };
    template<>
    struct hash<glm::vec3> {
        std::size_t operator()(const glm::vec3& v) const noexcept {
            std::size_t hx = hash<float>()(v.x);
            std::size_t hy = hash<float>()(v.y);
            std::size_t hz = hash<float>()(v.z);
            return hx ^ (hy << 1) ^ (hz << 2);
        }
    };
    template<>
    struct hash<Vertex> {
        std::size_t operator()(const Vertex& v) const noexcept {
            std::size_t h1 = hash<glm::vec3>()(v.local);
            std::size_t h2 = hash<glm::vec2>()(v.texCoord);
            std::size_t h3 = hash<glm::vec3>()(v.normal);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
}

inline vk::VertexInputBindingDescription getVertexBindingDescription() {
    vk::VertexInputBindingDescription description;
    description.binding = 0;
    description.stride = sizeof(Vertex);
    description.inputRate = vk::VertexInputRate::eVertex;
    return description;
}

inline std::array<vk::VertexInputAttributeDescription, 3> getVertexAttributeDescriptions() {
    std::array<vk::VertexInputAttributeDescription, 3> attributeDescriptions;

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = vk::Format::eR32G32B32Sfloat;
    attributeDescriptions[0].offset = offsetof(Vertex, local);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = vk::Format::eR32G32Sfloat;
    attributeDescriptions[1].offset = offsetof(Vertex, texCoord);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 7; // after color
    attributeDescriptions[2].format = vk::Format::eR32G32B32Sfloat;
    attributeDescriptions[2].offset = offsetof(Vertex, normal);

    return attributeDescriptions;
}

struct InstanceData {
    glm::mat4 transform;
    glm::vec4 color;
};

inline vk::VertexInputBindingDescription getInstanceBindingDescription() {
    vk::VertexInputBindingDescription description;
    description.binding = 1;
    description.stride = sizeof(InstanceData);
    description.inputRate = vk::VertexInputRate::eInstance;
    return description;
}

inline std::array<vk::VertexInputAttributeDescription, 5> getInstanceAttributeDescriptions() {
    std::array<vk::VertexInputAttributeDescription, 5> attributeDescriptions;

    for (int i = 0; i < 4; i++) {
        attributeDescriptions[i].binding = 1;
        attributeDescriptions[i].location = 2 + i;
        attributeDescriptions[i].format = vk::Format::eR32G32B32A32Sfloat;
        attributeDescriptions[i].offset = offsetof(InstanceData, transform) + sizeof(glm::vec4) * i;
    }

    attributeDescriptions[4].binding = 1;
    attributeDescriptions[4].location = 6;
    attributeDescriptions[4].format = vk::Format::eR32G32B32A32Sfloat;
    attributeDescriptions[4].offset = offsetof(InstanceData, color);

    return attributeDescriptions;
}
