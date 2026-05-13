#include "RenderPassManager.hpp"
#include <stdexcept>

namespace RYRayTracing {

RenderPassManager::RenderPassManager(vk::raii::Device& device, const RenderPassConfig& config)
    : device(device), config(config) {
    createRenderPass();
    LOG_INFO("RenderPassManager initialized successfully");
}

void RenderPassManager::createRenderPass() {
    std::vector<vk::AttachmentDescription> attachments;

    vk::AttachmentDescription colorAttachment;
    colorAttachment.setFormat(config.colorFormat);
    colorAttachment.setSamples(vk::SampleCountFlagBits::e1);
    colorAttachment.setLoadOp(config.clearColors ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad);
    colorAttachment.setStoreOp(vk::AttachmentStoreOp::eStore);
    colorAttachment.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
    colorAttachment.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
    colorAttachment.setInitialLayout(config.initialLayout);
    colorAttachment.setFinalLayout(config.finalLayout);
    attachments.push_back(colorAttachment);

    vk::AttachmentReference colorAttachmentRef;
    colorAttachmentRef.setAttachment(0);
    colorAttachmentRef.setLayout(vk::ImageLayout::eColorAttachmentOptimal);

    vk::AttachmentReference depthAttachmentRef;
    depthAttachmentRef.setAttachment(1);
    depthAttachmentRef.setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

    bool hasDepth = config.depthFormat != vk::Format::eUndefined;
    if (hasDepth) {
        vk::AttachmentDescription depthAttachment;
        depthAttachment.setFormat(config.depthFormat);
        depthAttachment.setSamples(vk::SampleCountFlagBits::e1);
        depthAttachment.setLoadOp(config.clearDepth ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad);
        depthAttachment.setStoreOp(vk::AttachmentStoreOp::eStore);
        depthAttachment.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
        depthAttachment.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
        depthAttachment.setInitialLayout(vk::ImageLayout::eUndefined);
        depthAttachment.setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
        attachments.push_back(depthAttachment);
    }

    vk::SubpassDescription subpass;
    subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
    subpass.setColorAttachments(colorAttachmentRef);
    if (hasDepth) {
        subpass.setPDepthStencilAttachment(&depthAttachmentRef);
    }

    vk::SubpassDependency dependency;
    dependency.setSrcSubpass(VK_SUBPASS_EXTERNAL);
    dependency.setDstSubpass(0);
    dependency.setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    dependency.setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    dependency.setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);
    dependency.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);

    vk::RenderPassCreateInfo createInfo;
    createInfo.setAttachments(attachments);
    createInfo.setSubpasses(subpass);
    createInfo.setDependencies(dependency);

    try {
        renderPass = device.createRenderPass(createInfo);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to create render pass: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    LOG_DEBUG("Render pass created successfully");
}

void RenderPassManager::begin(vk::raii::CommandBuffer& commandBuffer, vk::Framebuffer framebuffer,
                               vk::Rect2D renderArea, const std::vector<vk::ClearValue>& clearValues) {
    std::vector<vk::ClearValue> actualClearValues = clearValues;
    if (actualClearValues.empty()) {
        bool hasDepth = config.depthFormat != vk::Format::eUndefined;
        actualClearValues.resize(hasDepth ? 2 : 1);
        actualClearValues[0].color = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f};
        if (hasDepth) {
            actualClearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
        }
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
