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

#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <array>
#include <thread>
#include <chrono>

namespace RYRayTracing {

Application::Application()
    : currentFrame(0)
    , framesInFlight(0)
    , windowWidth(800)
    , windowHeight(600)
    , framebufferResized(false) {

    // Initialize logging system
    Logger::init("raytracing.log");
    LOG_INFO("=== Vulkan Triangle Rendering Application ===");
}

Application::~Application() {
    cleanup();
    LOG_INFO("=== Application Shutting Down ===");

    // Terminate GLFW
    glfwTerminate();

    Logger::shutdown();
}

void Application::run() {
    try {
        LOG_INFO("Starting Vulkan triangle application");

        // Initialize Vulkan and create window
        initVulkan();

        // Run main render loop
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
    windowConfig.title = "Vulkan Triangle";
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
    createGraphicsPipeline();
    createFramebuffers();
    createCommandPool();
    createVertexBuffer();
    createCommandBuffers();
    createSyncObjects();

    LOG_INFO("Full rendering pipeline initialized - triangle should be displayed");
    LOG_INFO("Press ESC to exit, or close the window");
}

void Application::cleanup() {
    LOG_INFO("Cleaning up resources...");

    if (vulkanDevice) {
        vulkanDevice->waitIdle();
    }

    // Cleanup swap chain resources
    cleanupSwapChain();

    // Cleanup sync objects
    cleanupSyncObjects();

    // Clear unique_ptrs (automatic cleanup)
    commandBuffers.clear();
    pipelineLayout = nullptr;
    fragmentShader.reset();
    vertexShader.reset();
    vertexBuffer.reset();
    commandManager.reset();
    pipelineManager.reset();
    renderPassManager.reset();
    swapChainManager.reset();
    vulkanDevice.reset();
    surface = nullptr;
    vulkanInstance.reset();
    windowManager.reset();

    LOG_INFO("Cleanup completed");
}

void Application::createInstance() {
    LOG_INFO("Creating Vulkan instance...");

    InstanceConfig config;
    config.applicationName = "Vulkan Triangle";
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

    // Create surface
    surface = windowManager->createSurface(vulkanInstance->get());
    LOG_INFO("Vulkan surface created");

    DeviceConfig deviceConfig;
    deviceConfig.requiredFeatures.samplerAnisotropy = true;

    vulkanDevice = std::make_unique<VulkanDevice>(vulkanInstance->get(), surface, deviceConfig);
    LOG_INFO("Vulkan device created");
}

void Application::createSwapChain() {
    LOG_INFO("Creating swap chain...");

    swapChainManager = std::make_unique<SwapChainManager>(
        vulkanDevice.get(), surface, windowWidth, windowHeight);

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

void Application::createGraphicsPipeline() {
    LOG_INFO("Creating graphics pipeline...");

    try {
        std::string vertShaderPath = "assets/shaders/triangle.vert.spv";
        std::string fragShaderPath = "assets/shaders/triangle.frag.spv";

        LOG_INFO("Loading vertex shader: " + vertShaderPath);
        vertexShader = std::make_unique<ShaderModule>(
            ShaderModule::createVertexShader(vulkanDevice.get(), vertShaderPath));

        LOG_INFO("Loading fragment shader: " + fragShaderPath);
        fragmentShader = std::make_unique<ShaderModule>(
            ShaderModule::createFragmentShader(vulkanDevice.get(), fragShaderPath));

        LOG_INFO("Shaders loaded successfully");

        // Create pipeline layout
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
            {},
            {},
            {}
        };

        pipelineLayout = vulkanDevice->get().createPipelineLayout(pipelineLayoutInfo);
        LOG_INFO("Pipeline layout created");

        // Create pipeline manager
        pipelineManager = std::make_unique<PipelineManager>(vulkanDevice->get());

        // Create graphics pipeline
        PipelineConfig pipelineConfig;
        pipelineConfig.vertexShader = *vertexShader->get();
        pipelineConfig.fragmentShader = *fragmentShader->get();
        pipelineConfig.pipelineLayout = *pipelineLayout;
        pipelineConfig.renderPass = *renderPassManager->get();

        // Vertex input description
        struct Vertex {
            float pos[2];
            float color[3];
        };

        pipelineConfig.vertexBindingDescription = vk::VertexInputBindingDescription{
            0,                          // binding
            sizeof(Vertex),             // stride
            vk::VertexInputRate::eVertex // inputRate
        };

        pipelineConfig.vertexAttributeDescriptions = {
            // Position attribute (location 0)
            vk::VertexInputAttributeDescription{
                0,                          // location
                0,                          // binding
                vk::Format::eR32G32Sfloat,  // format
                0                           // offset
            },
            // Color attribute (location 1)
            vk::VertexInputAttributeDescription{
                1,                          // location
                0,                          // binding
                vk::Format::eR32G32B32Sfloat, // format
                sizeof(float) * 2           // offset
            }
        };

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

    // Triangle vertices
    struct Vertex {
        float pos[2];
        float color[3];
    };

    std::vector<Vertex> vertices = {
        {{ 0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}},
        {{-0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}}
    };

    vertexBuffer = std::make_unique<Buffer>(
        Buffer::createVertexBuffer(vulkanDevice.get(), vertices.data(), sizeof(Vertex) * vertices.size()));

    LOG_INFO("Vertex buffer created");
}

void Application::createCommandBuffers() {
    LOG_INFO("Creating command buffers...");

    commandBuffers = commandManager->allocateCommandBuffers(swapChainFramebuffers.size());

    // Record command buffers
    for (size_t i = 0; i < commandBuffers.size(); i++) {
        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eSimultaneousUse);

        commandBuffers[i].begin(beginInfo);

        // Set viewport and scissor (dynamic state)
        vk::Viewport viewport;
        viewport.setX(0.0f);
        viewport.setY(0.0f);
        viewport.setWidth(static_cast<float>(swapChainManager->getExtent().width));
        viewport.setHeight(static_cast<float>(swapChainManager->getExtent().height));
        viewport.setMinDepth(0.0f);
        viewport.setMaxDepth(1.0f);
        commandBuffers[i].setViewport(0, viewport);

        vk::Rect2D scissor;
        scissor.setOffset({0, 0});
        scissor.setExtent(swapChainManager->getExtent());
        commandBuffers[i].setScissor(0, scissor);

        // Begin render pass
        vk::ClearValue clearColor;
        clearColor.color = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f};
        
        vk::RenderPassBeginInfo renderPassInfo;
        renderPassInfo.setRenderPass(*renderPassManager->get());
        renderPassInfo.setFramebuffer(*swapChainFramebuffers[i]);
        renderPassInfo.setRenderArea(vk::Rect2D{{0, 0}, swapChainManager->getExtent()});
        renderPassInfo.setClearValues(clearColor);

        commandBuffers[i].beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        // Bind pipeline
        commandBuffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, *pipelineManager->getPipeline("main"));

        // Bind vertex buffer
        vk::Buffer vertexBuffers[] = {*vertexBuffer->get()};
        vk::DeviceSize offsets[] = {0};
        commandBuffers[i].bindVertexBuffers(0, vertexBuffers, offsets);

        // Draw triangle
        commandBuffers[i].draw(3, 1, 0, 0);

        // End render pass
        commandBuffers[i].endRenderPass();

        // End command buffer
        commandBuffers[i].end();
    }

