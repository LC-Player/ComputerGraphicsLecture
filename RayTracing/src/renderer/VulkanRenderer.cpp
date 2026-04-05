#include "VulkanRenderer.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/SwapChainManager.hpp"
#include "vulkan/RenderPassManager.hpp"
#include "vulkan/PipelineManager.hpp"
#include "vulkan/CommandManager.hpp"

namespace RYRayTracing {

VulkanRenderer::VulkanRenderer(VulkanDevice* device, SwapChainManager* swapChain,
                               RenderPassManager* renderPass, PipelineManager* pipeline,
                               CommandManager* commandManager, const RendererConfig& config)
    : device(device), swapChain(swapChain), renderPass(renderPass),
      pipeline(pipeline), commandManager(commandManager), config(config),
      currentFrame(0), framesInFlight(config.maxFramesInFlight),
      framebufferResized(false), initialized(false) {
}

VulkanRenderer::~VulkanRenderer() {
    if (initialized) {
        cleanup();
    }
}

void VulkanRenderer::initialize() {
    createSyncObjects();
    createCommandBuffers();
    createFramebuffers();
    initialized = true;
    Logger::info("VulkanRenderer initialized successfully");
}

void VulkanRenderer::cleanup() {
    if (!initialized) return;

    VkDevice vkDevice = device->get();
    vkDeviceWaitIdle(vkDevice);

    cleanupFramebuffers();
    cleanupSyncObjects();

    initialized = false;
    Logger::info("VulkanRenderer cleaned up");
}

void VulkanRenderer::recreateSwapChain() {
    VkDevice vkDevice = device->get();
    vkDeviceWaitIdle(vkDevice);

    cleanupFramebuffers();

    // Recreate swap chain (this would be done by SwapChainManager)
    // swapChain->recreate();

    createFramebuffers();
    framebufferResized = false;
    Logger::debug("Swap chain and framebuffers recreated");
}

uint32_t VulkanRenderer::beginFrame() {
    VkDevice vkDevice = device->get();

    // Wait for the fence of the current frame
    vkWaitForFences(vkDevice, 1, &frames[currentFrame].inFlightFence, VK_TRUE, UINT64_MAX);

    // Acquire next image from swap chain
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        vkDevice, swapChain->get(), UINT64_MAX,
        frames[currentFrame].imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return beginFrame();
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw VulkanException(result, "Failed to acquire swap chain image", __FUNCTION__, __FILE__, __LINE__);
    }

    // Check if a previous frame is using this image
    if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(vkDevice, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }

    // Mark the image as now being in use by this frame
    imagesInFlight[imageIndex] = frames[currentFrame].inFlightFence;

    // Reset the fence for the current frame
    vkResetFences(vkDevice, 1, &frames[currentFrame].inFlightFence);

    // Reset command buffer
    commandManager->resetCommandBuffer(frames[currentFrame].commandBuffer);

    return imageIndex;
}

void VulkanRenderer::endFrame(uint32_t imageIndex) {
    VkDevice vkDevice = device->get();
    VkQueue graphicsQueue = device->getGraphicsQueue();
    VkQueue presentQueue = device->getPresentQueue();

    // Submit command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = { frames[currentFrame].imageAvailableSemaphore };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frames[currentFrame].commandBuffer;

    VkSemaphore signalSemaphores[] = { frames[currentFrame].renderFinishedSemaphore };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult result = vkQueueSubmit(graphicsQueue, 1, &submitInfo, frames[currentFrame].inFlightFence);
    if (result != VK_SUCCESS) {
        throw VulkanException(result, "Failed to submit draw command buffer", __FUNCTION__, __FILE__, __LINE__);
    }

    // Present the image
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    auto swapchainget = swapChain->get();
    presentInfo.pSwapchains = &swapchainget;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw VulkanException(result, "Failed to present swap chain image", __FUNCTION__, __FILE__, __LINE__);
    }

    // Advance to next frame
    currentFrame = (currentFrame + 1) % framesInFlight;
}

