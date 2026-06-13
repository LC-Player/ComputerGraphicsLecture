// DescriptorManager.cpp

#include "DescriptorManager.hpp"
#include "Camera.h"
#include "core/Logger.hpp"

namespace RYRayTracing {

// ── Layout creation ────────────────────────────────────────────────

void DescriptorManager::createTLASLayout() {
    vk::DescriptorSetLayoutBinding binding;
    binding.setBinding(0);
    binding.setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR);
    binding.setDescriptorCount(1);
    binding.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR
                        | vk::ShaderStageFlagBits::eClosestHitKHR);
    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.setBindings(binding);
    m_tlasSetLayout = m_device->createDescriptorSetLayout(layoutInfo);
}

void DescriptorManager::createPerFrameLayout() {
    std::array<vk::DescriptorSetLayoutBinding, 5> bindings;
    bindings[0] = {0, vk::DescriptorType::eUniformBuffer, 1,
        vk::ShaderStageFlagBits::eRaygenKHR};
    bindings[1] = {1, vk::DescriptorType::eStorageBuffer, 1,
        vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eMissKHR | vk::ShaderStageFlagBits::eRaygenKHR};
    bindings[2] = {2, vk::DescriptorType::eStorageImage, 1,
        vk::ShaderStageFlagBits::eRaygenKHR};
    bindings[3] = {3, vk::DescriptorType::eStorageBuffer, 1,
        vk::ShaderStageFlagBits::eClosestHitKHR};
    bindings[4] = {4, vk::DescriptorType::eStorageImage, 1,
        vk::ShaderStageFlagBits::eRaygenKHR};
    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.setBindings(bindings);
    m_perFrameSetLayout = m_device->createDescriptorSetLayout(layoutInfo);
}

void DescriptorManager::createSceneLayout(uint32_t maxTextures) {
    std::array<vk::DescriptorSetLayoutBinding, 6> bindings;
    bindings[0] = {0, vk::DescriptorType::eStorageBuffer, 1,
        vk::ShaderStageFlagBits::eClosestHitKHR};
    bindings[1] = {1, vk::DescriptorType::eStorageBuffer, 1,
        vk::ShaderStageFlagBits::eClosestHitKHR};
    bindings[2] = {2, vk::DescriptorType::eStorageBuffer, 1,
        vk::ShaderStageFlagBits::eClosestHitKHR};
    bindings[3] = {3, vk::DescriptorType::eStorageBuffer, 1,
        vk::ShaderStageFlagBits::eClosestHitKHR};
    bindings[4] = {4, vk::DescriptorType::eCombinedImageSampler, maxTextures,
        vk::ShaderStageFlagBits::eClosestHitKHR};
    bindings[5] = {5, vk::DescriptorType::eCombinedImageSampler, 1,
        vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eMissKHR | vk::ShaderStageFlagBits::eRaygenKHR};
    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.setBindings(bindings);
    m_sceneSetLayout = m_device->createDescriptorSetLayout(layoutInfo);
}

void DescriptorManager::createSamplerLayout() {
    vk::DescriptorSetLayoutBinding binding{0, vk::DescriptorType::eCombinedImageSampler, 1,
        vk::ShaderStageFlagBits::eFragment};
    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.setBindings(binding);
    m_samplerSetLayout = m_device->createDescriptorSetLayout(layoutInfo);
}

void DescriptorManager::createLayouts(uint32_t maxTextures) {
    LOG_INFO("Creating descriptor set layouts...");
    createTLASLayout();
    createPerFrameLayout();
    createSceneLayout(maxTextures);
    createSamplerLayout();
    LOG_INFO("Descriptor set layouts created");
}

// ── Pool creation ──────────────────────────────────────────────────

void DescriptorManager::createDescriptorPool(uint32_t framesInFlight, uint32_t maxTextures) {
    std::array<vk::DescriptorPoolSize, 5> poolSizes;
    poolSizes[0] = {vk::DescriptorType::eAccelerationStructureKHR, framesInFlight};
    poolSizes[1] = {vk::DescriptorType::eUniformBuffer, framesInFlight};
    poolSizes[2] = {vk::DescriptorType::eStorageBuffer, framesInFlight * 8};
    poolSizes[3] = {vk::DescriptorType::eStorageImage, framesInFlight * 2};
    poolSizes[4] = {vk::DescriptorType::eCombinedImageSampler,
                    framesInFlight * (maxTextures + 2)};

    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
    poolInfo.setMaxSets(framesInFlight * 4);
    poolInfo.setPoolSizes(poolSizes);
    m_descriptorPool = m_device->createDescriptorPool(poolInfo);
    LOG_INFO("Descriptor pool created");
}

