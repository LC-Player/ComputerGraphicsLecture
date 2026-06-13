// GeometryManager.cpp

#include "GeometryManager.hpp"
#include "vulkan/Buffer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "core/Logger.hpp"

#include "tiny_obj_loader.h"
#include "Welzl.h"

#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <cstring>

// AS-build-compatible buffer usage: storage + acceleration structure input + device address
static constexpr vk::BufferUsageFlags kGeometryBufferUsage =
    vk::BufferUsageFlagBits::eStorageBuffer
    | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR
    | vk::BufferUsageFlagBits::eShaderDeviceAddress;

namespace RYRayTracing {

// ── Model loading helpers (anonymous namespace) ──────────────────

namespace {

void deduplicateOBJVertices(const tinyobj::attrib_t& attrib,
                             const std::vector<tinyobj::shape_t>& shapes,
                             ModelSource& src) {
    std::unordered_map<uint64_t, uint32_t> uniqueMap;
    for (const auto& shape : shapes) {
        for (const auto& idx : shape.mesh.indices) {
            uint64_t key = static_cast<uint64_t>(idx.vertex_index) << 32
                         | static_cast<uint64_t>(static_cast<uint16_t>(idx.texcoord_index)) << 16
                         | static_cast<uint64_t>(static_cast<uint16_t>(idx.normal_index));
            if (!uniqueMap.contains(key)) {
                uniqueMap[key] = static_cast<uint32_t>(src.positions.size());
                src.positions.emplace_back(
                    attrib.vertices[3 * idx.vertex_index + 0],
                    attrib.vertices[3 * idx.vertex_index + 1],
                    attrib.vertices[3 * idx.vertex_index + 2]);
                if (idx.normal_index >= 0) {
                    src.normals.emplace_back(
                        attrib.normals[3 * idx.normal_index + 0],
                        attrib.normals[3 * idx.normal_index + 1],
                        attrib.normals[3 * idx.normal_index + 2]);
                } else {
                    src.normals.emplace_back(0.0f, 0.0f, 0.0f);
                }
                if (idx.texcoord_index >= 0) {
                    src.texCoords.emplace_back(
                        attrib.texcoords[2 * idx.texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]);
                } else {
                    src.texCoords.emplace_back(0.0f, 0.0f);
                }
            }
            src.indices.push_back(uniqueMap[key]);
        }
    }
}

void computeSmoothNormalsIfNeeded(ModelSource& src) {
    bool hasNormals = false;
    for (const auto& n : src.normals) {
        if (glm::dot(n, n) > 0.0f) { hasNormals = true; break; }
    }
    if (hasNormals || src.indices.size() < 3) return;

    std::vector<glm::vec3> accum(src.positions.size(), glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < src.indices.size(); i += 3) {
        uint32_t i0 = src.indices[i], i1 = src.indices[i + 1], i2 = src.indices[i + 2];
        glm::vec3 faceN = glm::cross(
            src.positions[i1] - src.positions[i0],
            src.positions[i2] - src.positions[i0]);
        accum[i0] += faceN; accum[i1] += faceN; accum[i2] += faceN;
    }
    for (auto& n : accum) {
        float len2 = glm::dot(n, n);
        n = (len2 > 1e-12f) ? n / std::sqrt(len2) : glm::vec3(0.0f, 1.0f, 0.0f);
    }
    src.normals = std::move(accum);
}

} // anonymous namespace

// ── Construction ─────────────────────────────────────────────────

GeometryManager::GeometryManager() = default;

void GeometryManager::setFramesInFlight(uint32_t fif) {
    m_framesInFlight = fif;
}

// ── Model loading ────────────────────────────────────────────────

int GeometryManager::addModelSource(const std::string& objPath, const std::string& texturePath) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, objPath.c_str())) {
        LOG_ERROR("Failed to load model: " + warn + err);
        return -1;
    }

    ModelSource src;
    src.name = objPath;
    src.texturePath = texturePath;
    src.bvhRoot = -1; // no BVH in hardware RT path

    deduplicateOBJVertices(attrib, shapes, src);
    computeSmoothNormalsIfNeeded(src);

    WelzlSphere ws = computeMinEnclosingSphere(src.positions);
    src.boundingSphereCenter = ws.center;
    src.boundingSphereRadius = ws.radius;

    int idx = static_cast<int>(m_modelSources.size());
    m_modelSources.push_back(std::move(src));
    LOG_INFO("Loaded model '" + objPath + "': " +
             std::to_string(m_modelSources.back().positions.size()) + " vertices, " +
             std::to_string(m_modelSources.back().indices.size()) + " indices" +
             (texturePath.empty() ? "" : " (textured)"));
    return idx;
}

