// AccelerationStructure.cpp

#include "AccelerationStructure.hpp"
#include "core/Logger.hpp"
#include "VulkanDevice.hpp"
#include "Scene.h"
#include <cmath>
#include <unordered_map>

namespace RYRayTracing {

// Load all AS-related function pointers once
struct ASFunctions {
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkCmdCopyAccelerationStructureKHR vkCmdCopyAccelerationStructureKHR = nullptr;
    bool loaded = false;

    void load(VkDevice dev) {
        if (loaded) return;
        #define LOAD(fn) fn = reinterpret_cast<PFN_##fn>(vkGetDeviceProcAddr(dev, #fn))
        LOAD(vkCreateAccelerationStructureKHR);
        LOAD(vkDestroyAccelerationStructureKHR);
        LOAD(vkGetAccelerationStructureBuildSizesKHR);
        LOAD(vkCmdBuildAccelerationStructuresKHR);
        LOAD(vkGetAccelerationStructureDeviceAddressKHR);
        LOAD(vkCmdCopyAccelerationStructureKHR);
        #undef LOAD
        loaded = true;
        LOG_INFO("AS function pointers loaded");
    }
};

static ASFunctions s_as;

// Helper: get raw VkDevice from VulkanDevice
static VkDevice rawDev(VulkanDevice* dev) { return static_cast<VkDevice>(*dev->get()); }

// Helper: allocate device memory
static vk::raii::DeviceMemory allocMem(vk::raii::Device& dev, const vk::MemoryRequirements& reqs,
                                        uint32_t memTypeIdx, bool needDeviceAddress) {
    vk::MemoryAllocateFlagsInfo flagsInfo;
    vk::MemoryAllocateInfo allocInfo;
    allocInfo.setAllocationSize(reqs.size);
    allocInfo.setMemoryTypeIndex(memTypeIdx);
    if (needDeviceAddress) {
        flagsInfo.setFlags(vk::MemoryAllocateFlagBits::eDeviceAddress);
        allocInfo.setPNext(&flagsInfo);
    }
    return vk::raii::DeviceMemory(dev, allocInfo);
}

// Helper: one-shot command buffer
static std::pair<vk::raii::CommandPool, vk::raii::CommandBuffer>
beginOneShot(vk::raii::Device& dev, uint32_t queueFamily) {
    vk::CommandPoolCreateInfo poolInfo;
    poolInfo.setFlags(vk::CommandPoolCreateFlagBits::eTransient);
    poolInfo.setQueueFamilyIndex(queueFamily);
    vk::raii::CommandPool pool(dev, poolInfo);

    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.setCommandPool(*pool);
    allocInfo.setLevel(vk::CommandBufferLevel::ePrimary);
    allocInfo.setCommandBufferCount(1);
    vk::raii::CommandBuffers bufs(dev, allocInfo);

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    bufs[0].begin(beginInfo);

    return {std::move(pool), std::move(bufs[0])};
}

static void submitAndWait(vk::Queue queue, vk::raii::Device& dev,
                           vk::raii::CommandBuffer& cmd) {
    cmd.end();
    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*cmd);
    vk::raii::Fence fence(dev, vk::FenceCreateInfo{});
    queue.submit(submitInfo, *fence);
    (void)dev.waitForFences(*fence, true, UINT64_MAX);
}

// ── BLAS build ────────────────────────────────────────────────────

