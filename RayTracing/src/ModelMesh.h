#pragma once
#include <filesystem>
#include <vector>

#include "Vertex.h"
#include "vulkan/Buffer.hpp"

namespace RYRayTracing {
    class ModelMesh {
        ModelMesh(VulkanDevice* device, const std::filesystem::path& modelPath) : m_vertexBuffer(device, BufferConfig()) {
        }

        VulkanDevice* device;
        std::vector<Vertex> m_vertices;
        std::vector<uint32_t> m_indices;
        Buffer m_vertexBuffer;
    };
} // RYRayTracing