// ── Texture creation ─────────────────────────────────────────────

void GeometryManager::uploadImageWithStaging(VulkanDevice& device, vk::Image image,
                                              uint32_t width, uint32_t height,
                                              const void* data, size_t dataSize) {
    Buffer staging(&device, {dataSize, vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent});
    staging.copyFrom(data, dataSize);

    vk::CommandPoolCreateInfo poolInfo;
    poolInfo.setFlags(vk::CommandPoolCreateFlagBits::eTransient);
    poolInfo.setQueueFamilyIndex(device.getGraphicsQueueFamily());
    vk::raii::CommandPool cmdPool{device.get(), poolInfo};

    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.setCommandPool(*cmdPool);
    allocInfo.setLevel(vk::CommandBufferLevel::ePrimary);
    allocInfo.setCommandBufferCount(1);
    vk::raii::CommandBuffers cmdBufs{device.get(), allocInfo};
    auto& cmd = cmdBufs[0];

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cmd.begin(beginInfo);

    vk::ImageMemoryBarrier barrier;
    barrier.setOldLayout(vk::ImageLayout::eUndefined);
    barrier.setNewLayout(vk::ImageLayout::eTransferDstOptimal);
    barrier.setImage(image);
    barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    barrier.setDstAccessMask(vk::AccessFlagBits::eTransferWrite);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eTransfer,
                        {}, nullptr, nullptr, barrier);

    vk::BufferImageCopy region;
    region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
    region.imageExtent = vk::Extent3D{width, height, 1};
    cmd.copyBufferToImage(*staging.get(), image,
                          vk::ImageLayout::eTransferDstOptimal, region);

    barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal);
    barrier.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
    barrier.setDstAccessMask(vk::AccessFlagBits::eShaderRead);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eComputeShader,
                        {}, nullptr, nullptr, barrier);

    cmd.end();

    vk::FenceCreateInfo fenceInfo;
    vk::raii::Fence fence{device.get(), fenceInfo};
    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*cmd);
    device.getGraphicsQueue().submit(submitInfo, *fence);
    (void)device.get().waitForFences(*fence, true, UINT64_MAX);
}

void GeometryManager::createDummyTexture(VulkanDevice& device) {
    uint32_t white = 0xFFFFFFFF;
    vk::ImageCreateInfo imageInfo;
    imageInfo.setImageType(vk::ImageType::e2D);
    imageInfo.setExtent(vk::Extent3D{1, 1, 1});
    imageInfo.setMipLevels(1);
    imageInfo.setArrayLayers(1);
    imageInfo.setFormat(vk::Format::eR8G8B8A8Unorm);
    imageInfo.setTiling(vk::ImageTiling::eOptimal);
    imageInfo.setInitialLayout(vk::ImageLayout::eUndefined);
    imageInfo.setUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
    imageInfo.setSamples(vk::SampleCountFlagBits::e1);
    m_dummyTextureImage = device.get().createImage(imageInfo);

    auto memReqs = m_dummyTextureImage.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo;
    allocInfo.setAllocationSize(memReqs.size);
    allocInfo.setMemoryTypeIndex(device.findMemoryType(memReqs.memoryTypeBits,
        vk::MemoryPropertyFlagBits::eDeviceLocal));
    m_dummyTextureMemory = device.get().allocateMemory(allocInfo);
    m_dummyTextureImage.bindMemory(*m_dummyTextureMemory, 0);

    uploadImageWithStaging(device, *m_dummyTextureImage, 1, 1, &white, 4);

    vk::ImageViewCreateInfo viewInfo;
    viewInfo.setImage(*m_dummyTextureImage);
    viewInfo.setViewType(vk::ImageViewType::e2D);
    viewInfo.setFormat(vk::Format::eR8G8B8A8Unorm);
    viewInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    m_dummyTextureView = device.get().createImageView(viewInfo);

    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.setMagFilter(vk::Filter::eLinear);
    samplerInfo.setMinFilter(vk::Filter::eLinear);
    samplerInfo.setAddressModeU(vk::SamplerAddressMode::eRepeat);
    samplerInfo.setAddressModeV(vk::SamplerAddressMode::eRepeat);
    samplerInfo.setAddressModeW(vk::SamplerAddressMode::eRepeat);
    m_textureSampler = device.get().createSampler(samplerInfo);

    LOG_INFO("Dummy texture and sampler created");
}