void VulkanRenderer::beginRenderPass(uint32_t imageIndex) {
    VkCommandBuffer commandBuffer = frames[currentFrame].commandBuffer;

    // Begin command buffer
    commandManager->beginCommandBuffer(commandBuffer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    // Set viewport and scissor
    VkExtent2D extent = swapChain->getExtent();
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    // Begin render pass
    VkRect2D renderArea{};
    renderArea.offset = {0, 0};
    renderArea.extent = extent;

    renderPass->begin(commandBuffer, framebuffers[imageIndex], renderArea, getClearValues());
}

void VulkanRenderer::endRenderPass() {
    VkCommandBuffer commandBuffer = frames[currentFrame].commandBuffer;
    renderPass->end(commandBuffer);
    commandManager->endCommandBuffer(commandBuffer);
}

VkCommandBuffer VulkanRenderer::getCurrentCommandBuffer() const {
    return frames[currentFrame].commandBuffer;
}

void VulkanRenderer::createFramebuffers() {
    const std::vector<VkImageView>& imageViews = swapChain->getImageViews();
    framebuffers.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++) {
        VkImageView attachments[] = { imageViews[i] };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass->get();
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapChain->getExtent().width;
        framebufferInfo.height = swapChain->getExtent().height;
        framebufferInfo.layers = 1;

        VkResult result = vkCreateFramebuffer(device->get(), &framebufferInfo, nullptr, &framebuffers[i]);
        if (result != VK_SUCCESS) {
            throw VulkanException(result, "Failed to create framebuffer", __FUNCTION__, __FILE__, __LINE__);
        }
    }

    Logger::debug("Framebuffers created successfully");
}

void VulkanRenderer::cleanupFramebuffers() {
    for (auto framebuffer : framebuffers) {
        vkDestroyFramebuffer(device->get(), framebuffer, nullptr);
    }
    framebuffers.clear();
}

void VulkanRenderer::createSyncObjects() {
    frames.resize(framesInFlight);
    imagesInFlight.resize(swapChain->getImageCount(), VK_NULL_HANDLE);

    VkDevice vkDevice = device->get();

    for (size_t i = 0; i < framesInFlight; i++) {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VkResult result = vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &frames[i].imageAvailableSemaphore);
        if (result != VK_SUCCESS) {
            throw VulkanException(result, "Failed to create semaphore", __FUNCTION__, __FILE__, __LINE__);
        }

        result = vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &frames[i].renderFinishedSemaphore);
        if (result != VK_SUCCESS) {
            throw VulkanException(result, "Failed to create semaphore", __FUNCTION__, __FILE__, __LINE__);
        }

        result = vkCreateFence(vkDevice, &fenceInfo, nullptr, &frames[i].inFlightFence);
        if (result != VK_SUCCESS) {
            throw VulkanException(result, "Failed to create fence", __FUNCTION__, __FILE__, __LINE__);
        }
    }

    Logger::debug("Synchronization objects created successfully");
}

void VulkanRenderer::cleanupSyncObjects() {
    VkDevice vkDevice = device->get();

    for (size_t i = 0; i < frames.size(); i++) {
        vkDestroySemaphore(vkDevice, frames[i].imageAvailableSemaphore, nullptr);
        vkDestroySemaphore(vkDevice, frames[i].renderFinishedSemaphore, nullptr);
        vkDestroyFence(vkDevice, frames[i].inFlightFence, nullptr);
    }

    frames.clear();
    imagesInFlight.clear();
}

void VulkanRenderer::createCommandBuffers() {
    for (size_t i = 0; i < framesInFlight; i++) {
        frames[i].commandBuffer = commandManager->allocateCommandBuffer();
    }
    Logger::debug("Command buffers created for each frame");
}

std::vector<VkClearValue> VulkanRenderer::getClearValues() const {
    std::vector<VkClearValue> clearValues(1);
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}}; // Black background
    return clearValues;
}

} // namespace RYRayTracing
