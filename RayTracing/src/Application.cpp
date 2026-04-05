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
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <array>
#include <thread>
#include <chrono>

namespace RYRayTracing {

Application::Application()
    : windowManager(nullptr)
    , vulkanInstance(nullptr)
    , vulkanDevice(nullptr)
    , swapChainManager(nullptr)
    , surface(VK_NULL_HANDLE)
    , vertexBuffer(nullptr)
    , vertexShader(nullptr)
    , fragmentShader(nullptr)
    , pipelineLayout(VK_NULL_HANDLE)
    , graphicsPipeline(VK_NULL_HANDLE)
    , renderPass(VK_NULL_HANDLE)
    , commandPool(VK_NULL_HANDLE)
    , commandBuffers()
    , swapChainFramebuffers()
    , imageAvailableSemaphores()
    , renderFinishedSemaphores()
    , inFlightFences()
    , imagesInFlight()
    , currentFrame(0)
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

    windowManager = new WindowManager(windowConfig);
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

    LOG_INFO("Vulkan initialization completed - window created");
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

    // Cleanup other Vulkan resources
    if (vulkanDevice) {
        if (graphicsPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(vulkanDevice->get(), graphicsPipeline, nullptr);
            graphicsPipeline = VK_NULL_HANDLE;
        }

        if (pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(vulkanDevice->get(), pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }

        if (commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(vulkanDevice->get(), commandPool, nullptr);
            commandPool = VK_NULL_HANDLE;
        }
    }

    // Delete managed objects (in reverse order of creation)
    delete fragmentShader;
    fragmentShader = nullptr;

    delete vertexShader;
    vertexShader = nullptr;

    delete vertexBuffer;
    vertexBuffer = nullptr;

    delete vulkanDevice;    // This will also destroy the surface
    vulkanDevice = nullptr;

    delete vulkanInstance;
    vulkanInstance = nullptr;

    delete windowManager;
    windowManager = nullptr;

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

    vulkanInstance = new VulkanInstance(config);
    LOG_INFO("Vulkan instance created");
}

void Application::createDevice() {
    LOG_INFO("Creating Vulkan device...");

    surface = windowManager->createSurface(vulkanInstance->get());

    DeviceConfig deviceConfig;
    deviceConfig.requiredFeatures.samplerAnisotropy = VK_TRUE;

    vulkanDevice = new VulkanDevice(vulkanInstance->get(), surface, deviceConfig);
    LOG_INFO("Vulkan device created");
}

void Application::createGraphicsPipeline() {
    LOG_INFO("Creating graphics pipeline...");

    try {
        std::string vertShaderPath = "assets/shaders/triangle.vert.spv";
        std::string fragShaderPath = "assets/shaders/triangle.frag.spv";

        LOG_INFO("Loading vertex shader: " + vertShaderPath);

        ShaderModuleConfig vertexConfig;
        vertexConfig.filename = vertShaderPath;
        vertexConfig.stage = ShaderStage::VERTEX;
        vertexConfig.entryPoint = "main";
        vertexShader = new ShaderModule(vulkanDevice, vertexConfig);

        LOG_INFO("Loading fragment shader: " + fragShaderPath);

        ShaderModuleConfig fragmentConfig;
        fragmentConfig.filename = fragShaderPath;
        fragmentConfig.stage = ShaderStage::FRAGMENT;
        fragmentConfig.entryPoint = "main";
        fragmentShader = new ShaderModule(vulkanDevice, fragmentConfig);

        LOG_INFO("Shaders loaded successfully");

        // Create pipeline layout
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 0;
        pipelineLayoutInfo.pushConstantRangeCount = 0;

        VkResult result = vkCreatePipelineLayout(vulkanDevice->get(), &pipelineLayoutInfo, nullptr, &pipelineLayout);
        if (result != VK_SUCCESS) {
            throw VulkanException(result, "vkCreatePipelineLayout", __FUNCTION__, __FILE__, __LINE__);
        }

        LOG_INFO("Pipeline layout created");

        // Create graphics pipeline
        createGraphicsPipelineInternal();

        LOG_INFO("Graphics pipeline created successfully");

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create graphics pipeline: " + std::string(e.what()));
        throw;
    }
}

void Application::createGraphicsPipelineInternal() {
    LOG_INFO("Creating graphics pipeline internal...");

    if (!swapChainManager) {
        throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                            "Swap chain manager not initialized",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Vertex input binding description
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 5; // 2 position + 3 color
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // Vertex attribute descriptions
    std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};

    // Position attribute
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = 0;

    // Color attribute
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = sizeof(float) * 2;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapChainManager->getExtent().width;
    viewport.height = (float)swapChainManager->getExtent().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapChainManager->getExtent();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    // Dynamic state
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // Shader stages
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

    // Vertex shader
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertexShader->get();
    shaderStages[0].pName = "main";

    // Fragment shader
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragmentShader->get();
    shaderStages[1].pName = "main";

    // Graphics pipeline creation
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = shaderStages.size();
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = nullptr; // Optional
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
    pipelineInfo.basePipelineIndex = -1; // Optional

    VkResult result = vkCreateGraphicsPipelines(vulkanDevice->get(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline);
    if (result != VK_SUCCESS) {
        throw VulkanException(result, "vkCreateGraphicsPipelines", __FUNCTION__, __FILE__, __LINE__);
    }

    LOG_INFO("Graphics pipeline created");
}

void Application::createVertexBuffer() {
    LOG_INFO("Creating vertex buffer for triangle...");

    try {
        struct Vertex {
            float position[2];
            float color[3];
        };

        std::array<Vertex, 3> vertices = {{
            {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},    // Blue, top-left (顺时针起点)
            {{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},     // Green, top-right
            {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}}     // Red, bottom
        }};

        BufferConfig bufferConfig;
        bufferConfig.size = sizeof(vertices);
        bufferConfig.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferConfig.properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        vertexBuffer = new Buffer(vulkanDevice, bufferConfig);

        void* data = vertexBuffer->map();
        memcpy(data, vertices.data(), bufferConfig.size);
        vertexBuffer->unmap();

        LOG_INFO("Vertex buffer created with triangle data");

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create vertex buffer: " + std::string(e.what()));
        throw;
    }
}

void Application::mainLoop() {
    LOG_INFO("Starting main loop...");
    LOG_INFO("Rendering colored triangle");
    LOG_INFO("Press ESC or close window to exit");

    while (!windowManager->shouldClose()) {
        windowManager->pollEvents();

        try {
            drawFrame();
        } catch (const VulkanException& e) {
            if (e.getErrorCode() == VK_ERROR_OUT_OF_DATE_KHR || e.getErrorCode() == VK_SUBOPTIMAL_KHR) {
                // Swap chain needs recreation
                recreateSwapChain();
            } else {
                LOG_ERROR("Failed to draw frame: " + std::string(e.what()));
                throw;
            }
        }

        static int frameCount = 0;
        frameCount++;

        // Log every 60 frames (approx 1 second at 60 FPS)
        if (frameCount % 60 == 0) {
            LOG_DEBUG("Frame: " + std::to_string(frameCount) +
                     ", Window size: " + std::to_string(windowWidth) +
                     "x" + std::to_string(windowHeight));
        }
    }

    // Wait for device to finish before cleanup
    vulkanDevice->waitIdle();

    LOG_INFO("Main loop completed");
}

// Static callback for window resize
void Application::onWindowResize(int width, int height, void* userData) {
    Application* app = static_cast<Application*>(userData);
    if (app) {
        app->framebufferResized = true;
        app->windowWidth = width;
        app->windowHeight = height;
    }
}


void Application::createSwapChain() {
    LOG_INFO("Creating swap chain...");

    try {
        // Create swap chain configuration
        SwapChainConfig swapChainConfig;
        swapChainConfig.presentMode = VK_PRESENT_MODE_FIFO_KHR; // VSync
        swapChainConfig.surfaceFormat = { VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
        swapChainConfig.minImageCount = 2; // Double buffering
        swapChainConfig.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapChainConfig.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapChainConfig.clipped = VK_TRUE;

        // Create swap chain manager
        swapChainManager = new SwapChainManager(vulkanDevice,
                                               surface,
                                               windowWidth, windowHeight,
                                               swapChainConfig);

        LOG_INFO("Swap chain created successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create swap chain: " + std::string(e.what()));
        throw;
    }
}

void Application::createRenderPass() {
    LOG_INFO("Creating render pass...");

    try {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapChainManager->getImageFormat();
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        VkResult result = vkCreateRenderPass(vulkanDevice->get(), &renderPassInfo, nullptr, &renderPass);
        if (result != VK_SUCCESS) {
            throw VulkanException(result, "vkCreateRenderPass", __FUNCTION__, __FILE__, __LINE__);
        }

        LOG_INFO("Render pass created successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create render pass: " + std::string(e.what()));
        throw;
    }
}

void Application::createFramebuffers() {
    LOG_INFO("Creating framebuffers...");

    try {
        const auto& swapChainImageViews = swapChainManager->getImageViews();
        swapChainFramebuffers.resize(swapChainImageViews.size());

        for (size_t i = 0; i < swapChainImageViews.size(); i++) {
            VkImageView attachments[] = {
                swapChainImageViews[i]
            };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = swapChainManager->getExtent().width;
            framebufferInfo.height = swapChainManager->getExtent().height;
            framebufferInfo.layers = 1;

            VkResult result = vkCreateFramebuffer(vulkanDevice->get(), &framebufferInfo, nullptr, &swapChainFramebuffers[i]);
            if (result != VK_SUCCESS) {
                // Clean up any framebuffers created so far
                for (size_t j = 0; j < i; j++) {
                    vkDestroyFramebuffer(vulkanDevice->get(), swapChainFramebuffers[j], nullptr);
                }
                swapChainFramebuffers.clear();
                throw VulkanException(result, "vkCreateFramebuffer", __FUNCTION__, __FILE__, __LINE__);
            }
        }

        LOG_INFO("Created " + std::to_string(swapChainFramebuffers.size()) + " framebuffers");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create framebuffers: " + std::string(e.what()));
        throw;
    }
}

void Application::createCommandPool() {
    LOG_INFO("Creating command pool...");

    try {
        QueueFamilyIndices queueFamilyIndices = vulkanDevice->getQueueFamilyIndices();

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

        VkResult result = vkCreateCommandPool(vulkanDevice->get(), &poolInfo, nullptr, &commandPool);
        if (result != VK_SUCCESS) {
            throw VulkanException(result, "vkCreateCommandPool", __FUNCTION__, __FILE__, __LINE__);
        }

        LOG_INFO("Command pool created successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create command pool: " + std::string(e.what()));
        throw;
    }
}

void Application::createCommandBuffers() {
    LOG_INFO("Creating command buffers...");

    try {
        // Check if framebuffers are created
        if (swapChainFramebuffers.empty()) {
            throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                                "No framebuffers available for command buffer creation",
                                __FUNCTION__, __FILE__, __LINE__);
        }

        // Check if graphics pipeline is created
        if (graphicsPipeline == VK_NULL_HANDLE) {
            throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                                "Graphics pipeline not created before command buffer creation",
                                __FUNCTION__, __FILE__, __LINE__);
        }

        // Check if vertex buffer is created
        if (vertexBuffer == nullptr) {
            throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                                "Vertex buffer not created before command buffer creation",
                                __FUNCTION__, __FILE__, __LINE__);
        }

        commandBuffers.resize(swapChainFramebuffers.size());

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

        VkResult result = vkAllocateCommandBuffers(vulkanDevice->get(), &allocInfo, commandBuffers.data());
        if (result != VK_SUCCESS) {
            throw VulkanException(result, "vkAllocateCommandBuffers", __FUNCTION__, __FILE__, __LINE__);
        }

        // Record command buffers
        for (size_t i = 0; i < commandBuffers.size(); i++) {
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = 0; // Optional
            beginInfo.pInheritanceInfo = nullptr; // Optional

            result = vkBeginCommandBuffer(commandBuffers[i], &beginInfo);
            if (result != VK_SUCCESS) {
                throw VulkanException(result, "vkBeginCommandBuffer", __FUNCTION__, __FILE__, __LINE__);
            }

            // Begin render pass
            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = renderPass;
            renderPassInfo.framebuffer = swapChainFramebuffers[i];
            renderPassInfo.renderArea.offset = {0, 0};
            renderPassInfo.renderArea.extent = swapChainManager->getExtent();

            VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}}; // Black background
            renderPassInfo.clearValueCount = 1;
            renderPassInfo.pClearValues = &clearColor;

            vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            // Bind graphics pipeline
            vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

            // Set dynamic viewport and scissor
            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = (float)swapChainManager->getExtent().width;
            viewport.height = (float)swapChainManager->getExtent().height;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(commandBuffers[i], 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = swapChainManager->getExtent();
            vkCmdSetScissor(commandBuffers[i], 0, 1, &scissor);

            // Bind vertex buffer
            VkBuffer vertexBufferHandle = vertexBuffer->get();
            if (vertexBufferHandle == VK_NULL_HANDLE) {
                throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                                    "Vertex buffer handle is null",
                                    __FUNCTION__, __FILE__, __LINE__);
            }
            VkBuffer vertexBuffers[] = {vertexBufferHandle};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);

            // Draw triangle
            vkCmdDraw(commandBuffers[i], 3, 1, 0, 0);

            // End render pass
            vkCmdEndRenderPass(commandBuffers[i]);

            result = vkEndCommandBuffer(commandBuffers[i]);
            if (result != VK_SUCCESS) {
                throw VulkanException(result, "vkEndCommandBuffer", __FUNCTION__, __FILE__, __LINE__);
            }
        }

        LOG_INFO("Created " + std::to_string(commandBuffers.size()) + " command buffers");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create command buffers: " + std::string(e.what()));
        throw;
    }
}