void DescriptorManager::createImGuiPool() {
    vk::DescriptorPoolSize poolSize(vk::DescriptorType::eCombinedImageSampler, 1000);
    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
    poolInfo.setMaxSets(1000);
    poolInfo.setPoolSizes(poolSize);
    m_imguiPool = m_device->createDescriptorPool(poolInfo);
}

// ── TLAS descriptor sets (allocated only; handle written per-frame) ──

void DescriptorManager::createTLASSets(uint32_t framesInFlight) {
    std::vector<vk::DescriptorSetLayout> layouts(framesInFlight, *m_tlasSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.setDescriptorPool(*m_descriptorPool);
    allocInfo.setSetLayouts(layouts);
    m_tlasSets = m_device->allocateDescriptorSets(allocInfo);
}

void DescriptorManager::writeTLASBinding(size_t frame, vk::AccelerationStructureKHR tlas) {
    vk::WriteDescriptorSetAccelerationStructureKHR asInfo;
    asInfo.setAccelerationStructures(tlas);
    vk::WriteDescriptorSet write;
    write.setDstSet(*m_tlasSets[frame]);
    write.setDstBinding(0);
    write.setDescriptorCount(1);
    write.setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR);
    write.pNext = &asInfo;
    m_device->updateDescriptorSets(write, nullptr);
}

// ── Per-frame descriptor sets (set 1) ──────────────────────────────

void DescriptorManager::createPerFrameSets(uint32_t framesInFlight,
                                            const PtDescriptorBindings& b) {
    std::vector<vk::DescriptorSetLayout> layouts(framesInFlight, *m_perFrameSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.setDescriptorPool(*m_descriptorPool);
    allocInfo.setSetLayouts(layouts);
    m_perFrameSets = m_device->allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < framesInFlight; ++i) {
        std::vector<vk::WriteDescriptorSet> writes;

        // b0: camera UBO
        if (b.cameraBuffers && !b.cameraBuffers->empty()) {
            vk::DescriptorBufferInfo info{(*b.cameraBuffers)[i], 0, sizeof(CameraData)};
            writes.push_back({*m_perFrameSets[i], 0, 0, 1, vk::DescriptorType::eUniformBuffer,
                              nullptr, &info, nullptr});
        }

        // b1: lights SSBO
        if (b.lightBuffers && !b.lightBuffers->empty()) {
            vk::DescriptorBufferInfo info{(*b.lightBuffers)[i], 0, VK_WHOLE_SIZE};
            writes.push_back({*m_perFrameSets[i], 1, 0, 1, vk::DescriptorType::eStorageBuffer,
                              nullptr, &info, nullptr});
        }

        // b2: output storage image
        if (b.rtImageViews && !b.rtImageViews->empty()) {
            vk::DescriptorImageInfo info{nullptr, (*b.rtImageViews)[i], vk::ImageLayout::eGeneral};
            writes.push_back({*m_perFrameSets[i], 2, 0, 1, vk::DescriptorType::eStorageImage,
                              &info, nullptr, nullptr});
        }

        // b3: instance data SSBO (single buffer, shared across frames)
        if (b.instanceDataBuffer) {
            vk::DescriptorBufferInfo info{b.instanceDataBuffer, 0, VK_WHOLE_SIZE};
            writes.push_back({*m_perFrameSets[i], 3, 0, 1, vk::DescriptorType::eStorageBuffer,
                              nullptr, &info, nullptr});
        }

        // b4: accumulation image (single image, shared across frames)
        if (b.accumImageView) {
            vk::DescriptorImageInfo info{nullptr, b.accumImageView, vk::ImageLayout::eGeneral};
            writes.push_back({*m_perFrameSets[i], 4, 0, 1, vk::DescriptorType::eStorageImage,
                              &info, nullptr, nullptr});
        }

        m_device->updateDescriptorSets(writes, nullptr);
    }
    LOG_INFO("Per-frame descriptor sets created");
}

// ── Scene descriptor sets (set 2) ──────────────────────────────────

