// Rendering.cpp
#include "Application.h"

#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"
#include "imgui.h"

#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <limits>

void Application::mainLoop() {
    while (!glfwWindowShouldClose(m_window)) {

        // update logic
        m_transform1.rotation.z += 0.01;
        update();
        glfwPollEvents();

        drawFrame();
    }
    m_device.waitIdle();
}

void Application::update() {
    if (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS) {
    }
}

void Application::updateUniformBuffer(const int currentFrame) {
    CameraData data({m_camera.GetViewProj() * glm::inverse(m_cameraTransform())});
    memcpy(m_uniformBuffersMapped[currentFrame], &data, sizeof(data));
}

void Application::updateInstanceBuffer(const int currentFrame) {
    std::array<QuadInstanceData, 2> instances;
    instances[0].transform = m_transform1();
    instances[1].transform = m_transform2();
    memcpy(m_mappedInstanceData[currentFrame], instances.data(),
        sizeof(QuadInstanceData) * instances.size());
}

void Application::drawFrame() {

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Transform");

    ImGui::SetWindowFontScale(2);

    ImGui::DragFloat3("Translation1", glm::value_ptr(m_transform1.translation), 0.01f);
    ImGui::DragFloat3("Rotation1", glm::value_ptr(m_transform1.rotation), 0.01f);
    ImGui::DragFloat3("Scale1", glm::value_ptr(m_transform1.scale), 0.01f);
    ImGui::Separator();
    ImGui::DragFloat3("Translation2", glm::value_ptr(m_transform2.translation), 0.01f);
    ImGui::DragFloat3("Rotation2", glm::value_ptr(m_transform2.rotation), 0.01f);
    ImGui::DragFloat3("Scale2", glm::value_ptr(m_transform2.scale), 0.01f);
    ImGui::Separator();
    ImGui::Text("Camera");
    ImGui::Separator();
    ImGui::DragFloat3("Translation", glm::value_ptr(m_cameraTransform.translation), 0.01f);
    ImGui::DragFloat3("Rotation", glm::value_ptr(m_cameraTransform.rotation), 0.01f);
    float fov = glm::degrees(m_camera.GetPerspectiveVerticalFOV());
    if (ImGui::DragFloat("FOV", &fov, 0.01f)) {
        m_camera.SetPerspectiveVerticalFOV(glm::radians(fov));
    }

    ImGui::End();

    ImGui::Render();

    auto result = m_device.waitForFences(*m_inFlightFences[m_currentFrame], true, UINT64_MAX);

    if (result != vk::Result::eSuccess) {
        throw std::runtime_error{ "waitForFences in drawFrame was failed" };
    }

    auto [nxtRes, imageIndex] = m_swapChain.acquireNextImage(
        std::numeric_limits<uint64_t>::max(),
        m_imageAvailableSemaphores[m_currentFrame]
    );
    if (nxtRes == vk::Result::eErrorOutOfDateKHR) {
        recreateSwapChain();
        return;
    }

    if (nxtRes != vk::Result::eSuccess && nxtRes != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("Failed to acquire swap chain image!");
    }

    // Only reset the fence if we are submitting work
    m_device.resetFences(*m_inFlightFences[m_currentFrame]);

    updateInstanceBuffer(m_currentFrame);
    updateUniformBuffer(m_currentFrame);
    m_commandBuffers[m_currentFrame].reset();
    recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex, m_instanceBuffers[m_currentFrame]);

    vk::SubmitInfo submitInfo;

    submitInfo.setWaitSemaphores(*m_imageAvailableSemaphores[m_currentFrame]);
    std::array<vk::PipelineStageFlags, 1> waitStages = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
    submitInfo.setWaitDstStageMask(waitStages);

    submitInfo.setCommandBuffers(*m_commandBuffers[m_currentFrame]);

    submitInfo.setSignalSemaphores(*m_renderFinishedSemaphores[imageIndex]);

    m_graphicsQueue.submit(submitInfo, m_inFlightFences[m_currentFrame]);

    vk::PresentInfoKHR presentInfo;
    presentInfo.setWaitSemaphores(*m_renderFinishedSemaphores[imageIndex]);

    presentInfo.setSwapchains(*m_swapChain);
    presentInfo.pImageIndices = &imageIndex;

    auto presentRes = m_graphicsQueue.presentKHR(presentInfo);
    if (presentRes == vk::Result::eErrorOutOfDateKHR || presentRes == vk::Result::eSuboptimalKHR || m_framebufferResized) {
        recreateSwapChain();
        return;
    }
    else if (presentRes != vk::Result::eSuccess) {
        throw std::runtime_error("presentKHR failed with unexpected error");
    }

    if (m_framebufferResized) {
        recreateSwapChain();
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

vk::raii::CommandBuffer Application::beginSingleTimeCommands() const {
    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    auto commandBuffers = m_device.allocateCommandBuffers(allocInfo);
    vk::raii::CommandBuffer commandBuffer = std::move(commandBuffers.at(0));

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    commandBuffer.begin(beginInfo);

    return commandBuffer;
}
void Application::endSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const {
    commandBuffer.end();

    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*commandBuffer);

    m_graphicsQueue.submit(submitInfo);
    m_graphicsQueue.waitIdle();
}

