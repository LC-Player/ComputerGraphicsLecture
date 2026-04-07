#include "RenderPassManager.hpp"
#include <stdexcept>

namespace RYRayTracing {

RenderPassManager::RenderPassManager(vk::raii::Device& device, const RenderPassConfig& config)
    : device(&device), config(config) {
    createRenderPass();
    LOG_INFO("RenderPassManager initialized successfully");
}

void RenderPassManager::createRenderPass() {
    // Color attachment description
    vk::AttachmentDescription colorAttachment;
    colorAttachment.setFormat(config.colorFormat);
    colorAttachment.setSamples(vk::SampleCountFlagBits::e1);
    colorAttachment.setLoadOp(config.clearColors ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad);
    colorAttachment.setStoreOp(vk::AttachmentStoreOp::eStore);
    colorAttachment.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
    colorAttachment.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
    colorAttachment.setInitialLayout(config.initialLayout);
    colorAttachment.setFinalLayout(config.finalLayout);

    // Color attachment reference
    vk::AttachmentReference colorAttachmentRef;
    colorAttachmentRef.setAttachment(0);
    colorAttachmentRef.setLayout(vk::ImageLayout::eColorAttachmentOptimal);

    // Subpass description
    vk::SubpassDescription subpass;
    subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
    subpass.setColorAttachments(colorAttachmentRef);

    // Subpass dependency for synchronization
    vk::SubpassDependency dependency;
    dependency.setSrcSubpass(VK_SUBPASS_EXTERNAL);
    dependency.setDstSubpass(0);
    dependency.setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    dependency.setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    dependency.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);

    // Create render pass
    vk::RenderPassCreateInfo createInfo;
    createInfo.setAttachments(colorAttachment);
    createInfo.setSubpasses(subpass);
    createInfo.setDependencies(dependency);

    try {
        renderPass = device->createRenderPass(createInfo);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to create render pass: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    LOG_DEBUG("Render pass created successfully");
}

void RenderPassManager::begin(vk::raii::CommandBuffer& commandBuffer, vk::Framebuffer framebuffer,
                               vk::Rect2D renderArea, const std::vector<vk::ClearValue>& clearValues) {
    // Set default clear values if not provided
    std::vector<vk::ClearValue> actualClearValues = clearValues;
    if (actualClearValues.empty()) {
        actualClearValues.resize(1);
        actualClearValues[0].color = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}; // Black background
    }

    vk::RenderPassBeginInfo beginInfo{
        *renderPass,
        framebuffer,
        renderArea,
        actualClearValues
    };

    commandBuffer.beginRenderPass(beginInfo, vk::SubpassContents::eInline);
}

void RenderPassManager::end(vk::raii::CommandBuffer& commandBuffer) {
    commandBuffer.endRenderPass();
}

} // namespace RYRayTracing