void Application::createSyncObjects() {
    LOG_INFO("Creating sync objects...");

    try {
        // We need one set of semaphores per swapchain image to avoid reuse issues
        uint32_t imageCount = swapChainManager ? swapChainManager->getImageCount() : 0;
        if (imageCount == 0) {
            throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                                "Cannot create sync objects: swap chain has no images",
                                __FUNCTION__, __FILE__, __LINE__);
        }

        // Create semaphores and fences for each swapchain image
        imageAvailableSemaphores.resize(imageCount);
        renderFinishedSemaphores.resize(imageCount);
        inFlightFences.resize(imageCount);
        imagesInFlight.resize(imageCount, VK_NULL_HANDLE);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < imageCount; i++) {
            VkResult result;

            result = vkCreateSemaphore(vulkanDevice->get(), &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]);
            if (result != VK_SUCCESS) {
                throw VulkanException(result, "vkCreateSemaphore (imageAvailable)", __FUNCTION__, __FILE__, __LINE__);
            }

            result = vkCreateSemaphore(vulkanDevice->get(), &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]);
            if (result != VK_SUCCESS) {
                throw VulkanException(result, "vkCreateSemaphore (renderFinished)", __FUNCTION__, __FILE__, __LINE__);
            }

            result = vkCreateFence(vulkanDevice->get(), &fenceInfo, nullptr, &inFlightFences[i]);
            if (result != VK_SUCCESS) {
                throw VulkanException(result, "vkCreateFence", __FUNCTION__, __FILE__, __LINE__);
            }
        }

        // Set framesInFlight to match swapchain image count
        framesInFlight = imageCount;

        LOG_INFO("Created sync objects for " + std::to_string(imageCount) + " swapchain images, framesInFlight = " + std::to_string(framesInFlight));
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create sync objects: " + std::string(e.what()));
        throw;
    }
}

