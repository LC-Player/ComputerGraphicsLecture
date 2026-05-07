#include "Application.hpp"
#include "core/Exception.hpp"
#include "core/Logger.hpp"
#include "window/WindowManager.hpp"
#include "vulkan/Validation.hpp"
#include "vulkan/VulkanInstance.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "vulkan/SwapChainManager.hpp"
#include "vulkan/Buffer.hpp"
#include "vulkan/ShaderModule.hpp"
#include "vulkan/RenderPassManager.hpp"
#include "vulkan/PipelineManager.hpp"
#include "vulkan/CommandManager.hpp"

#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"
#include "imgui.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <array>
#include <thread>
#include <chrono>
#include <cstring>

namespace RYRayTracing {

Application::Application()
    : currentFrame(0)
    , framesInFlight(0)
    , windowWidth(1440)
    , windowHeight(1080)
    , framebufferResized(false) {

    Logger::init("raytracing.log");
    LOG_INFO("=== Vulkan Quad Rendering Application ===");
}

Application::~Application() {
    cleanup();
    LOG_INFO("=== Application Shutting Down ===");

    glfwTerminate();

    Logger::shutdown();
}

void Application::run() {
    try {
        LOG_INFO("Starting Vulkan quad application");

        initVulkan();
        initImGui();
        initComponents();
        mainLoop();

        LOG_INFO("Application completed successfully");

    } catch (const VulkanException& e) {
        LOG_ERROR("Vulkan error: " + std::string(e.what()));
        LOG_ERROR("Error code: " + VulkanException::getErrorString(e.getErrorCode()));
        LOG_ERROR("Location: " + e.getLocation());
    } catch (const std::exception& e) {
        LOG_ERROR("Standard exception: " + std::string(e.what()));
    }
}

void Application::initVulkan() {
    LOG_INFO("Initializing Vulkan...");

    WindowConfig windowConfig;
    windowConfig.width = windowWidth;
    windowConfig.height = windowHeight;
    windowConfig.title = "Vulkan Quad Rendering";
    windowConfig.resizable = true;

    windowManager = std::make_unique<WindowManager>(windowConfig);
    windowManager->init();

    WindowCallbacks callbacks;
    callbacks.onResize = [this](int width, int height) {
        this->framebufferResized = true;
        this->windowWidth = width;
        this->windowHeight = height;
    };
    windowManager->setCallbacks(callbacks);

    createInstance();
    createDevice();
    createSwapChain();
    createRenderPass();
    createDescriptorSetLayout();
    createGraphicsPipeline();
    createFramebuffers();
    createCommandPool();
    createVertexBuffer();
    createIndexBuffer();
    createInstanceBuffers();
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();
    createSyncObjects();

    LOG_INFO("Full rendering pipeline initialized - quads should be displayed");
}

void Application::initImGui() {
    vk::DescriptorPoolSize pool_sizes[] = {
        { vk::DescriptorType::eCombinedImageSampler, 1000 },
    };

    vk::DescriptorPoolCreateInfo pool_info{};
    pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = pool_sizes;

    imguiPool = vulkanDevice->get().createDescriptorPool(pool_info);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplGlfw_InitForVulkan(windowManager->getHandle(), true);

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance = *vulkanInstance->get();
    init_info.PhysicalDevice = *vulkanDevice->getPhysical();
    init_info.Device = *vulkanDevice->get();
    init_info.Queue = vulkanDevice->getGraphicsQueue();
    init_info.DescriptorPool = *imguiPool;
    init_info.MinImageCount = swapChainManager->getImageCount();
    init_info.ImageCount = swapChainManager->getImageCount();

    ImGui_ImplVulkan_Init(&init_info, *renderPassManager->get());

    auto& cmd = commandBuffers[0];
    cmd.reset();

    vk::CommandBufferBeginInfo beginInfo{};
    cmd.begin(beginInfo);

    ImGui_ImplVulkan_CreateFontsTexture(*cmd);

    cmd.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.setCommandBuffers(*cmd);

    vulkanDevice->getGraphicsQueue().submit(submitInfo);
    vulkanDevice->getGraphicsQueue().waitIdle();

    ImGui_ImplVulkan_DestroyFontUploadObjects();
}

void Application::initComponents() {
    camera.SetAspectRatio(static_cast<float>(windowWidth) / windowHeight);
    camera.SetPerspective(glm::radians(45.0f), 1, 100);
    cameraTransform.translation.z = 5;
    transform1.translation.x = -1;
    transform2.translation.x = 1;
}

void Application::cleanup() {
    LOG_INFO("Cleaning up resources...");

    if (vulkanDevice) {
        vulkanDevice->waitIdle();
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    cleanupSwapChain();
    cleanupSyncObjects();

    for (auto& ptr : mappedInstanceData) {
    }
    mappedInstanceData.clear();

    for (auto& ptr : mappedUniformData) {
    }
    mappedUniformData.clear();

    commandBuffers.clear();
    descriptorSets.clear();
    descriptorPool = nullptr;
    descriptorSetLayout = nullptr;
    imguiPool = nullptr;
    pipelineLayout = nullptr;
    fragmentShader.reset();
    vertexShader.reset();
    instanceBuffers.clear();
    uniformBuffers.clear();
    vertexBuffer.reset();
    indexBuffer.reset();
    commandManager.reset();
    pipelineManager.reset();
    renderPassManager.reset();
    swapChainManager.reset();
    vulkanDevice.reset();
    windowManager.reset();
    vulkanInstance.reset();

    LOG_INFO("Cleanup completed");
}

void Application::createInstance() {
    LOG_INFO("Creating Vulkan instance...");

    InstanceConfig config;
    config.applicationName = "Vulkan Quad Rendering";
    config.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    config.engineName = "No Engine";
    config.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    config.apiVersion = VK_API_VERSION_1_3;
    config.enableValidation = Validation::shouldEnableValidation();

    vulkanInstance = std::make_unique<VulkanInstance>(config);
    LOG_INFO("Vulkan instance created");
}

void Application::createDevice() {
    LOG_INFO("Creating Vulkan device...");

    windowManager->createSurface(vulkanInstance->get());
    LOG_INFO("Vulkan surface created");

    DeviceConfig deviceConfig;
    deviceConfig.requiredFeatures.samplerAnisotropy = true;

    vulkanDevice = std::make_unique<VulkanDevice>(vulkanInstance->get(), windowManager->getSurface(), deviceConfig);
    LOG_INFO("Vulkan device created");
}

void Application::createSwapChain() {
    LOG_INFO("Creating swap chain...");

    swapChainManager = std::make_unique<SwapChainManager>(
        *vulkanDevice, windowManager->getSurface(), windowWidth, windowHeight);

    LOG_INFO("Swap chain created");
}

void Application::createRenderPass() {
    LOG_INFO("Creating render pass...");

    RenderPassConfig config;
    config.colorFormat = swapChainManager->getImageFormat();
    config.clearColors = true;

    renderPassManager = std::make_unique<RenderPassManager>(vulkanDevice->get(), config);
    LOG_INFO("Render pass created");
}

void Application::createDescriptorSetLayout() {
    LOG_INFO("Creating descriptor set layout...");

    vk::DescriptorSetLayoutBinding uboLayoutBinding;
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.setBindings(uboLayoutBinding);
    descriptorSetLayout = vulkanDevice->get().createDescriptorSetLayout(layoutInfo);

    LOG_INFO("Descriptor set layout created");
}

void Application::createGraphicsPipeline() {
    LOG_INFO("Creating graphics pipeline...");

    try {
        std::string shaderPath = "shaders/shader.spv";

        LOG_INFO("Loading shader: " + shaderPath);
        vertexShader = std::make_unique<ShaderModule>(
            ShaderModule::createVertexShader(vulkanDevice.get(), shaderPath));

        fragmentShader = std::make_unique<ShaderModule>(
            ShaderModule::createFragmentShader(vulkanDevice.get(), shaderPath));

        LOG_INFO("Shaders loaded successfully");

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setSetLayouts(*descriptorSetLayout);

        pipelineLayout = vulkanDevice->get().createPipelineLayout(pipelineLayoutInfo);
        LOG_INFO("Pipeline layout created");

        pipelineManager = std::make_unique<PipelineManager>(vulkanDevice->get());

        PipelineConfig pipelineConfig;
        pipelineConfig.vertexShader = *vertexShader->get();
        pipelineConfig.fragmentShader = *fragmentShader->get();
        pipelineConfig.pipelineLayout = *pipelineLayout;
        pipelineConfig.renderPass = *renderPassManager->get();
        pipelineConfig.vertexEntryPoint = "vertMain";
        pipelineConfig.fragmentEntryPoint = "fragMain";
        pipelineConfig.blendEnable = true;

        auto bindingDescriptions = {getVertexBindingDescription(), getInstanceBindingDescription()};
        pipelineConfig.vertexBindingDescriptions.assign(bindingDescriptions.begin(), bindingDescriptions.end());

        auto attributeDescriptions = getVertexAttributeDescriptions();
        pipelineConfig.vertexAttributeDescriptions.assign(
            attributeDescriptions.begin(), attributeDescriptions.end());

        pipelineManager->createPipeline("main", pipelineConfig);

        LOG_INFO("Graphics pipeline created successfully");

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create graphics pipeline: " + std::string(e.what()));
        throw;
    }
}

void Application::createFramebuffers() {
    LOG_INFO("Creating framebuffers...");

    swapChainFramebuffers.clear();

    auto& imageViews = swapChainManager->getImageViews();
    for (size_t i = 0; i < imageViews.size(); i++) {
        vk::FramebufferCreateInfo framebufferInfo;
        framebufferInfo.setRenderPass(*renderPassManager->get());
        framebufferInfo.setAttachments(*imageViews[i]);
        framebufferInfo.setWidth(swapChainManager->getExtent().width);
        framebufferInfo.setHeight(swapChainManager->getExtent().height);
        framebufferInfo.setLayers(1);

        try {
            swapChainFramebuffers.emplace_back(vulkanDevice->get().createFramebuffer(framebufferInfo));
        } catch (const vk::SystemError& e) {
            throw VulkanException(e.code(), std::string("Failed to create framebuffer: ") + e.what(),
                                __FUNCTION__, __FILE__, __LINE__);
        }
    }

    LOG_INFO("Framebuffers created: " + std::to_string(swapChainFramebuffers.size()));
}

void Application::createCommandPool() {
    LOG_INFO("Creating command pool...");

    CommandPoolConfig config;
    config.queueFamilyIndex = vulkanDevice->getGraphicsQueueFamily();

    commandManager = std::make_unique<CommandManager>(vulkanDevice->get(), config);
    LOG_INFO("Command pool created");
}

void Application::createVertexBuffer() {
    LOG_INFO("Creating vertex buffer...");

    std::array<Vertex, 4> vertices = {
        Vertex{{-0.5f, -0.5f, 0.0f}, {1, 0, 0, 1}},
        Vertex{{0.5f, -0.5f, 0.0f}, {0, 1, 0, 1}},
        Vertex{{0.5f, 0.5f, 0.0f}, {0, 0, 1, 1}},
        Vertex{{-0.5f, 0.5f, 0.0f}, {1, 1, 1, 1}}
    };

    vertexBuffer = std::make_unique<Buffer>(
        Buffer::createVertexBuffer(vulkanDevice.get(), vertices.data(), sizeof(Vertex) * vertices.size()));

    LOG_INFO("Vertex buffer created");
}

void Application::createIndexBuffer() {
    LOG_INFO("Creating index buffer...");

    std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};

    indexBuffer = std::make_unique<Buffer>(
        Buffer::createIndexBuffer(vulkanDevice.get(), indices.data(), sizeof(uint16_t) * indices.size()));

    LOG_INFO("Index buffer created");
}

void Application::createInstanceBuffers() {
    LOG_INFO("Creating instance buffers...");

    vk::DeviceSize instanceBufferSize = sizeof(QuadInstanceData) * quadInstances.size();

    framesInFlight = swapChainManager->getImageCount();

    instanceBuffers.clear();
    mappedInstanceData.clear();
    instanceBuffers.reserve(framesInFlight);
    mappedInstanceData.reserve(framesInFlight);

    for (size_t i = 0; i < framesInFlight; i++) {
        instanceBuffers.emplace_back(std::make_unique<Buffer>(
            Buffer::createBuffer(vulkanDevice.get(), instanceBufferSize,
                vk::BufferUsageFlagBits::eVertexBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));

        mappedInstanceData.push_back(instanceBuffers[i]->map(0, instanceBufferSize));
    }

    LOG_INFO("Instance buffers created");
}

void Application::createUniformBuffers() {
    LOG_INFO("Creating uniform buffers...");

    constexpr vk::DeviceSize bufferSize = sizeof(CameraData);

    uniformBuffers.clear();
    mappedUniformData.clear();
    uniformBuffers.reserve(framesInFlight);
    mappedUniformData.reserve(framesInFlight);

    for (size_t i = 0; i < framesInFlight; i++) {
        uniformBuffers.emplace_back(std::make_unique<Buffer>(
            Buffer::createBuffer(vulkanDevice.get(), bufferSize,
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));

        mappedUniformData.push_back(uniformBuffers[i]->map(0, bufferSize));
    }

    LOG_INFO("Uniform buffers created");
}

void Application::createDescriptorPool() {
    LOG_INFO("Creating descriptor pool...");

    vk::DescriptorPoolSize poolSize(vk::DescriptorType::eUniformBuffer, static_cast<uint32_t>(framesInFlight));
    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = static_cast<uint32_t>(framesInFlight);
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    descriptorPool = vulkanDevice->get().createDescriptorPool(poolInfo);

    LOG_INFO("Descriptor pool created");
}

void Application::createDescriptorSets() {
    LOG_INFO("Creating descriptor sets...");

    std::vector<vk::DescriptorSetLayout> layouts(framesInFlight, *descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.setSetLayouts(layouts);
    descriptorSets = vulkanDevice->get().allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < framesInFlight; ++i) {
        vk::DescriptorBufferInfo bufferInfo;
        bufferInfo.buffer = *uniformBuffers[i]->get();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(CameraData);

        vk::WriteDescriptorSet descriptorWrite;
        descriptorWrite.dstSet = descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        descriptorWrite.setBufferInfo(bufferInfo);

        vulkanDevice->get().updateDescriptorSets(descriptorWrite, nullptr);
    }

    LOG_INFO("Descriptor sets created");
}

void Application::createCommandBuffers() {
    LOG_INFO("Creating command buffers...");

    commandBuffers = commandManager->allocateCommandBuffers(swapChainFramebuffers.size());

    LOG_INFO("Command buffers created: " + std::to_string(commandBuffers.size()));
}

void Application::createSyncObjects() {
    LOG_INFO("Creating synchronization objects...");

    framesInFlight = swapChainManager->getImageCount();

    imageAvailableSemaphores.clear();
    renderFinishedSemaphores.clear();
    inFlightFences.clear();
    imagesInFlight.resize(framesInFlight, nullptr);

    for (size_t i = 0; i < framesInFlight; i++) {
        try {
            imageAvailableSemaphores.emplace_back(vulkanDevice->get().createSemaphore({}));
            renderFinishedSemaphores.emplace_back(vulkanDevice->get().createSemaphore({}));
            inFlightFences.emplace_back(vulkanDevice->get().createFence({vk::FenceCreateFlagBits::eSignaled}));
        } catch (const vk::SystemError& e) {
            throw VulkanException(e.code(), std::string("Failed to create sync objects: ") + e.what(),
                                __FUNCTION__, __FILE__, __LINE__);
        }
    }

    LOG_INFO("Sync objects created for " + std::to_string(framesInFlight) + " frames");
}

void Application::updateUniformBuffer(size_t currentFrame) {
    CameraData data{camera.GetViewProj() * glm::inverse(cameraTransform())};
    memcpy(mappedUniformData[currentFrame], &data, sizeof(data));
}

void Application::updateInstanceBuffer(size_t currentFrame) {
    std::array<QuadInstanceData, 2> instances;
    instances[0].transform = transform1();
    instances[1].transform = transform2();
    memcpy(mappedInstanceData[currentFrame], instances.data(),
        sizeof(QuadInstanceData) * instances.size());
}

void Application::cleanupSwapChain() {
    // Descriptor sets must be freed before their parent pool is destroyed
    descriptorSets.clear();
    descriptorPool = nullptr;
    commandBuffers.clear();
    pipelineLayout = nullptr;
    vertexShader.reset();
    fragmentShader.reset();
    pipelineManager.reset();
    swapChainFramebuffers.clear();
    renderPassManager.reset();
    instanceBuffers.clear();
    mappedInstanceData.clear();
    uniformBuffers.clear();
    mappedUniformData.clear();
}

void Application::cleanupSyncObjects() {
    imageAvailableSemaphores.clear();
    renderFinishedSemaphores.clear();
    inFlightFences.clear();
    imagesInFlight.clear();
}

void Application::recreateSwapChain() {
    vulkanDevice->waitIdle();

    cleanupSwapChain();

    swapChainManager->recreate(windowWidth, windowHeight);
    createRenderPass();
    createGraphicsPipeline();
    createFramebuffers();
    createInstanceBuffers();
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();

    camera.SetAspectRatio(static_cast<float>(windowWidth) / windowHeight);

    framebufferResized = false;
}

void Application::drawFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Transform");

    ImGui::SetWindowFontScale(2);

    ImGui::DragFloat3("Translation1", glm::value_ptr(transform1.translation), 0.01f);
    ImGui::DragFloat3("Rotation1", glm::value_ptr(transform1.rotation), 0.01f);
    ImGui::DragFloat3("Scale1", glm::value_ptr(transform1.scale), 0.01f);
    ImGui::Separator();
    ImGui::DragFloat3("Translation2", glm::value_ptr(transform2.translation), 0.01f);
    ImGui::DragFloat3("Rotation2", glm::value_ptr(transform2.rotation), 0.01f);
    ImGui::DragFloat3("Scale2", glm::value_ptr(transform2.scale), 0.01f);
    ImGui::Separator();
    ImGui::Text("Camera");
    ImGui::Separator();
    ImGui::DragFloat3("Translation", glm::value_ptr(cameraTransform.translation), 0.01f);
    ImGui::DragFloat3("Rotation", glm::value_ptr(cameraTransform.rotation), 0.01f);
    float fov = glm::degrees(camera.GetPerspectiveVerticalFOV());
    if (ImGui::DragFloat("FOV", &fov, 0.01f)) {
        camera.SetPerspectiveVerticalFOV(glm::radians(fov));
    }

    ImGui::End();

    ImGui::Render();

    try {
        (void)vulkanDevice->get().waitForFences(*inFlightFences[currentFrame], true, UINT64_MAX);
    } catch (const vk::SystemError& e) {
        throw VulkanException(static_cast<vk::Result>(e.code().value()),
                            std::string("Failed to wait for fence: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    uint32_t imageIndex = swapChainManager->acquireNextImage(*imageAvailableSemaphores[currentFrame]);

    if (imageIndex == UINT32_MAX) {
        recreateSwapChain();
        return;
    }

    if (imagesInFlight[imageIndex]) {
        try {
            (void)vulkanDevice->get().waitForFences(imagesInFlight[imageIndex], true, UINT64_MAX);
        } catch (const vk::SystemError& e) {
            throw VulkanException(static_cast<vk::Result>(e.code().value()),
                                std::string("Failed to wait for image fence: ") + e.what(),
                                __FUNCTION__, __FILE__, __LINE__);
        }
    }
    imagesInFlight[imageIndex] = *inFlightFences[currentFrame];

    updateInstanceBuffer(currentFrame);
    updateUniformBuffer(currentFrame);

    commandBuffers[currentFrame].reset();

    vk::CommandBufferBeginInfo beginInfo;
    commandBuffers[currentFrame].begin(beginInfo);

    vk::Viewport viewport;
    viewport.setX(0.0f);
    viewport.setY(0.0f);
    viewport.setWidth(static_cast<float>(swapChainManager->getExtent().width));
    viewport.setHeight(static_cast<float>(swapChainManager->getExtent().height));
    viewport.setMinDepth(0.0f);
    viewport.setMaxDepth(1.0f);
    commandBuffers[currentFrame].setViewport(0, viewport);

    vk::Rect2D scissor;
    scissor.setOffset({0, 0});
    scissor.setExtent(swapChainManager->getExtent());
    commandBuffers[currentFrame].setScissor(0, scissor);

    vk::ClearValue clearColor;
    clearColor.color = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f};

    vk::RenderPassBeginInfo renderPassInfo;
    renderPassInfo.setRenderPass(*renderPassManager->get());
    renderPassInfo.setFramebuffer(*swapChainFramebuffers[imageIndex]);
    renderPassInfo.setRenderArea(vk::Rect2D{{0, 0}, swapChainManager->getExtent()});
    renderPassInfo.setClearValues(clearColor);

    commandBuffers[currentFrame].beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    commandBuffers[currentFrame].bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineManager->getPipeline("main"));

    std::array<vk::Buffer, 2> vertexBuffers{*vertexBuffer->get(), *instanceBuffers[currentFrame]->get()};
    std::array<vk::DeviceSize, 2> offsets{0, 0};
    commandBuffers[currentFrame].bindVertexBuffers(0, vertexBuffers, offsets);

    commandBuffers[currentFrame].bindIndexBuffer(*indexBuffer->get(), 0, vk::IndexType::eUint16);

    commandBuffers[currentFrame].bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *pipelineLayout,
        0,
        *descriptorSets[currentFrame],
        nullptr
    );

    commandBuffers[currentFrame].drawIndexed(6, static_cast<uint32_t>(quadInstances.size()), 0, 0, 0);

    ImGui_ImplVulkan_RenderDrawData(
        ImGui::GetDrawData(),
        *commandBuffers[currentFrame]
    );

    commandBuffers[currentFrame].endRenderPass();

    commandBuffers[currentFrame].end();

    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::SubmitInfo submitInfo;
    submitInfo.setWaitSemaphores(*imageAvailableSemaphores[currentFrame]);
    submitInfo.setWaitDstStageMask(waitStages);
    submitInfo.setCommandBuffers(*commandBuffers[currentFrame]);
    submitInfo.setSignalSemaphores(*renderFinishedSemaphores[currentFrame]);

    try {
        vulkanDevice->get().resetFences(*inFlightFences[currentFrame]);
        vulkanDevice->getGraphicsQueue().submit(submitInfo, *inFlightFences[currentFrame]);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to submit draw command buffer: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    swapChainManager->presentImage(imageIndex, *renderFinishedSemaphores[currentFrame]);

    currentFrame = (currentFrame + 1) % framesInFlight;
}

void Application::mainLoop() {
    LOG_INFO("Entering main loop...");

    while (!windowManager->shouldClose()) {
        glfwPollEvents();

        transform1.rotation.z += 0.01;

        drawFrame();

        if (framebufferResized) {
            recreateSwapChain();
        }
    }

    vulkanDevice->waitIdle();
    LOG_INFO("Main loop exited");
}

}
