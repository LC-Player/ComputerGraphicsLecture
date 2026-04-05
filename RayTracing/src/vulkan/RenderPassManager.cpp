#include "RenderPassManager.hpp"
#include <stdexcept>

namespace RYRayTracing {

RenderPassManager::RenderPassManager(VkDevice device, const RenderPassConfig& config)
    : device(device), renderPass(VK_NULL_HANDLE), config(config), initialized(false) {
    createRenderPass();
    initialized = true;
    Logger::info("RenderPassManager initialized successfully");
}

RenderPassManager::~RenderPassManager() {
    if (initialized && renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, renderPass, nullptr);
        Logger::info("RenderPassManager destroyed");
    }
}

RenderPassManager::RenderPassManager(RenderPassManager&& other) noexcept
    : device(other.device), renderPass(other.renderPass),
      config(other.config), initialized(other.initialized) {
    other.renderPass = VK_NULL_HANDLE;
    other.initialized = false;
}

RenderPassManager& RenderPassManager::operator=(RenderPassManager&& other) noexcept {
    if (this != &other) {
        if (initialized && renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, renderPass, nullptr);
        }

        device = other.device;
        renderPass = other.renderPass;
        config = other.config;
        initialized = other.initialized;

        other.renderPass = VK_NULL_HANDLE;
        other.initialized = false;
    }
    return *this;
}

void RenderPassManager::createRenderPass() {
    // Color attachment description
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = config.colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = config.clearColors ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = config.initialLayout;
    colorAttachment.finalLayout = config.finalLayout;

    // Color attachment reference
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Subpass description
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    // Subpass dependency for synchronization
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // Create render pass
    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &colorAttachment;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;

    VkResult result = vkCreateRenderPass(device, &createInfo, nullptr, &renderPass);
    if (result != VK_SUCCESS) {
        throw VulkanException(result, "Failed to create render pass", __FUNCTION__, __FILE__, __LINE__);
    }

    Logger::debug("Render pass created successfully");
}

void RenderPassManager::begin(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer,
                               VkRect2D renderArea, const std::vector<VkClearValue>& clearValues) {
    VkRenderPassBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = renderPass;
    beginInfo.framebuffer = framebuffer;
    beginInfo.renderArea = renderArea;

    // Set default clear values if not provided
    std::vector<VkClearValue> defaultClearValues;
    if (clearValues.empty()) {
        defaultClearValues.resize(1);
        defaultClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}}; // Black background
        beginInfo.clearValueCount = static_cast<uint32_t>(defaultClearValues.size());
        beginInfo.pClearValues = defaultClearValues.data();
    } else {
        beginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        beginInfo.pClearValues = clearValues.data();
    }

    vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void RenderPassManager::end(VkCommandBuffer commandBuffer) {
    vkCmdEndRenderPass(commandBuffer);
}

} // namespace RYRayTracing
