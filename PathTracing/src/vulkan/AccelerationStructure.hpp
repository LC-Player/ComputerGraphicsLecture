// AccelerationStructure.hpp
#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace RYRayTracing {

class VulkanDevice;
struct GPUVertex;

struct BLAS {
    vk::AccelerationStructureKHR handle = nullptr;
    vk::raii::Buffer buffer = nullptr;
    vk::raii::DeviceMemory memory = nullptr;  // must stay alive with buffer
    uint64_t deviceAddress = 0;
};

struct TLAS {
    vk::AccelerationStructureKHR handle = nullptr;
    vk::raii::Buffer buffer = nullptr;
    vk::raii::DeviceMemory memory = nullptr;         // AS buffer memory
    vk::raii::Buffer instanceBuffer = nullptr;
    vk::raii::DeviceMemory instanceMemory = nullptr; // instance buffer memory
    uint32_t instanceCount = 0;
};

/// Build a BLAS (creates internal command buffer, submits, and waits).
BLAS buildBLAS(VulkanDevice* device,
               vk::Buffer vertexBuffer, uint32_t vertexCount,
               vk::Buffer indexBuffer,  uint32_t firstIndex, uint32_t indexCount,
               vk::DeviceAddress vertexAddr, vk::DeviceAddress indexAddr);

/// Build a TLAS (creates internal command buffer, submits, and waits).
/// Returns the new TLAS. The caller owns the returned TLAS and must manage its lifetime.
TLAS buildTLAS(VulkanDevice* device,
               const std::vector<VkAccelerationStructureInstanceKHR>& instances);

void destroyBLAS(vk::Device device, BLAS& blas);
void destroyTLAS(vk::Device device, TLAS& tlas);

VkAccelerationStructureInstanceKHR makeInstance(
    const BLAS& blas, const glm::mat4& transform,
    uint32_t customIndex, uint32_t hitGroupIndex = 0);

/// Tessellate a sphere via icosahedron subdivision.
std::pair<std::vector<glm::vec3>, std::vector<uint32_t>>
tessellateSphere(const glm::vec3& center, float radius, uint32_t subdivisions = 3);

} // namespace RYRayTracing
