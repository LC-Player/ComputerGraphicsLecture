// DescriptorManager.cpp

#include "DescriptorManager.hpp"
#include "Camera.h"
#include "core/Logger.hpp"

namespace RYRayTracing {

// ── Layout creation ────────────────────────────────────────────────

void DescriptorManager::createPerFrameLayout() {
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings;
    bindings[0].binding = 0;
    bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eCompute;

    bindings[1].binding = 1;
    bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.setBindings(bindings);
    m_perFrameSetLayout = m_device->createDescriptorSetLayout(layoutInfo);
}

void DescriptorManager::createSamplerLayout() {
    vk::DescriptorSetLayoutBinding binding;
    binding.binding = 0;
    binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    binding.descriptorCount = 1;
    binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.setBindings(binding);
    m_samplerSetLayout = m_device->createDescriptorSetLayout(layoutInfo);
}

void DescriptorManager::createRtResourceLayout() {
    std::array<vk::DescriptorSetLayoutBinding, 10> bindings;

    bindings[0].binding = 0;
    bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

    bindings[1].binding = 1;
    bindings[1].descriptorType = vk::DescriptorType::eStorageImage;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

    bindings[2].binding = 2;
    bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;

    bindings[3].binding = 3;
    bindings[3].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = vk::ShaderStageFlagBits::eCompute;

    bindings[4].binding = 4;
    bindings[4].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = vk::ShaderStageFlagBits::eCompute;

    bindings[5].binding = 5;
    bindings[5].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = vk::ShaderStageFlagBits::eCompute;

    bindings[6].binding = 6;
    bindings[6].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    bindings[6].descriptorCount = 8; // kMaxTextures — bound as array
    bindings[6].stageFlags = vk::ShaderStageFlagBits::eCompute;

    bindings[7].binding = 7;
    bindings[7].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = vk::ShaderStageFlagBits::eCompute;

    bindings[8].binding = 8;
    bindings[8].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[8].descriptorCount = 1;
    bindings[8].stageFlags = vk::ShaderStageFlagBits::eCompute;

    bindings[9].binding = 9;
    bindings[9].descriptorType = vk::DescriptorType::eStorageBuffer;
    bindings[9].descriptorCount = 1;
    bindings[9].stageFlags = vk::ShaderStageFlagBits::eCompute;

    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.setBindings(bindings);
    m_rtResourceSetLayout = m_device->createDescriptorSetLayout(layoutInfo);
}

void DescriptorManager::createLayouts() {
    LOG_INFO("Creating descriptor set layouts...");
    createPerFrameLayout();
    createSamplerLayout();
    createRtResourceLayout();
    LOG_INFO("Descriptor set layouts created");
}

// ── Pool creation ──────────────────────────────────────────────────

void DescriptorManager::createDescriptorPool(uint32_t framesInFlight, uint32_t maxTextures) {
    std::array<vk::DescriptorPoolSize, 4> poolSizes;
    poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
    poolSizes[0].descriptorCount = framesInFlight;
    poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
    poolSizes[1].descriptorCount = framesInFlight * (maxTextures + 2);
    poolSizes[2].type = vk::DescriptorType::eStorageBuffer;
    poolSizes[2].descriptorCount = framesInFlight * 8;
    poolSizes[3].type = vk::DescriptorType::eStorageImage;
    poolSizes[3].descriptorCount = framesInFlight;

    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = framesInFlight * 4;
    poolInfo.setPoolSizes(poolSizes);
    m_descriptorPool = m_device->createDescriptorPool(poolInfo);
    LOG_INFO("Descriptor pool created");
}

void DescriptorManager::createImGuiPool() {
    vk::DescriptorPoolSize poolSize(vk::DescriptorType::eCombinedImageSampler, 1000);
    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = 1000;
    poolInfo.setPoolSizes(poolSize);
    m_imguiPool = m_device->createDescriptorPool(poolInfo);
}

// ── Per-frame descriptor sets ──────────────────────────────────────

void DescriptorManager::createPerFrameSets(uint32_t framesInFlight,
                                            const std::vector<vk::Buffer>& cameraBuffers,
                                            const std::vector<vk::Buffer>& lightBuffers) {
    std::vector<vk::DescriptorSetLayout> layouts(framesInFlight, *m_perFrameSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = *m_descriptorPool;
    allocInfo.setSetLayouts(layouts);
    m_perFrameSets = m_device->allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < framesInFlight; ++i) {
        vk::DescriptorBufferInfo cameraInfo;
        cameraInfo.buffer = cameraBuffers[i];
        cameraInfo.offset = 0;
        cameraInfo.range = sizeof(CameraData);

        vk::DescriptorBufferInfo lightInfo;
        lightInfo.buffer = lightBuffers[i];
        lightInfo.offset = 0;
        lightInfo.range = VK_WHOLE_SIZE;

        std::array<vk::WriteDescriptorSet, 2> writes;
        writes[0].dstSet = *m_perFrameSets[i];
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[0].setBufferInfo(cameraInfo);

        writes[1].dstSet = *m_perFrameSets[i];
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].setBufferInfo(lightInfo);

        m_device->updateDescriptorSets(writes, nullptr);
    }
    LOG_INFO("Per-frame descriptor sets created");
}

// ── RT resource descriptor sets ────────────────────────────────────

void DescriptorManager::writeBufferBinding(size_t frame, uint32_t binding,
                                            vk::Buffer buffer, vk::DescriptorType type) {
    vk::DescriptorBufferInfo bufferInfo;
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;

    vk::WriteDescriptorSet write;
    write.dstSet = *m_rtSets[frame];
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = type;
    write.setBufferInfo(bufferInfo);
    m_device->updateDescriptorSets(write, nullptr);
}