BLAS buildBLAS(VulkanDevice* device,
               vk::Buffer vertexBuffer, uint32_t vertexCount,
               vk::Buffer indexBuffer,  uint32_t firstIndex, uint32_t indexCount,
               vk::DeviceAddress vertexAddr, vk::DeviceAddress indexAddr) {
    BLAS result{};
    VkDevice vkDev = rawDev(device);
    s_as.load(vkDev);

    vk::DeviceAddress indexDevAddr = indexAddr + firstIndex * sizeof(uint32_t);

    auto [pool, cmd] = beginOneShot(device->get(), device->getGraphicsQueueFamily());

    // Query build sizes
    VkAccelerationStructureBuildGeometryInfoKHR buildGeoInfo{};
    buildGeoInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildGeoInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildGeoInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildGeoInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geometry.geometry.triangles.vertexData.deviceAddress = vertexAddr;
    geometry.geometry.triangles.vertexStride = sizeof(GPUVertex);
    geometry.geometry.triangles.maxVertex = vertexCount - 1;
    geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
    geometry.geometry.triangles.indexData.deviceAddress = indexDevAddr;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

    buildGeoInfo.geometryCount = 1;
    buildGeoInfo.pGeometries = &geometry;

    uint32_t maxPrimCount = indexCount / 3;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    s_as.vkGetAccelerationStructureBuildSizesKHR(vkDev,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildGeoInfo, &maxPrimCount, &sizeInfo);

    // Create AS buffer
    {
        vk::BufferCreateInfo bufInfo;
        bufInfo.setSize(sizeInfo.accelerationStructureSize);
        bufInfo.setUsage(vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR
                       | vk::BufferUsageFlagBits::eShaderDeviceAddress);
        bufInfo.setSharingMode(vk::SharingMode::eExclusive);
        result.buffer = vk::raii::Buffer(device->get(), bufInfo);

        auto memReqs = result.buffer.getMemoryRequirements();
        result.memory = allocMem(device->get(), memReqs,
            device->findMemoryType(memReqs.memoryTypeBits,
                vk::MemoryPropertyFlagBits::eDeviceLocal), true);
        result.buffer.bindMemory(*result.memory, 0);
    }

    // Create AS handle
    VkAccelerationStructureCreateInfoKHR asCreateInfo{};
    asCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asCreateInfo.buffer = static_cast<VkBuffer>(*result.buffer);
    asCreateInfo.size = sizeInfo.accelerationStructureSize;
    asCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    VkAccelerationStructureKHR asHandle;
    VkResult res = s_as.vkCreateAccelerationStructureKHR(vkDev, &asCreateInfo, nullptr, &asHandle);
    if (res != VK_SUCCESS) {
        LOG_ERROR("vkCreateAccelerationStructureKHR failed: " + std::to_string(res));
        throw std::runtime_error("Failed to create BLAS");
    }
    result.handle = asHandle;

    // Scratch buffer
    vk::raii::Buffer scratchBuf = nullptr;
    vk::raii::DeviceMemory scratchMem = nullptr;
    {
        vk::BufferCreateInfo bufInfo;
        bufInfo.setSize(sizeInfo.buildScratchSize);
        bufInfo.setUsage(vk::BufferUsageFlagBits::eStorageBuffer
                       | vk::BufferUsageFlagBits::eShaderDeviceAddress);
        bufInfo.setSharingMode(vk::SharingMode::eExclusive);
        scratchBuf = vk::raii::Buffer(device->get(), bufInfo);

        auto memReqs = scratchBuf.getMemoryRequirements();
        scratchMem = allocMem(device->get(), memReqs,
            device->findMemoryType(memReqs.memoryTypeBits,
                vk::MemoryPropertyFlagBits::eDeviceLocal), true);
        scratchBuf.bindMemory(*scratchMem, 0);
    }

    vk::DeviceAddress scratchAddr = device->get().getBufferAddress({*scratchBuf});

    // Build
    buildGeoInfo.dstAccelerationStructure = asHandle;
    buildGeoInfo.scratchData.deviceAddress = scratchAddr;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = maxPrimCount;

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
    s_as.vkCmdBuildAccelerationStructuresKHR(static_cast<VkCommandBuffer>(*cmd), 1, &buildGeoInfo, &pRangeInfo);

    // Submit and wait
    submitAndWait(device->getGraphicsQueue(), device->get(), cmd);

    // Get device address
    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
    addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = asHandle;
    result.deviceAddress = s_as.vkGetAccelerationStructureDeviceAddressKHR(vkDev, &addrInfo);

    LOG_INFO("BLAS built: " + std::to_string(maxPrimCount) + " triangles, "
             + std::to_string(sizeInfo.accelerationStructureSize) + " bytes");

    return result;
}

// ── TLAS build ────────────────────────────────────────────────────