void GeometryManager::createTextures(VulkanDevice& device) {
    while (m_textures.size() < m_modelSources.size()) {
        size_t i = m_textures.size();
        if (!m_modelSources[i].texturePath.empty()) {
            m_textures.push_back(std::make_unique<Texture>(&device,
                TextureConfig{m_modelSources[i].texturePath}));
        } else {
            m_textures.push_back(nullptr);
        }
    }
}

void GeometryManager::createEnvmap(VulkanDevice& device, const std::string& path) {
    TextureConfig cfg;
    cfg.filepath = path;
    cfg.addressMode = vk::SamplerAddressMode::eRepeat;
    cfg.magFilter = vk::Filter::eLinear;
    cfg.minFilter = vk::Filter::eLinear;
    m_envmapTexture = std::make_unique<Texture>(&device, cfg);
    LOG_INFO("Envmap texture created");
}

// ── Geometry buffer building ─────────────────────────────────────

void GeometryManager::createGeometryBuffers(VulkanDevice& device,
                                             std::vector<ModelRef>& modelRefs,
                                             const std::vector<int>& modelRefSourceIdx,
                                             const std::vector<Transform>& modelRefTransforms) {
    m_vertexBuffers.clear();
    m_indexBuffers.clear();
    m_modelRefBuffers.clear();

    std::vector<GPUVertex> mergedVerts;
    std::vector<uint32_t> mergedIndices;

    for (auto& src : m_modelSources) {
        src.vertexOffset = static_cast<uint32_t>(mergedVerts.size());
        src.firstIndex  = static_cast<uint32_t>(mergedIndices.size());
        src.indexCount  = static_cast<uint32_t>(src.indices.size());

        for (size_t v = 0; v < src.positions.size(); v++) {
            GPUVertex gv;
            gv.position = src.positions[v];
            gv.normal = v < src.normals.size() ? src.normals[v] : glm::vec3(0.0f);
            gv.texCoord = v < src.texCoords.size() ? src.texCoords[v] : glm::vec2(0.0f);
            gv._pad[0] = 0.0f; gv._pad[1] = 0.0f;
            mergedVerts.push_back(gv);
        }
        mergedIndices.insert(mergedIndices.end(), src.indices.begin(), src.indices.end());
    }

    if (mergedVerts.empty()) { mergedVerts.push_back({}); mergedIndices.push_back(0); }

    for (size_t i = 0; i < modelRefs.size(); i++) {
        const auto& src = m_modelSources[modelRefSourceIdx[i]];
        modelRefs[i].vertexOffset = src.vertexOffset;
        modelRefs[i].firstIndex  = src.firstIndex;
        modelRefs[i].indexCount  = src.indexCount;
        modelRefs[i].boundingSphereCenter = src.boundingSphereCenter;
        modelRefs[i].boundingSphereRadius = src.boundingSphereRadius;
        modelRefs[i].invTransform = glm::inverse(modelRefTransforms[i].transform());
        modelRefs[i].textureIndex = modelRefSourceIdx[i];
        modelRefs[i].bvhRoot = -1;
    }

    for (size_t i = 0; i < m_framesInFlight; i++) {
        m_vertexBuffers.push_back(Buffer::createBuffer(&device,
            kMaxVertices * sizeof(GPUVertex),
            kGeometryBufferUsage,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
        m_vertexBuffers.back().copyFrom(mergedVerts.data(), mergedVerts.size() * sizeof(GPUVertex));

        m_indexBuffers.push_back(Buffer::createBuffer(&device,
            kMaxIndices * sizeof(uint32_t),
            kGeometryBufferUsage,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
        m_indexBuffers.back().copyFrom(mergedIndices.data(), mergedIndices.size() * sizeof(uint32_t));

        m_modelRefBuffers.push_back(Buffer::createBuffer(&device,
            kMaxModelRefs * sizeof(ModelRef),
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
        m_modelRefBuffers.back().copyFrom(modelRefs.data(), modelRefs.size() * sizeof(ModelRef));
    }

    LOG_INFO("Geometry built: " + std::to_string(mergedVerts.size()) + " vertices, " +
             std::to_string(mergedIndices.size()) + " indices, " +
             std::to_string(modelRefs.size()) + " model refs");
}

void GeometryManager::updateModelRefBuffer(size_t currentFrame,
                                            const std::vector<ModelRef>& modelRefs) {
    if (modelRefs.empty()) return;
    m_modelRefBuffers[currentFrame].copyFrom(modelRefs.data(), modelRefs.size() * sizeof(ModelRef));
}

} // namespace RYRayTracing