void Application::recordCommandBuffer(const vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex, const vk::Buffer& instanceBuffer) const {
    constexpr vk::CommandBufferBeginInfo beginInfo;
    commandBuffer.begin(beginInfo);

    vk::RenderPassBeginInfo renderPassInfo;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapChainFramebuffers[imageIndex];

    renderPassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
    renderPassInfo.renderArea.extent = m_swapChainExtent;
    vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    renderPassInfo.setClearValues(clearColor);

    commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline);

    const vk::Viewport viewport(
        0.0f, 0.0f, // x, y
        static_cast<float>(m_swapChainExtent.width),    // width
        static_cast<float>(m_swapChainExtent.height),   // height
        0.0f, 1.0f  // minDepth maxDepth
    );
    commandBuffer.setViewport(0, viewport);

    const vk::Rect2D scissor(
        vk::Offset2D{ 0, 0 }, // offset
        m_swapChainExtent   // extent
    );
    commandBuffer.setScissor(0, scissor);

    const std::array<vk::Buffer, 2> vertexBuffers{ m_vertexBuffer, instanceBuffer };
    constexpr std::array<vk::DeviceSize, 2> offsets{ 0, 0 };
    commandBuffer.bindVertexBuffers(0, vertexBuffers, offsets);
    commandBuffer.bindIndexBuffer(m_indexBuffer, 0, vk::IndexType::eUint16);
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        m_pipelineLayout,
        0,
        *m_descriptorSets[m_currentFrame],
        nullptr
    );
    commandBuffer.drawIndexed(
        static_cast<uint32_t>(m_indices.size()),
        m_quadInstances.size(), 0, 0, 0);

    ImGui_ImplVulkan_RenderDrawData(
        ImGui::GetDrawData(),
        *commandBuffer
    );

    commandBuffer.endRenderPass();
    commandBuffer.end();
}

void Application::createCommandPool() {
    const auto [graphicsFamily, presentFamily] = findQueueFamilies(m_physicalDevice);

    vk::CommandPoolCreateInfo poolInfo;
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = graphicsFamily.value();

    m_commandPool = m_device.createCommandPool(poolInfo);
}

void Application::createCommandBuffers() {
    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

    m_commandBuffers = m_device.allocateCommandBuffers(allocInfo);
}

void Application::createSyncObjects() {
    constexpr vk::SemaphoreCreateInfo semaphoreInfo;
    constexpr vk::FenceCreateInfo fenceInfo(
        vk::FenceCreateFlagBits::eSignaled  // flags
    );
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_imageAvailableSemaphores.emplace_back(m_device, semaphoreInfo);
        m_inFlightFences.emplace_back(m_device, fenceInfo);
    }
    for (size_t i = 0; i < m_swapChainImages.size(); ++i) {
        m_renderFinishedSemaphores.emplace_back(m_device, semaphoreInfo);
    }
}

void Application::cleanup() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    for (auto& it : m_instanceBufferMemory) {
        it.unmapMemory();
    }
    for (const auto& it : m_uniformBuffersMemory) {
        it.unmapMemory();
    }
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
}
