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

void VulkanRenderer::initialize() {
    createSyncObjects();
    createCommandBuffers();
    createFramebuffers();
    initialized = true;
    LOG_INFO("VulkanRenderer initialized successfully");
}

void VulkanRenderer::cleanup() {
    if (!initialized) return;

    device->waitIdle();

    cleanupFramebuffers();
    cleanupSyncObjects();

    initialized = false;
    LOG_INFO("VulkanRenderer cleaned up");
}

void VulkanRenderer::recreateSwapChain() {
    device->waitIdle();

    cleanupFramebuffers();
    createFramebuffers();
    framebufferResized = false;
    LOG_DEBUG("Swap chain and framebuffers recreated");
}

uint32_t VulkanRenderer::beginFrame() {
    // Wait for the fence of the current frame
    auto result = device->get().waitForFences(*frames[currentFrame].inFlightFence, true, UINT64_MAX);

    if (result != vk::Result::eSuccess) {
        throw std::runtime_error{ "waitForFences in drawFrame was failed" };
    }

    // Acquire next image from swap chain
    uint32_t imageIndex = swapChain->acquireNextImage(*frames[currentFrame].imageAvailableSemaphore);

    if (imageIndex == UINT32_MAX) {
        recreateSwapChain();
        return beginFrame();
    }

    // Check if a previous frame is using this image
    if (imagesInFlight[imageIndex]) {
        auto result = device->get().waitForFences(imagesInFlight[imageIndex], true, UINT64_MAX);
        if (result != vk::Result::eSuccess) {
            throw std::runtime_error{ "waitForFences in drawFrame was failed" };
        }
    }

    // Mark the image as now being in use by this frame
    imagesInFlight[imageIndex] = *frames[currentFrame].inFlightFence;

    // Reset the fence for the current frame
    device->get().resetFences(*frames[currentFrame].inFlightFence);

    // Reset command buffer
    commandManager->resetCommandBuffer(frames[currentFrame].commandBuffer);

    return imageIndex;
}

void VulkanRenderer::endFrame(uint32_t imageIndex) {
    // Submit command buffer
    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*frames[currentFrame].commandBuffer);
    submitInfo.setWaitSemaphores(*frames[currentFrame].imageAvailableSemaphore);
    std::array<vk::PipelineStageFlags, 1> waitStages = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
    submitInfo.setWaitDstStageMask(waitStages);
    submitInfo.setSignalSemaphores(*frames[currentFrame].renderFinishedSemaphore);

    try {
        device->getGraphicsQueue().submit(submitInfo, *frames[currentFrame].inFlightFence);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(),
                            std::string("Failed to submit draw command buffer: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Present the image
    swapChain->presentImage(imageIndex, *frames[currentFrame].renderFinishedSemaphore);

    // Advance to next frame
    currentFrame = (currentFrame + 1) % framesInFlight;
}

void VulkanRenderer::beginRenderPass(uint32_t imageIndex) {
    auto& commandBuffer = frames[currentFrame].commandBuffer;

    // Begin command buffer
    commandManager->beginCommandBuffer(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

    // Set viewport and scissor
    auto extent = swapChain->getExtent();
    vk::Viewport viewport{
        0.0f,
        0.0f,
        static_cast<float>(extent.width),
        static_cast<float>(extent.height),
        0.0f,
        1.0f
    };
    commandBuffer.setViewport(0, viewport);

    vk::Rect2D scissor{
        {0, 0},
        extent
    };
    commandBuffer.setScissor(0, scissor);

    // Begin render pass
    vk::Rect2D renderArea{
        {0, 0},
        extent
    };

    renderPass->begin(commandBuffer, *framebuffers[imageIndex], renderArea, getClearValues());
}

void VulkanRenderer::endRenderPass() {
    auto& commandBuffer = frames[currentFrame].commandBuffer;
    renderPass->end(commandBuffer);
    commandManager->endCommandBuffer(commandBuffer);
}

vk::raii::CommandBuffer& VulkanRenderer::getCurrentCommandBuffer() {
    return frames[currentFrame].commandBuffer;
}

void VulkanRenderer::createFramebuffers() {
    auto& imageViews = swapChain->getImageViews();
    framebuffers.clear();

    for (size_t i = 0; i < imageViews.size(); i++) {
        vk::FramebufferCreateInfo framebufferInfo{
            {},
            *renderPass->get(),
            *imageViews[i],
            swapChain->getExtent().width,
            swapChain->getExtent().height,
            1
        };

        try {
            framebuffers.emplace_back(device->get().createFramebuffer(framebufferInfo));
        } catch (const vk::SystemError& e) {
            throw VulkanException(e.code(),
                                std::string("Failed to create framebuffer: ") + e.what(),
                                __FUNCTION__, __FILE__, __LINE__);
        }
    }

    LOG_DEBUG("Framebuffers created successfully");
}

void VulkanRenderer::cleanupFramebuffers() {
    framebuffers.clear();
}

void VulkanRenderer::createSyncObjects() {
    frames.clear();
    imagesInFlight.resize(swapChain->getImageCount(), nullptr);

    for (size_t i = 0; i < framesInFlight; i++) {
        FrameData frame;

        try {
            frame.imageAvailableSemaphore = device->get().createSemaphore({});
            frame.renderFinishedSemaphore = device->get().createSemaphore({});
            frame.inFlightFence = device->get().createFence({vk::FenceCreateFlagBits::eSignaled});
        } catch (const vk::SystemError& e) {
            throw VulkanException(e.code(),
                                std::string("Failed to create sync objects: ") + e.what(),
                                __FUNCTION__, __FILE__, __LINE__);
        }

        frames.push_back(std::move(frame));
    }

    LOG_DEBUG("Synchronization objects created successfully");
}

void VulkanRenderer::cleanupSyncObjects() {
    frames.clear();
    imagesInFlight.clear();
}

void VulkanRenderer::createCommandBuffers() {
    for (size_t i = 0; i < framesInFlight; i++) {
        frames[i].commandBuffer = commandManager->allocateCommandBuffer();
    }
    LOG_DEBUG("Command buffers created for each frame");
}

std::vector<vk::ClearValue> VulkanRenderer::getClearValues() const {
    std::vector<vk::ClearValue> clearValues(1);
    clearValues[0].color = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f}; // Black background
    return clearValues;
}

} // namespace RYRayTracing