void DescriptorManager::writeImageBinding(size_t frame, uint32_t binding,
                                           vk::ImageView view, vk::ImageLayout layout,
                                           vk::DescriptorType type) {
    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageView = view;
    imageInfo.imageLayout = layout;

    vk::WriteDescriptorSet write;
    write.dstSet = *m_rtSets[frame];
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = type;
    write.setImageInfo(imageInfo);
    m_device->updateDescriptorSets(write, nullptr);
}

void DescriptorManager::writeSamplerArrayBinding(size_t frame, uint32_t binding,
                                                   uint32_t count,
                                                   const std::array<vk::DescriptorImageInfo, 8>& infos) {
    vk::WriteDescriptorSet write;
    write.dstSet = *m_rtSets[frame];
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = count;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.setImageInfo(infos);
    m_device->updateDescriptorSets(write, nullptr);
}

void DescriptorManager::writeSamplerBinding(size_t frame, uint32_t binding,
                                             vk::DescriptorImageInfo info) {
    vk::WriteDescriptorSet write;
    write.dstSet = *m_rtSets[frame];
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.setImageInfo(info);
    m_device->updateDescriptorSets(write, nullptr);
}

void DescriptorManager::createRtResourceSets(uint32_t framesInFlight,
                                              const RtResourceBindings& b) {
    std::vector<vk::DescriptorSetLayout> layouts(framesInFlight, *m_rtResourceSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = *m_descriptorPool;
    allocInfo.setSetLayouts(layouts);
    m_rtSets = m_device->allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < framesInFlight; ++i) {
        // b0: sphere SSBO
        if (b.sphereBuffers && !b.sphereBuffers->empty())
            writeBufferBinding(i, 0, (*b.sphereBuffers)[i]);

        // b1: output storage image
        if (b.rtImageViews && !b.rtImageViews->empty())
            writeImageBinding(i, 1, (*b.rtImageViews)[i], vk::ImageLayout::eGeneral,
                              vk::DescriptorType::eStorageImage);

        // b2: material SSBO
        if (b.materialBuffers && !b.materialBuffers->empty())
            writeBufferBinding(i, 2, (*b.materialBuffers)[i]);

        // b3: vertex SSBO
        if (b.vertexBuffers && !b.vertexBuffers->empty())
            writeBufferBinding(i, 3, (*b.vertexBuffers)[i]);

        // b4: index SSBO
        if (b.indexBuffers && !b.indexBuffers->empty())
            writeBufferBinding(i, 4, (*b.indexBuffers)[i]);

        // b5: modelRef SSBO
        if (b.modelRefBuffers && !b.modelRefBuffers->empty())
            writeBufferBinding(i, 5, (*b.modelRefBuffers)[i]);

        // b6: texture array
        {
            std::array<vk::DescriptorImageInfo, 8> texInfos;
            for (size_t t = 0; t < b.maxTextures; t++) {
                if (b.textureViews && t < b.textureViews->size()) {
                    texInfos[t].imageView = (*b.textureViews)[t];
                } else {
                    texInfos[t].imageView = b.dummyTextureView;
                }
                texInfos[t].sampler = b.textureSampler;
                texInfos[t].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            }
            writeSamplerArrayBinding(i, 6, b.maxTextures, texInfos);
        }

        // b7: envmap
        if (b.envmapInfo.imageView)
            writeSamplerBinding(i, 7, b.envmapInfo);

        // b8: BVH nodes SSBO
        if (b.bvhBuffers && !b.bvhBuffers->empty())
            writeBufferBinding(i, 8, (*b.bvhBuffers)[i]);

        // b9: BVH tri-remap SSBO
        if (b.bvhTriRemapBuffers && !b.bvhTriRemapBuffers->empty())
            writeBufferBinding(i, 9, (*b.bvhTriRemapBuffers)[i]);
    }
    LOG_INFO("RT resource descriptor sets created");
}

// ── Fullscreen descriptor sets ─────────────────────────────────────

void DescriptorManager::createFullscreenSets(uint32_t framesInFlight,
                                              const std::vector<vk::ImageView>& rtImageViews,
                                              vk::Sampler rtSampler) {
    std::vector<vk::DescriptorSetLayout> layouts(framesInFlight, *m_samplerSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = *m_descriptorPool;
    allocInfo.setSetLayouts(layouts);
    m_fullscreenSets = m_device->allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < framesInFlight; i++) {
        vk::DescriptorImageInfo imageInfo;
        imageInfo.imageView = rtImageViews[i];
        imageInfo.sampler = rtSampler;
        imageInfo.imageLayout = vk::ImageLayout::eGeneral;

        vk::WriteDescriptorSet write;
        write.dstSet = *m_fullscreenSets[i];
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.setImageInfo(imageInfo);
        m_device->updateDescriptorSets(write, nullptr);
    }
    LOG_INFO("Fullscreen descriptor sets created");
}

// ── Swapchain-recreation rebinding ─────────────────────────────────

void DescriptorManager::updateRtOutputBinding(size_t frame, vk::ImageView newView) {
    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageView = newView;
    imageInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::WriteDescriptorSet write;
    write.dstSet = *m_rtSets[frame];
    write.dstBinding = 1;
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eStorageImage;
    write.setImageInfo(imageInfo);
    m_device->updateDescriptorSets(write, nullptr);
}

void DescriptorManager::updateFullscreenBinding(size_t frame, vk::ImageView newView,
                                                  vk::Sampler sampler) {
    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageView = newView;
    imageInfo.imageLayout = vk::ImageLayout::eGeneral;
    imageInfo.sampler = sampler;

    vk::WriteDescriptorSet write;
    write.dstSet = *m_fullscreenSets[frame];
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.setImageInfo(imageInfo);
    m_device->updateDescriptorSets(write, nullptr);
}

} // namespace RYRayTracing