void Application::drawFrame() {
    // Check if we have sync objects
    if (imageAvailableSemaphores.empty() || renderFinishedSemaphores.empty() || inFlightFences.empty()) {
        LOG_ERROR("Sync objects not initialized");
        return;
    }

    // Ensure currentFrame is within bounds
    if (currentFrame >= imageAvailableSemaphores.size()) {
        currentFrame = 0;
    }

    // Wait for the fence of the current frame
    vkWaitForFences(vulkanDevice->get(), 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    // Acquire next image from swap chain
    uint32_t imageIndex;
    try {
        imageIndex = swapChainManager->acquireNextImage(imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE);

        // Check if swap chain needs recreation
        if (imageIndex == UINT32_MAX) {
            recreateSwapChain();
            return;
        }
    } catch (const VulkanException& e) {
        if (e.getErrorCode() == VK_ERROR_OUT_OF_DATE_KHR || e.getErrorCode() == VK_SUBOPTIMAL_KHR) {
            recreateSwapChain();
            return;
        }
        throw;
    }

    // Check if a previous frame is using this image (i.e. there is its fence to wait on)
    if (imageIndex < imagesInFlight.size() && imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(vulkanDevice->get(), 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }

    // Mark the image as now being in use by this frame
    if (imageIndex < imagesInFlight.size()) {
        imagesInFlight[imageIndex] = inFlightFences[currentFrame];
    }

    // Reset fence for the current frame
    vkResetFences(vulkanDevice->get(), 1, &inFlightFences[currentFrame]);

    // Submit command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    // Wait for imageAvailable semaphore (signaled when swapchain image is ready)
    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

    // Signal renderFinished semaphore when rendering is complete
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult result = vkQueueSubmit(vulkanDevice->getGraphicsQueue(), 1, &submitInfo, inFlightFences[currentFrame]);
    if (result != VK_SUCCESS) {
        throw VulkanException(result, "vkQueueSubmit", __FUNCTION__, __FILE__, __LINE__);
    }

    // Present the image
    try {
        swapChainManager->presentImage(imageIndex, renderFinishedSemaphores[currentFrame]);
    } catch (const VulkanException& e) {
        if (e.getErrorCode() == VK_ERROR_OUT_OF_DATE_KHR || e.getErrorCode() == VK_SUBOPTIMAL_KHR) {
            recreateSwapChain();
            return;
        }
        throw;
    }

    // Advance to next frame
    if (framesInFlight > 0) {
        currentFrame = (currentFrame + 1) % framesInFlight;
    }
}

void Application::cleanupSwapChain() {
    VkDevice device = vulkanDevice->get();

    // Destroy framebuffers
    for (auto framebuffer : swapChainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    swapChainFramebuffers.clear();

    // Free command buffers
    if (!commandBuffers.empty()) {
        vkFreeCommandBuffers(device, commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
        commandBuffers.clear();
    }

    // Destroy render pass
    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }

    // Destroy swap chain manager
    delete swapChainManager;
    swapChainManager = nullptr;

    // Clear imagesInFlight tracking
    imagesInFlight.clear();
}

void Application::cleanupSyncObjects() {
    if (!vulkanDevice) return;

    VkDevice device = vulkanDevice->get();
    if (device == VK_NULL_HANDLE) return;

    // Destroy image available semaphores
    for (size_t i = 0; i < imageAvailableSemaphores.size(); i++) {
        if (imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, imageAvailableSemaphores[i], nullptr);
            imageAvailableSemaphores[i] = VK_NULL_HANDLE;
        }
    }

    // Destroy render finished semaphores
    for (size_t i = 0; i < renderFinishedSemaphores.size(); i++) {
        if (renderFinishedSemaphores[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, renderFinishedSemaphores[i], nullptr);
            renderFinishedSemaphores[i] = VK_NULL_HANDLE;
        }
    }

    // Destroy in-flight fences
    for (size_t i = 0; i < inFlightFences.size(); i++) {
        if (inFlightFences[i] != VK_NULL_HANDLE) {
            vkDestroyFence(device, inFlightFences[i], nullptr);
            inFlightFences[i] = VK_NULL_HANDLE;
        }
    }

    // Clear vectors
    imageAvailableSemaphores.clear();
    renderFinishedSemaphores.clear();
    inFlightFences.clear();
    imagesInFlight.clear();

    LOG_INFO("Sync objects cleaned up");
}

void Application::recreateSwapChain() {
    LOG_INFO("Recreating swap chain due to window resize or swap chain invalidation");

    // Wait for device to be idle
    vulkanDevice->waitIdle();

    // Cleanup old swap chain resources
    cleanupSwapChain();

    // Cleanup old sync objects (they may be in signaled state)
    cleanupSyncObjects();

    // Get new window size
    auto [width, height] = windowManager->getFramebufferSize();
    windowWidth = width;
    windowHeight = height;

    // Recreate swap chain
    createSwapChain();
    createRenderPass();
    createFramebuffers();
    createCommandBuffers();

    // Recreate sync objects for the new swap chain
    createSyncObjects();

    // Reset current frame index
    currentFrame = 0;

    LOG_INFO("Swap chain recreated successfully, framesInFlight = " + std::to_string(framesInFlight));
}

} // namespace RYRayTracing