void DescriptorManager::createSceneSets(uint32_t framesInFlight,
                                         const PtDescriptorBindings& b) {
    std::vector<vk::DescriptorSetLayout> layouts(framesInFlight, *m_sceneSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.setDescriptorPool(*m_descriptorPool);
    allocInfo.setSetLayouts(layouts);
    m_sceneSets = m_device->allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < framesInFlight; ++i) {
        std::vector<vk::WriteDescriptorSet> writes;

        // b0: materials SSBO
        if (b.materialBuffers && !b.materialBuffers->empty()) {
            vk::DescriptorBufferInfo info{(*b.materialBuffers)[i], 0, VK_WHOLE_SIZE};
            writes.push_back({*m_sceneSets[i], 0, 0, 1, vk::DescriptorType::eStorageBuffer,
                              nullptr, &info, nullptr});
        }
        // b1: vertices SSBO
        if (b.vertexBuffers && !b.vertexBuffers->empty()) {
            vk::DescriptorBufferInfo info{(*b.vertexBuffers)[i], 0, VK_WHOLE_SIZE};
            writes.push_back({*m_sceneSets[i], 1, 0, 1, vk::DescriptorType::eStorageBuffer,
                              nullptr, &info, nullptr});
        }
        // b2: indices SSBO
        if (b.indexBuffers && !b.indexBuffers->empty()) {
            vk::DescriptorBufferInfo info{(*b.indexBuffers)[i], 0, VK_WHOLE_SIZE};
            writes.push_back({*m_sceneSets[i], 2, 0, 1, vk::DescriptorType::eStorageBuffer,
                              nullptr, &info, nullptr});
        }
        // b3: modelRefs SSBO
        if (b.modelRefBuffers && !b.modelRefBuffers->empty()) {
            vk::DescriptorBufferInfo info{(*b.modelRefBuffers)[i], 0, VK_WHOLE_SIZE};
            writes.push_back({*m_sceneSets[i], 3, 0, 1, vk::DescriptorType::eStorageBuffer,
                              nullptr, &info, nullptr});
        }

        // b4: texture array
        {
            std::array<vk::DescriptorImageInfo, 8> texInfos;
            for (size_t t = 0; t < b.maxTextures; t++) {
                texInfos[t].setSampler(b.textureSampler);
                texInfos[t].setImageView(
                    (b.textureViews && t < b.textureViews->size())
                        ? (*b.textureViews)[t] : b.dummyTextureView);
                texInfos[t].setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
            }
            vk::WriteDescriptorSet texWrite;
            texWrite.setDstSet(*m_sceneSets[i]);
            texWrite.setDstBinding(4);
            texWrite.setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
            texWrite.setDescriptorCount(b.maxTextures);
            texWrite.setImageInfo(texInfos);
            writes.push_back(texWrite);
        }

        // b5: envmap
        if (b.envmapInfo.imageView) {
            vk::WriteDescriptorSet w;
            w.setDstSet(*m_sceneSets[i]);
            w.setDstBinding(5);
            w.setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
            w.setDescriptorCount(1);
            w.setImageInfo(b.envmapInfo);
            writes.push_back(w);
        }

        m_device->updateDescriptorSets(writes, nullptr);
    }
    LOG_INFO("Scene descriptor sets created");
}

// ── Fullscreen descriptor sets ─────────────────────────────────────

void DescriptorManager::createFullscreenSets(uint32_t framesInFlight,
                                              const std::vector<vk::ImageView>& rtImageViews,
                                              vk::Sampler rtSampler) {
    std::vector<vk::DescriptorSetLayout> layouts(framesInFlight, *m_samplerSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.setDescriptorPool(*m_descriptorPool);
    allocInfo.setSetLayouts(layouts);
    m_fullscreenSets = m_device->allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < framesInFlight; i++) {
        vk::DescriptorImageInfo info{rtSampler, rtImageViews[i], vk::ImageLayout::eGeneral};
        vk::WriteDescriptorSet write{*m_fullscreenSets[i], 0, 0, 1,
            vk::DescriptorType::eCombinedImageSampler, &info, nullptr, nullptr};
        m_device->updateDescriptorSets(write, nullptr);
    }
    LOG_INFO("Fullscreen descriptor sets created");
}

// ── Swapchain-recreation rebinding ─────────────────────────────────

void DescriptorManager::updatePerFrameImageBindings(size_t frame,
                                                     vk::ImageView rtView,
                                                     vk::ImageView accumView) {
    std::vector<vk::WriteDescriptorSet> writes;

    vk::DescriptorImageInfo rtInfo{nullptr, rtView, vk::ImageLayout::eGeneral};
    writes.push_back({*m_perFrameSets[frame], 2, 0, 1, vk::DescriptorType::eStorageImage,
                      &rtInfo, nullptr, nullptr});

    vk::DescriptorImageInfo accumInfo{nullptr, accumView, vk::ImageLayout::eGeneral};
    writes.push_back({*m_perFrameSets[frame], 4, 0, 1, vk::DescriptorType::eStorageImage,
                      &accumInfo, nullptr, nullptr});

    m_device->updateDescriptorSets(writes, nullptr);
}

void DescriptorManager::updateFullscreenBinding(size_t frame, vk::ImageView newView,
                                                  vk::Sampler sampler) {
    vk::DescriptorImageInfo info{sampler, newView, vk::ImageLayout::eGeneral};
    vk::WriteDescriptorSet write{*m_fullscreenSets[frame], 0, 0, 1,
        vk::DescriptorType::eCombinedImageSampler, &info, nullptr, nullptr};
    m_device->updateDescriptorSets(write, nullptr);
}

} // namespace RYRayTracing