    LOG_INFO("Command buffers created and recorded: " + std::to_string(commandBuffers.size()));
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

void Application::cleanupSwapChain() {
    swapChainFramebuffers.clear();
    if (swapChainManager) {
        // swapChainManager cleanup is automatic with unique_ptr
    }
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
    createCommandBuffers();
}

void Application::drawFrame() {
    // Wait for the previous frame to finish
    try {
        (void)vulkanDevice->get().waitForFences(*inFlightFences[currentFrame], true, UINT64_MAX);
    } catch (const vk::SystemError& e) {
        throw VulkanException(static_cast<vk::Result>(e.code().value()),
                            std::string("Failed to wait for fence: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Acquire next image
    uint32_t imageIndex = swapChainManager->acquireNextImage(*imageAvailableSemaphores[currentFrame]);

    if (imageIndex == UINT32_MAX) {
        recreateSwapChain();
        return;
    }

    // Check if a previous frame is using this image
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

    // Submit command buffer
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::SubmitInfo submitInfo;
    submitInfo.setWaitSemaphores(*imageAvailableSemaphores[currentFrame]);
    submitInfo.setWaitDstStageMask(waitStages);
    submitInfo.setCommandBuffers(*commandBuffers[imageIndex]);
    submitInfo.setSignalSemaphores(*renderFinishedSemaphores[currentFrame]);

    try {
        vulkanDevice->get().resetFences(*inFlightFences[currentFrame]);
        vulkanDevice->getGraphicsQueue().submit(submitInfo, *inFlightFences[currentFrame]);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to submit draw command buffer: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Present
    swapChainManager->presentImage(imageIndex, *renderFinishedSemaphores[currentFrame]);

    currentFrame = (currentFrame + 1) % framesInFlight;
}

void Application::mainLoop() {
    LOG_INFO("Entering main loop...");

    while (!windowManager->shouldClose()) {
        glfwPollEvents();
        drawFrame();

        if (framebufferResized) {
            recreateSwapChain();
            framebufferResized = false;
        }
    }

    vulkanDevice->waitIdle();
    LOG_INFO("Main loop exited");
}

void Application::onWindowResize(int width, int height, void* userData) {
    auto app = reinterpret_cast<Application*>(userData);
    app->framebufferResized = true;
    app->windowWidth = width;
    app->windowHeight = height;
}

} // namespace RYRayTracing
