// type.h
#pragma once
#include <glm/glm.hpp>

#include <vulkan/vulkan.hpp>

#include <compare>

struct Vertex {
    glm::vec3 local;
    glm::vec4 color;
    glm::vec2 texCoord;
    glm::mat4 transform;

    bool operator==(const Vertex& other) const = default;

    auto operator<=>(const Vertex& other) const {

        if (auto cmp = local.x <=> other.local.x; cmp != 0) return cmp;
        if (auto cmp = local.y <=> other.local.y; cmp != 0) return cmp;
        if (auto cmp = local.z <=> other.local.z; cmp != 0) return cmp;

        if (auto cmp = texCoord.x <=> other.texCoord.x; cmp != 0) return cmp;
        if (auto cmp = texCoord.y <=> other.texCoord.y; cmp != 0) return cmp;

        return color.x <=> other.color.x;
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
    struct hash<glm::vec4> {
        std::size_t operator()(const glm::vec4& v) const noexcept {
            std::size_t hx = hash<float>()(v.x);
            std::size_t hy = hash<float>()(v.y);
            std::size_t hz = hash<float>()(v.z);
            std::size_t hw = hash<float>()(v.w);
            return hx ^ (hy << 1) ^ (hz << 2) ^ (hw << 3);
        }
    };
    template<>
    struct hash<Vertex> {
        std::size_t operator()(const Vertex& v) const noexcept {
            std::size_t h1 = hash<glm::vec3>()(v.local);
            std::size_t h2 = hash<glm::vec4>()(v.color);
            std::size_t h3 = hash<glm::vec2>()(v.texCoord);
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