TLAS buildTLAS(VulkanDevice* device,
               const std::vector<VkAccelerationStructureInstanceKHR>& instances) {
    TLAS tlas;
    if (instances.empty()) return tlas;

    VkDevice vkDev = rawDev(device);
    s_as.load(vkDev);

    uint32_t instCount = static_cast<uint32_t>(instances.size());
    vk::DeviceSize instanceBufSize = instCount * sizeof(VkAccelerationStructureInstanceKHR);

    auto [pool, cmd] = beginOneShot(device->get(), device->getGraphicsQueueFamily());

    // Instance buffer
    {
        vk::BufferCreateInfo ibInfo;
        ibInfo.setSize(instanceBufSize);
        ibInfo.setUsage(vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR
                      | vk::BufferUsageFlagBits::eShaderDeviceAddress);
        ibInfo.setSharingMode(vk::SharingMode::eExclusive);
        tlas.instanceBuffer = vk::raii::Buffer(device->get(), ibInfo);

        auto ibMemReqs = tlas.instanceBuffer.getMemoryRequirements();
        uint32_t ibMemType = device->findMemoryType(ibMemReqs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        tlas.instanceMemory = allocMem(device->get(), ibMemReqs, ibMemType, true);
        tlas.instanceBuffer.bindMemory(*tlas.instanceMemory, 0);

        void* mapped = tlas.instanceMemory.mapMemory(0, instanceBufSize);
        memcpy(mapped, instances.data(), instanceBufSize);
        tlas.instanceMemory.unmapMemory();
    }

    tlas.instanceCount = instCount;
    vk::DeviceAddress instanceBufAddr = device->get().getBufferAddress({*tlas.instanceBuffer});

    // Query build sizes
    VkAccelerationStructureBuildGeometryInfoKHR buildGeoInfo{};
    buildGeoInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildGeoInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildGeoInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildGeoInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = instanceBufAddr;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

    buildGeoInfo.geometryCount = 1;
    buildGeoInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    s_as.vkGetAccelerationStructureBuildSizesKHR(vkDev,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildGeoInfo, &instCount, &sizeInfo);

    // Create AS buffer
    {
        vk::BufferCreateInfo bInfo;
        bInfo.setSize(sizeInfo.accelerationStructureSize);
        bInfo.setUsage(vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR
                     | vk::BufferUsageFlagBits::eShaderDeviceAddress);
        bInfo.setSharingMode(vk::SharingMode::eExclusive);
        tlas.buffer = vk::raii::Buffer(device->get(), bInfo);

        auto memReqs = tlas.buffer.getMemoryRequirements();
        tlas.memory = allocMem(device->get(), memReqs,
            device->findMemoryType(memReqs.memoryTypeBits,
                vk::MemoryPropertyFlagBits::eDeviceLocal), true);
        tlas.buffer.bindMemory(*tlas.memory, 0);
    }

    // Create AS handle
    VkAccelerationStructureCreateInfoKHR asCreateInfo{};
    asCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asCreateInfo.buffer = static_cast<VkBuffer>(*tlas.buffer);
    asCreateInfo.size = sizeInfo.accelerationStructureSize;
    asCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    VkAccelerationStructureKHR asHandle;
    VkResult res = s_as.vkCreateAccelerationStructureKHR(vkDev, &asCreateInfo, nullptr, &asHandle);
    if (res != VK_SUCCESS) {
        LOG_ERROR("vkCreateAccelerationStructureKHR failed: " + std::to_string(res));
        throw std::runtime_error("Failed to create TLAS");
    }
    tlas.handle = asHandle;

    // Scratch buffer
    vk::raii::Buffer scratchBuf = nullptr;
    vk::raii::DeviceMemory scratchMem = nullptr;
    {
        vk::BufferCreateInfo bInfo;
        bInfo.setSize(sizeInfo.buildScratchSize);
        bInfo.setUsage(vk::BufferUsageFlagBits::eStorageBuffer
                     | vk::BufferUsageFlagBits::eShaderDeviceAddress);
        bInfo.setSharingMode(vk::SharingMode::eExclusive);
        scratchBuf = vk::raii::Buffer(device->get(), bInfo);

        auto memReqs3 = scratchBuf.getMemoryRequirements();
        scratchMem = allocMem(device->get(), memReqs3,
            device->findMemoryType(memReqs3.memoryTypeBits,
                vk::MemoryPropertyFlagBits::eDeviceLocal), true);
        scratchBuf.bindMemory(*scratchMem, 0);
    }

    vk::DeviceAddress scratchAddr = device->get().getBufferAddress({*scratchBuf});

    // Build
    buildGeoInfo.dstAccelerationStructure = asHandle;
    buildGeoInfo.scratchData.deviceAddress = scratchAddr;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = instCount;

    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
    s_as.vkCmdBuildAccelerationStructuresKHR(static_cast<VkCommandBuffer>(*cmd), 1, &buildGeoInfo, &pRangeInfo);

    // Submit and wait
    submitAndWait(device->getGraphicsQueue(), device->get(), cmd);

    LOG_INFO("TLAS built: " + std::to_string(instCount) + " instances");
    return tlas;
}

void destroyBLAS(vk::Device device, BLAS& blas) {
    if (blas.handle) {
        VkDevice vkDev = static_cast<VkDevice>(device);
        s_as.load(vkDev);
        s_as.vkDestroyAccelerationStructureKHR(vkDev, static_cast<VkAccelerationStructureKHR>(blas.handle), nullptr);
        blas.handle = nullptr;
    }
    blas.buffer = nullptr;
    blas.memory = nullptr;
    blas.deviceAddress = 0;
}

void destroyTLAS(vk::Device device, TLAS& tlas) {
    if (tlas.handle) {
        VkDevice vkDev = static_cast<VkDevice>(device);
        s_as.load(vkDev);
        s_as.vkDestroyAccelerationStructureKHR(vkDev, static_cast<VkAccelerationStructureKHR>(tlas.handle), nullptr);
        tlas.handle = nullptr;
    }
    tlas.buffer = nullptr;
    tlas.memory = nullptr;
    tlas.instanceBuffer = nullptr;
    tlas.instanceMemory = nullptr;
    tlas.instanceCount = 0;
}

// ── Instance creation ─────────────────────────────────────────────

VkAccelerationStructureInstanceKHR makeInstance(
    const BLAS& blas, const glm::mat4& transform,
    uint32_t customIndex, uint32_t hitGroupIndex) {
    VkAccelerationStructureInstanceKHR inst{};
    inst.accelerationStructureReference = blas.deviceAddress;
    inst.instanceCustomIndex = customIndex;
    inst.mask = 0xFF;
    inst.instanceShaderBindingTableRecordOffset = hitGroupIndex;
    inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++)
            inst.transform.matrix[r][c] = transform[c][r];

    return inst;
}

// ── Sphere tessellation ─────────────────────────────────────────

std::pair<std::vector<glm::vec3>, std::vector<uint32_t>>
tessellateSphere(const glm::vec3& center, float radius, uint32_t subdivisions) {
    std::vector<glm::vec3> pos;
    std::vector<uint32_t> idx;

    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

    auto addVertex = [&](float x, float y, float z) {
        float len = std::sqrt(x*x + y*y + z*z);
        pos.emplace_back(center.x + radius * x/len,
                         center.y + radius * y/len,
                         center.z + radius * z/len);
    };

    addVertex(-1,  t,  0); addVertex( 1,  t,  0);
    addVertex(-1, -t,  0); addVertex( 1, -t,  0);
    addVertex( 0, -1,  t); addVertex( 0,  1,  t);
    addVertex( 0, -1, -t); addVertex( 0,  1, -t);
    addVertex( t,  0, -1); addVertex( t,  0,  1);
    addVertex(-t,  0, -1); addVertex(-t,  0,  1);

    uint32_t baseIdx[] = {
        0,11,5,  0,5,1,  0,1,7,  0,7,10, 0,10,11,
        1,5,9,  5,11,4,  11,10,2, 10,7,6,  7,1,8,
        3,9,4,  3,4,2,  3,2,6,  3,6,8,  3,8,9,
        4,9,5,  2,4,11, 6,2,10, 8,6,7,  9,8,1
    };
    idx.assign(std::begin(baseIdx), std::end(baseIdx));

    std::unordered_map<uint64_t, uint32_t> edgeMap;
    auto midPoint = [&](uint32_t a, uint32_t b) -> uint32_t {
        uint64_t key = (uint64_t)std::min(a,b) << 32 | std::max(a,b);
        auto it = edgeMap.find(key);
        if (it != edgeMap.end()) return it->second;
        glm::vec3 mid = glm::normalize(pos[a] + pos[b]);
        uint32_t idx2 = static_cast<uint32_t>(pos.size());
        pos.emplace_back(center + radius * mid);
        edgeMap[key] = idx2;
        return idx2;
    };

    for (uint32_t s = 0; s < subdivisions; s++) {
        std::vector<uint32_t> newIdx;
        for (size_t i = 0; i + 2 < idx.size(); i += 3) {
            uint32_t a = idx[i], b = idx[i+1], c = idx[i+2];
            uint32_t ab = midPoint(a, b);
            uint32_t bc = midPoint(b, c);
            uint32_t ca = midPoint(c, a);
            newIdx.insert(newIdx.end(), {a,ab,ca, ab,b,bc, ca,bc,c, ab,bc,ca});
        }
        idx = std::move(newIdx);
        edgeMap.clear();
    }

    return {pos, idx};
}

} // namespace RYRayTracing
