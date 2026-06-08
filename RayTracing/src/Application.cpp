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
#include "tiny_obj_loader.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <array>
#include <thread>
#include <chrono>
#include <cstring>

#include "imgui_internal.h"
#include "Light.h"
#include "Scene.h"

namespace RYRayTracing {

Application::Application()
      : m_depthFormat(vk::Format::eUndefined)
      , m_currentFrame(0)
      , m_framesInFlight(0)
      , m_windowWidth(1920)
      , m_windowHeight(1080)
      , m_framebufferResized(false) {
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

void Application::createModels() {
    m_models.reserve(3);
    m_instances.reserve(3);

    // Viking room
    {
        Model model;
        model.name = "Viking Room";
        model.loadFromObj("assets/models/viking_room.obj");
        model.createBuffers(m_vulkanDevice.get());
        model.createTexture(m_vulkanDevice.get(), "assets/textures/viking_room.png");
        m_models.push_back(std::move(model));
    }

    // Bunny
    {
        Model model;
        model.name = "Bunny";
        model.loadFromObj("assets/models/bunny.obj");
        model.createBuffers(m_vulkanDevice.get());
        model.createTexture(m_vulkanDevice.get(), "assets/textures/bunny.png");
        m_models.push_back(std::move(model));
    }

    // Basketball
    {
        Model model;
        model.name = "Basketball";
        model.loadFromObj("assets/models/sphere.obj");
        model.createBuffers(m_vulkanDevice.get());
        model.createTexture(m_vulkanDevice.get(), "assets/textures/basketball.png");
        m_models.push_back(std::move(model));
    }

    // Create instances referencing the models
    {
        Instance instance;
        instance.name = "Viking Room";
        instance.model = &m_models[0];
        instance.transform = Transform{};
        instance.transform.rotation = glm::vec3(0.38, -0.65, -2.18);
        instance.transform.translation = { 0, 0, 0 };
        instance.transform.scale = glm::vec3(1.0f);
        instance.createBuffer(m_vulkanDevice.get());
        m_instances.push_back(std::move(instance));
    }
    {
        Instance instance;
        instance.name = "Bunny";
        instance.model = &m_models[1];
        instance.transform = Transform{};
        instance.transform.rotation = { 0.0, 0.77, 0.0 };
        instance.transform.translation = { -2, -0.8, 0.00 };
        instance.transform.scale = glm::vec3(1.0f);
        instance.createBuffer(m_vulkanDevice.get());
        m_instances.push_back(std::move(instance));
    }
    {
        Instance instance;
        instance.name = "Basketball";
        instance.model = &m_models[2];
        instance.transform = Transform{};
        instance.transform.rotation = glm::vec3(0.0f);
        instance.transform.translation = { 2, 0, 0};
        instance.transform.scale = glm::vec3(0.85f);
        instance.createBuffer(m_vulkanDevice.get());
        m_instances.push_back(std::move(instance));
    }
}

void Application::initVulkan() {
    LOG_INFO("Initializing Vulkan...");

    WindowConfig windowConfig;
    windowConfig.width = m_windowWidth;
    windowConfig.height = m_windowHeight;
    windowConfig.title = "Lab 3 Blinn-Phong Rendering";
    windowConfig.resizable = true;

    m_windowManager = std::make_unique<WindowManager>(windowConfig);
    m_windowManager->init();

    WindowCallbacks callbacks;
    callbacks.onResize = [this](int width, int height) {
        this->m_framebufferResized = true;
        this->m_windowWidth = width;
        this->m_windowHeight = height;
    };
    m_windowManager->setCallbacks(callbacks);

    createInstance();
    createDevice();
    createSwapChain();
    createRenderPass();
    createDescriptorSetLayout();
    createGraphicsPipeline(); // pipeline needs descriptor set layout
    createDepthResources();
    createFramebuffers();
    createCommandPool();
    createModels();
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();
    createSyncObjects();

    createRtStorageImage();
    createRtDescriptorSetLayout();
    createRtDescriptorPool();
    createRtComputePipeline();
    createRtCameraUniformBuffers();
    initSpheres();
    createSphereBuffer();
    createRtDescriptorSets();
    createFullscreenDescriptorSet();  // creates layout + set (must be before pipeline)
    createFullscreenPipeline();

    LOG_INFO("Full rendering pipeline initialized (RT compute + display)");
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

    m_imguiPool = m_vulkanDevice->get().createDescriptorPool(pool_info);

    ImGui::CreateContext();

    ImGui_ImplGlfw_InitForVulkan(m_windowManager->getHandle(), true);

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance = *m_vulkanInstance->get();
    init_info.PhysicalDevice = *m_vulkanDevice->getPhysical();
    init_info.Device = *m_vulkanDevice->get();
    init_info.Queue = m_vulkanDevice->getGraphicsQueue();
    init_info.DescriptorPool = *m_imguiPool;
    init_info.MinImageCount = m_swapChainManager->getImageCount();
    init_info.ImageCount = m_swapChainManager->getImageCount();

    ImGui_ImplVulkan_Init(&init_info, *m_renderPassManager->get());

    auto& cmd = m_commandBuffers[0];
    cmd.reset();

    vk::CommandBufferBeginInfo beginInfo{};
    cmd.begin(beginInfo);

    ImGui_ImplVulkan_CreateFontsTexture(*cmd);

    cmd.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.setCommandBuffers(*cmd);

    m_vulkanDevice->getGraphicsQueue().submit(submitInfo);
    m_vulkanDevice->getGraphicsQueue().waitIdle();

    ImGui_ImplVulkan_DestroyFontUploadObjects();
    m_imguiInitialized = true;
}

void Application::initComponents() {
    m_camera.SetAspectRatio(static_cast<float>(m_windowWidth) / m_windowHeight);
    m_camera.SetPerspective(glm::radians(45.0f), 1, 100);
    m_cameraTransform.translation = { 0, 0, 6 };
    m_cameraTransform.rotation = { 0, 0, 0 };

    m_lights.pointLight1.pos = {4.0f, 4.0f, 4.0f};
    m_lights.pointLight1.color = {1.0f, 1.0f, 1.0f};
    m_lights.pointLight1.intensity = 20.0f;
    m_lights.pointLight1.maxDistance = 30.0f;

    m_lights.pointLight2.pos = {-4.0f, 4.0f, 4.0f};
    m_lights.pointLight2.color = {1.0f, 1.0f, 1.0f};
    m_lights.pointLight2.intensity = 20.0f;
    m_lights.pointLight2.maxDistance = 30.0f;

    m_lights.spotLight.pos = {3.0f, 2.0f, 2.0f};
    m_lights.spotLight.dir = glm::normalize(glm::vec3{-1.0f, -1.0f, -1.0f});
    m_lights.spotLight.color = {0.0f, 1.0f, 0.0f};
    m_lights.spotLight.cosineInclinationAngle = cos(glm::radians(15.5f));
    m_lights.spotLight.cosineExclusivityAngle = cos(glm::radians(32.5f));
    m_lights.spotLight.intensity = 20.0f;
    m_lights.spotLight.maxDistance = 15.0f;

    m_lights.directionalLight.dir = glm::normalize(glm::vec3{-0.5f, -1.0f, -0.5f});
    m_lights.directionalLight.color = {1.0f, 0.95f, 0.9f};
    m_lights.directionalLight.intensity = 0.5f;

    m_lights.ambientArgs = {0.1f, 0.1f, 0.1f, 0.1f};
    m_lights.diffuseStrength = 0.5f;
    m_lights.specularStrength = 1.0f;
}

void Application::cleanup() {
    LOG_INFO("Cleaning up resources...");

    if (m_vulkanDevice) {
        m_vulkanDevice->waitIdle();
    }

    if (m_imguiInitialized) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    cleanupSwapChain();
    cleanupSyncObjects();
    cleanupRtResources();

    m_mappedLightUniformData.clear();

    m_commandBuffers.clear();
    m_uboDescriptorSets.clear();
    m_descriptorPool = nullptr;
    m_uboDescriptorSetLayout = nullptr;
    m_textureDescriptorSetLayout = nullptr;
    m_imguiPool = nullptr;
    m_pipelineLayout = nullptr;
    m_fragmentShader.reset();
    m_vertexShader.reset();
    m_lightUniformBuffers.clear();
    m_instances.clear();
    m_models.clear();
    m_commandManager.reset();
    m_pipelineManager.reset();
    m_renderPassManager.reset();
    m_swapChainManager.reset();
    m_vulkanDevice.reset();
    m_windowManager.reset();
    m_vulkanInstance.reset();

    LOG_INFO("Cleanup completed");
}

void Application::createInstance() {
    LOG_INFO("Creating Vulkan instance...");

    InstanceConfig config;
    config.applicationName = "Lab 3 Blinn-Phong Rendering";
    config.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    config.engineName = "No Engine";
    config.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    config.apiVersion = VK_API_VERSION_1_3;
    config.enableValidation = Validation::shouldEnableValidation();

    m_vulkanInstance = std::make_unique<VulkanInstance>(config);
    LOG_INFO("Vulkan instance created");
}

void Application::createDevice() {
    LOG_INFO("Creating Vulkan device...");

    m_windowManager->createSurface(m_vulkanInstance->get());
    LOG_INFO("Vulkan surface created");

    DeviceConfig deviceConfig;
    deviceConfig.requiredFeatures.samplerAnisotropy = true;

    m_vulkanDevice = std::make_unique<VulkanDevice>(m_vulkanInstance->get(), m_windowManager->getSurface(), deviceConfig);
    LOG_INFO("Vulkan device created");
}

void Application::createSwapChain() {
    LOG_INFO("Creating swap chain...");

    m_swapChainManager = std::make_unique<SwapChainManager>(
        *m_vulkanDevice, m_windowManager->getSurface(), m_windowWidth, m_windowHeight);

    LOG_INFO("Swap chain created");
}

void Application::createRenderPass() {
    LOG_INFO("Creating render pass...");

    m_depthFormat = findDepthFormat({vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint});

    RenderPassConfig config;
    config.colorFormat = m_swapChainManager->getImageFormat();
    config.depthFormat = m_depthFormat;
    config.clearColors = true;
    config.clearDepth = true;

    m_renderPassManager = std::make_unique<RenderPassManager>(m_vulkanDevice->get(), config);
    LOG_INFO("Render pass created");
}

void Application::createDescriptorSetLayout() {
    LOG_INFO("Creating descriptor set layout...");

    {
        std::array<vk::DescriptorSetLayoutBinding, 2> layoutBinding;
        vk::DescriptorSetLayoutBinding& cameraUboLayoutBinding = layoutBinding[0];
        cameraUboLayoutBinding.binding = 0;
        cameraUboLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
        cameraUboLayoutBinding.descriptorCount = 1;
        cameraUboLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

        vk::DescriptorSetLayoutBinding& lightUboLayoutBinding = layoutBinding[1];
        lightUboLayoutBinding.binding = 1;
        lightUboLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
        lightUboLayoutBinding.descriptorCount = 1;
        lightUboLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo;
        layoutInfo.setBindings(layoutBinding);
        m_uboDescriptorSetLayout = m_vulkanDevice->get().createDescriptorSetLayout(layoutInfo);
    }

    {
        vk::DescriptorSetLayoutBinding samplerLayoutBinding;
        samplerLayoutBinding.binding = 0;
        samplerLayoutBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo textureLayoutInfo;
        textureLayoutInfo.setBindings(samplerLayoutBinding);
        m_textureDescriptorSetLayout = m_vulkanDevice->get().createDescriptorSetLayout(textureLayoutInfo);
    }

    LOG_INFO("Descriptor set layout created");
}

void Application::createGraphicsPipeline() {
    LOG_INFO("Creating graphics pipeline...");

    try {
        std::string shaderPath = "shaders/shader.spv";

        LOG_INFO("Loading shader: " + shaderPath);
        m_vertexShader = std::make_unique<ShaderModule>(
            ShaderModule::createVertexShader(m_vulkanDevice.get(), shaderPath));

        m_fragmentShader = std::make_unique<ShaderModule>(
            ShaderModule::createFragmentShader(m_vulkanDevice.get(), shaderPath));

        LOG_INFO("Shaders loaded successfully");

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {*m_uboDescriptorSetLayout, *m_textureDescriptorSetLayout};
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setSetLayouts(setLayouts);

        m_pipelineLayout = m_vulkanDevice->get().createPipelineLayout(pipelineLayoutInfo);
        LOG_INFO("Pipeline layout created");

        if (!m_pipelineManager) {
            m_pipelineManager = std::make_unique<PipelineManager>(m_vulkanDevice->get());
        }

        PipelineConfig pipelineConfig;
        pipelineConfig.vertexShader = *m_vertexShader->get();
        pipelineConfig.fragmentShader = *m_fragmentShader->get();
        pipelineConfig.pipelineLayout = *m_pipelineLayout;
        pipelineConfig.renderPass = *m_renderPassManager->get();
        pipelineConfig.vertexEntryPoint = "vertMain";
        pipelineConfig.fragmentEntryPoint = "fragMain";
        pipelineConfig.blendEnable = true;
        pipelineConfig.depthTestEnable = true;

        pipelineConfig.vertexBindingDescriptions = {getVertexBindingDescription(), getInstanceBindingDescription()};

        auto vertAttribs = getVertexAttributeDescriptions();
        auto instAttribs = getInstanceAttributeDescriptions();
        pipelineConfig.vertexAttributeDescriptions.reserve(vertAttribs.size() + instAttribs.size());
        pipelineConfig.vertexAttributeDescriptions.assign(
            vertAttribs.begin(), vertAttribs.end());
        pipelineConfig.vertexAttributeDescriptions.insert(
            pipelineConfig.vertexAttributeDescriptions.end(),
            instAttribs.begin(), instAttribs.end());

        m_pipelineManager->createPipeline("main", pipelineConfig);

        LOG_INFO("Graphics pipeline created successfully");

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create graphics pipeline: " + std::string(e.what()));
        throw;
    }
}

void Application::createFramebuffers() {
    LOG_INFO("Creating framebuffers...");

    m_swapChainFramebuffers.clear();

    auto& imageViews = m_swapChainManager->getImageViews();
    for (size_t i = 0; i < imageViews.size(); i++) {
        std::vector<vk::ImageView> attachments = {*imageViews[i], *m_depthImageView};

        vk::FramebufferCreateInfo framebufferInfo;
        framebufferInfo.setRenderPass(*m_renderPassManager->get());
        framebufferInfo.setAttachments(attachments);
        framebufferInfo.setWidth(m_swapChainManager->getExtent().width);
        framebufferInfo.setHeight(m_swapChainManager->getExtent().height);
        framebufferInfo.setLayers(1);

        try {
            m_swapChainFramebuffers.emplace_back(m_vulkanDevice->get().createFramebuffer(framebufferInfo));
        } catch (const vk::SystemError& e) {
            throw VulkanException(e.code(), std::string("Failed to create framebuffer: ") + e.what(),
                                __FUNCTION__, __FILE__, __LINE__);
        }
    }

    LOG_INFO("Framebuffers created: " + std::to_string(m_swapChainFramebuffers.size()));
}

void Application::createCommandPool() {
    LOG_INFO("Creating command pool...");

    CommandPoolConfig config;
    config.queueFamilyIndex = m_vulkanDevice->getGraphicsQueueFamily();

    m_commandManager = std::make_unique<CommandManager>(m_vulkanDevice->get(), config);
    LOG_INFO("Command pool created");
}



void Application::createUniformBuffers() {
    LOG_INFO("Creating uniform buffers...");

    m_framesInFlight = m_swapChainManager->getImageCount();
    createUniformBuffersImpl(sizeof(LightInfo), m_lightUniformBuffers, m_mappedLightUniformData);
    LOG_INFO("Uniform buffers created");
}

void Application::createUniformBuffersImpl(vk::DeviceSize bufferSize, std::vector<std::unique_ptr<Buffer>>& bufferOut, std::vector<void*>& mappedDataOut) const {
    bufferOut.clear();
    mappedDataOut.clear();
    bufferOut.reserve(m_framesInFlight);
    mappedDataOut.reserve(m_framesInFlight);

    for (size_t i = 0; i < m_framesInFlight; i++) {
        bufferOut.emplace_back(std::make_unique<Buffer>(
            Buffer::createUniformBuffer(m_vulkanDevice.get(), bufferSize)));
        mappedDataOut.push_back(bufferOut[i]->map(0, bufferSize));
    }

}


void Application::createDepthResources() {
    LOG_INFO("Creating depth resources...");

    vk::ImageCreateInfo imageInfo;
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent.width = m_swapChainManager->getExtent().width;
    imageInfo.extent.height = m_swapChainManager->getExtent().height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = m_depthFormat;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.samples = vk::SampleCountFlagBits::e1;

    try {
        m_depthImage = m_vulkanDevice->get().createImage(imageInfo);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to create depth image: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    auto memRequirements = m_depthImage.getMemoryRequirements();

    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = m_vulkanDevice->findMemoryType(memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

    try {
        m_depthImageMemory = m_vulkanDevice->get().allocateMemory(allocInfo);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to allocate depth image memory: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    m_depthImage.bindMemory(*m_depthImageMemory, 0);

    vk::ImageViewCreateInfo viewInfo;
    viewInfo.image = *m_depthImage;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = m_depthFormat;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    try {
        m_depthImageView = m_vulkanDevice->get().createImageView(viewInfo);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to create depth image view: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    LOG_INFO("Depth resources created");
}

void Application::createDescriptorPool() {
    LOG_INFO("Creating descriptor pool...");

    uint32_t modelCount = static_cast<uint32_t>(m_models.size());

    std::array<vk::DescriptorPoolSize, 2> poolSizes;
    poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(m_framesInFlight) * 2; /* one for camera, one for light */
    poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
    poolSizes[1].descriptorCount = modelCount + 1; // +1 for fullscreen RT display set

    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = static_cast<uint32_t>(m_framesInFlight) + modelCount + 1; // +1 for fullscreen # descriptor set
    poolInfo.setPoolSizes(poolSizes);
    m_descriptorPool = m_vulkanDevice->get().createDescriptorPool(poolInfo);

    LOG_INFO("Descriptor pool created");
}

void Application::createDescriptorSets() {
    LOG_INFO("Creating descriptor sets...");

    std::vector<vk::DescriptorSetLayout> layouts(m_framesInFlight, *m_uboDescriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.setSetLayouts(layouts);
    m_uboDescriptorSets = m_vulkanDevice->get().allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < m_framesInFlight; ++i) {
        vk::DescriptorBufferInfo lightBufferInfo;
        lightBufferInfo.buffer = *m_lightUniformBuffers[i]->get();
        lightBufferInfo.offset = 0;
        lightBufferInfo.range = sizeof(LightInfo);

        vk::WriteDescriptorSet lightDescriptorWrite;
        lightDescriptorWrite.dstSet = m_uboDescriptorSets[i];
        lightDescriptorWrite.dstBinding = 1;
        lightDescriptorWrite.dstArrayElement = 0;
        lightDescriptorWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
        lightDescriptorWrite.setBufferInfo(lightBufferInfo);

        m_vulkanDevice->get().updateDescriptorSets(std::array{lightDescriptorWrite}, nullptr);
    }

    for (auto& model : m_models) {
        model.createTextureDescriptorSet(m_vulkanDevice->get(), m_descriptorPool, m_textureDescriptorSetLayout);
    }

    LOG_INFO("Descriptor sets created");
}

void Application::createCommandBuffers() {
    LOG_INFO("Creating command buffers...");

    m_commandBuffers = m_commandManager->allocateCommandBuffers(m_swapChainFramebuffers.size());

    LOG_INFO("Command buffers created: " + std::to_string(m_commandBuffers.size()));
}

void Application::createSyncObjects() {
    LOG_INFO("Creating synchronization objects...");

    m_framesInFlight = m_swapChainManager->getImageCount();

    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();
    m_imagesInFlight.resize(m_framesInFlight, nullptr);

    for (size_t i = 0; i < m_framesInFlight; i++) {
        try {
            m_imageAvailableSemaphores.emplace_back(m_vulkanDevice->get().createSemaphore({}));
            m_renderFinishedSemaphores.emplace_back(m_vulkanDevice->get().createSemaphore({}));
            m_inFlightFences.emplace_back(m_vulkanDevice->get().createFence({vk::FenceCreateFlagBits::eSignaled}));
        } catch (const vk::SystemError& e) {
            throw VulkanException(e.code(), std::string("Failed to create sync objects: ") + e.what(),
                                __FUNCTION__, __FILE__, __LINE__);
        }
    }

    LOG_INFO("Sync objects created for " + std::to_string(m_framesInFlight) + " frames");
}

void Application::updateUniformBuffer(size_t currentFrame) {
    glm::mat4 viewProj = m_camera.GetViewProj() * glm::inverse(m_cameraTransform());
    CameraData data{viewProj, glm::inverse(viewProj), glm::vec4(m_cameraTransform.translation, float(m_spheres.size()))};
    memcpy(m_mappedRtCameraUniformData[currentFrame], &data, sizeof(data));
    memcpy(m_mappedLightUniformData[currentFrame], &m_lights, sizeof(LightInfo));
}

void Application::updateSphereBuffer() {
    if (m_spheres.empty()) return;
    m_sphereBuffer->copyFrom(m_spheres.data(), m_spheres.size() * sizeof(SphereData));
    m_spheresDirty = false;
}


void Application::cleanupSwapChain() {
    m_uboDescriptorSets.clear();
    for (auto& model : m_models) {
        model.resetTextureDescriptorSet();
    }
    m_swapChainFramebuffers.clear();
    m_depthImageView = nullptr;
    m_depthImage = nullptr;
    m_depthImageMemory = nullptr;
    m_renderPassManager.reset();
}

void Application::cleanupSyncObjects() {
    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();
    m_imagesInFlight.clear();
}

// ── RT resource cleanup ───────────────────────────────────────────

void Application::cleanupRtResources() {
    cleanupRtSwapChainResources();

    m_rtDescriptorPool = nullptr;
    m_rtDescriptorSetLayout = nullptr;
    m_rtPipelineLayout = nullptr;
    m_rtComputeShader.reset();

    m_sphereBuffer.reset();
    m_rtCameraUniformBuffers.clear();
    m_mappedRtCameraUniformData.clear();

}

// ── RT storage image ──────────────────────────────────────────────

void Application::createRtStorageImage() {
    vk::ImageCreateInfo imageInfo;
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent.width = m_windowWidth;
    imageInfo.extent.height = m_windowHeight;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = vk::Format::eR8G8B8A8Unorm;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.samples = vk::SampleCountFlagBits::e1;

    m_rtOutputImage = m_vulkanDevice->get().createImage(imageInfo);

    auto memReqs = m_rtOutputImage.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = m_vulkanDevice->findMemoryType(
        memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    m_rtOutputImageMemory = m_vulkanDevice->get().allocateMemory(allocInfo);
    m_rtOutputImage.bindMemory(*m_rtOutputImageMemory, 0);

    // Transition to GENERAL layout once (compute writes + fragment reads both use GENERAL)
    {
        vk::CommandBuffer cmd = m_commandBuffers[0];
        cmd.reset();
        vk::CommandBufferBeginInfo beginInfo;
        cmd.begin(beginInfo);

        vk::ImageMemoryBarrier barrier;
        barrier.setOldLayout(vk::ImageLayout::eUndefined);
        barrier.setNewLayout(vk::ImageLayout::eGeneral);
        barrier.setImage(*m_rtOutputImage);
        barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.setSrcAccessMask(vk::AccessFlagBits::eNone);
        barrier.setDstAccessMask(vk::AccessFlagBits::eShaderWrite);

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, nullptr, nullptr, barrier);
        cmd.end();

        vk::SubmitInfo submitInfo;
        submitInfo.setCommandBuffers(cmd);
        m_vulkanDevice->getGraphicsQueue().submit(submitInfo, nullptr);
        m_vulkanDevice->waitIdle();
    }

    vk::ImageViewCreateInfo viewInfo;
    viewInfo.image = *m_rtOutputImage;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = vk::Format::eR8G8B8A8Unorm;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    m_rtOutputImageView = m_vulkanDevice->get().createImageView(viewInfo);

    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    m_rtOutputSampler = m_vulkanDevice->get().createSampler(samplerInfo);

    LOG_INFO("RT storage image created: " + std::to_string(m_windowWidth) + "x" + std::to_string(m_windowHeight));
}

// ── RT descriptor set layout ──────────────────────────────────────

void Application::createRtDescriptorSetLayout() {
    // binding 0: CameraData UBO
    vk::DescriptorSetLayoutBinding cameraBinding;
    cameraBinding.binding = 0;
    cameraBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
    cameraBinding.descriptorCount = 1;
    cameraBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    // binding 1: Spheres SSBO
    vk::DescriptorSetLayoutBinding sphereBinding;
    sphereBinding.binding = 1;
    sphereBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    sphereBinding.descriptorCount = 1;
    sphereBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    // binding 2: Output storage image
    vk::DescriptorSetLayoutBinding imageBinding;
    imageBinding.binding = 2;
    imageBinding.descriptorType = vk::DescriptorType::eStorageImage;
    imageBinding.descriptorCount = 1;
    imageBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;

    std::array<vk::DescriptorSetLayoutBinding, 3> bindings = {cameraBinding, sphereBinding, imageBinding};
    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.setBindings(bindings);
    m_rtDescriptorSetLayout = m_vulkanDevice->get().createDescriptorSetLayout(layoutInfo);

    LOG_INFO("RT descriptor set layout created");
}

void Application::createRtDescriptorPool() {
    std::array<vk::DescriptorPoolSize, 3> poolSizes;
    poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = vk::DescriptorType::eStorageBuffer;
    poolSizes[1].descriptorCount = 1;
    poolSizes[2].type = vk::DescriptorType::eStorageImage;
    poolSizes[2].descriptorCount = 1;

    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = 1;
    poolInfo.setPoolSizes(poolSizes);
    m_rtDescriptorPool = m_vulkanDevice->get().createDescriptorPool(poolInfo);

    LOG_INFO("RT descriptor pool created");
}

void Application::createRtDescriptorSets() {
    std::vector<vk::DescriptorSetLayout> layouts(1, *m_rtDescriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = m_rtDescriptorPool;
    allocInfo.setSetLayouts(layouts);
    std::vector<vk::raii::DescriptorSet> sets = m_vulkanDevice->get().allocateDescriptorSets(allocInfo);
    m_rtDescriptorSet = std::move(sets[0]);

    // Write camera UBO (binding 0)
    {
        vk::DescriptorBufferInfo bufferInfo;
        bufferInfo.buffer = *m_rtCameraUniformBuffers[0]->get();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(CameraData);

        vk::WriteDescriptorSet write;
        write.dstSet = *m_rtDescriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = vk::DescriptorType::eUniformBuffer;
        write.setBufferInfo(bufferInfo);
        m_vulkanDevice->get().updateDescriptorSets(write, nullptr);
    }

    // Write spheres SSBO (binding 1)
    {
        vk::DescriptorBufferInfo bufferInfo;
        bufferInfo.buffer = *m_sphereBuffer->get();
        bufferInfo.offset = 0;
        bufferInfo.range = VK_WHOLE_SIZE;

        vk::WriteDescriptorSet write;
        write.dstSet = *m_rtDescriptorSet;
        write.dstBinding = 1;
        write.dstArrayElement = 0;
        write.descriptorType = vk::DescriptorType::eStorageBuffer;
        write.setBufferInfo(bufferInfo);
        m_vulkanDevice->get().updateDescriptorSets(write, nullptr);
    }

    // Write output storage image (binding 2)
    {
        vk::DescriptorImageInfo imageInfo;
        imageInfo.imageView = *m_rtOutputImageView;
        imageInfo.imageLayout = vk::ImageLayout::eGeneral;

        vk::WriteDescriptorSet write;
        write.dstSet = *m_rtDescriptorSet;
        write.dstBinding = 2;
        write.dstArrayElement = 0;
        write.descriptorType = vk::DescriptorType::eStorageImage;
        write.setImageInfo(imageInfo);
        m_vulkanDevice->get().updateDescriptorSets(write, nullptr);
    }

    LOG_INFO("RT descriptor set created");
}

// ── RT compute pipeline ───────────────────────────────────────────

void Application::createRtComputePipeline() {
    try {
        std::string shaderPath = "shaders/raytracer.spv";
        LOG_INFO("Loading RT compute shader: " + shaderPath);
        m_rtComputeShader = std::make_unique<ShaderModule>(
            ShaderModule::createComputeShader(m_vulkanDevice.get(), shaderPath));

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
        pipelineLayoutInfo.setSetLayouts(*m_rtDescriptorSetLayout);
        m_rtPipelineLayout = m_vulkanDevice->get().createPipelineLayout(pipelineLayoutInfo);

        if (!m_pipelineManager) {
            m_pipelineManager = std::make_unique<PipelineManager>(m_vulkanDevice->get());
        }

        ComputePipelineConfig config;
        config.shaderModule = *m_rtComputeShader->get();
        config.entryPoint = "computeMain";
        config.layout = *m_rtPipelineLayout;

        m_pipelineManager->createComputePipeline("rt_main", config);
        LOG_INFO("RT compute pipeline created");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create RT compute pipeline: " + std::string(e.what()));
        throw;
    }
}

// ── RT camera uniform buffers ─────────────────────────────────────

void Application::createRtCameraUniformBuffers() {
    createUniformBuffersImpl(sizeof(CameraData), m_rtCameraUniformBuffers, m_mappedRtCameraUniformData);
    LOG_INFO("RT camera uniform buffers created");
}

// ── Sphere buffer (SSBO) ──────────────────────────────────────────

void Application::createSphereBuffer() {
    if (m_spheres.empty()) {
        m_spheres.push_back({}); // ensure at least one entry for buffer creation
    }
    // Host-visible for easy per-frame updates
    m_sphereBuffer = std::make_unique<Buffer>(
        Buffer::createBuffer(m_vulkanDevice.get(),
            m_spheres.size() * sizeof(SphereData),
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
    m_sphereBuffer->copyFrom(m_spheres.data(), m_spheres.size() * sizeof(SphereData));
    m_spheresDirty = false;
    LOG_INFO("Sphere buffer created with " + std::to_string(m_spheres.size()) + " spheres");
}

// ── Fullscreen display pipeline ───────────────────────────────────

void Application::createFullscreenPipeline() {
    try {
        std::string shaderPath = "shaders/fullscreen.spv";
        LOG_INFO("Loading fullscreen shader: " + shaderPath);

        m_fullscreenVertShader = std::make_unique<ShaderModule>(
            ShaderModule::createVertexShader(m_vulkanDevice.get(), shaderPath));
        m_fullscreenFragShader = std::make_unique<ShaderModule>(
            ShaderModule::createFragmentShader(m_vulkanDevice.get(), shaderPath));

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
        pipelineLayoutInfo.setSetLayouts(*m_fullscreenDescriptorSetLayout);
        m_fullscreenPipelineLayout = m_vulkanDevice->get().createPipelineLayout(pipelineLayoutInfo);

        if (!m_pipelineManager) {
            m_pipelineManager = std::make_unique<PipelineManager>(m_vulkanDevice->get());
        }

        PipelineConfig config;
        config.vertexShader = *m_fullscreenVertShader->get();
        config.fragmentShader = *m_fullscreenFragShader->get();
        config.vertexEntryPoint = "vertMain";
        config.fragmentEntryPoint = "fragMain";
        config.pipelineLayout = *m_fullscreenPipelineLayout;
        config.renderPass = *m_renderPassManager->get();
        config.topology = vk::PrimitiveTopology::eTriangleList;
        config.depthTestEnable = false;
        config.depthWriteEnable = false;
        config.blendEnable = false;

        // No vertex buffer — shader generates vertices from SV_VertexID
        config.vertexBindingDescription = vk::VertexInputBindingDescription{};
        config.vertexBindingDescriptions.clear();
        config.vertexAttributeDescriptions.clear();

        m_pipelineManager->createPipeline("fullscreen", config);
        LOG_INFO("Fullscreen pipeline created");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create fullscreen pipeline: " + std::string(e.what()));
        throw;
    }
}

void Application::createFullscreenDescriptorSet() {
    // Layout for binding 0: combined image sampler of the RT output
    vk::DescriptorSetLayoutBinding samplerBinding;
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;

    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.setBindings(samplerBinding);
    m_fullscreenDescriptorSetLayout = m_vulkanDevice->get().createDescriptorSetLayout(layoutInfo);

    // Allocate descriptor set from the main descriptor pool (which supports combined image sampler)
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.setSetLayouts(*m_fullscreenDescriptorSetLayout);
    std::vector<vk::raii::DescriptorSet> sets = m_vulkanDevice->get().allocateDescriptorSets(allocInfo);
    m_fullscreenDescriptorSet = std::move(sets[0]);

    vk::DescriptorImageInfo imageInfo;
    imageInfo.imageView = *m_rtOutputImageView;
    imageInfo.sampler = *m_rtOutputSampler;
    imageInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::WriteDescriptorSet write;
    write.dstSet = *m_fullscreenDescriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.setImageInfo(imageInfo);
    m_vulkanDevice->get().updateDescriptorSets(write, nullptr);

    LOG_INFO("Fullscreen descriptor set created");
}

// ── Default scene spheres ─────────────────────────────────────────

void Application::initSpheres() {
    m_spheres.clear();

    // Ground-like large sphere
    m_spheres.push_back({
        glm::vec3(0.0f, -100.5f, 0.0f),  // center
        100.0f,                            // radius
        glm::vec4(0.3f, 0.5f, 0.3f, 1.0f), // color (greenish)
        0.0f, 0.0f, 1.0f, 0.0f           // refl, refr, ior, pad
    });

    // Center sphere
    m_spheres.push_back({
        glm::vec3(0.0f, 0.0f, 0.0f),
        1.0f,
        glm::vec4(0.9f, 0.3f, 0.2f, 1.0f), // red
        0.0f, 0.0f, 1.5f, 0.0f
    });

    // Left sphere
    m_spheres.push_back({
        glm::vec3(-2.5f, 0.0f, 0.5f),
        1.0f,
        glm::vec4(0.2f, 0.4f, 0.9f, 1.0f), // blue
        0.0f, 0.0f, 1.5f, 0.0f
    });

    // Right sphere
    m_spheres.push_back({
        glm::vec3(2.5f, 0.0f, 0.5f),
        1.0f,
        glm::vec4(0.9f, 0.8f, 0.1f, 1.0f), // yellow
        0.0f, 0.0f, 1.5f, 0.0f
    });

    // Small sphere on top
    m_spheres.push_back({
        glm::vec3(0.0f, 2.0f, 1.0f),
        0.6f,
        glm::vec4(0.8f, 0.2f, 0.7f, 1.0f), // pink
        0.0f, 0.0f, 1.5f, 0.0f
    });

    m_spheresDirty = true;
    LOG_INFO("Default scene initialized with " + std::to_string(m_spheres.size()) + " spheres");
}

void Application::cleanupRtSwapChainResources() {
    m_fullscreenDescriptorSet = nullptr;
    m_fullscreenDescriptorSetLayout = nullptr;
    m_fullscreenPipelineLayout = nullptr;
    m_fullscreenVertShader.reset();
    m_fullscreenFragShader.reset();

    m_rtDescriptorSet = nullptr;

    m_rtOutputSampler = nullptr;
    m_rtOutputImageView = nullptr;
    m_rtOutputImage = nullptr;
    m_rtOutputImageMemory = nullptr;
}

void Application::recreateSwapChain() {
    if (m_windowWidth == 0 || m_windowHeight == 0) {
        return;
    }

    m_vulkanDevice->waitIdle();

    cleanupSwapChain();
    cleanupRtSwapChainResources();

    m_swapChainManager->recreate(m_windowWidth, m_windowHeight);
    createRenderPass();
    createDescriptorSetLayout();
    createGraphicsPipeline();
    createDepthResources();
    createFramebuffers();
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();

    // Recreate RT resources (size-dependent)
    createRtStorageImage();
    createRtDescriptorSets();
    createFullscreenDescriptorSet();  // creates layout + set (must be before pipeline)
    createFullscreenPipeline();

    m_camera.SetAspectRatio(static_cast<float>(m_windowWidth) / m_windowHeight);

    m_framebufferResized = false;
}

void Application::drawFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Transform");

    ImGui::SetWindowFontScale(1.4f);

    ImGui::Text("Camera");
    ImGui::Separator();
    ImGui::DragFloat3("Translation", glm::value_ptr(m_cameraTransform.translation), 0.01f);
    ImGui::DragFloat3("Rotation", glm::value_ptr(m_cameraTransform.rotation), 0.01f);
    float fov = glm::degrees(m_camera.GetPerspectiveVerticalFOV());
    if (ImGui::DragFloat("FOV", &fov, 0.01f)) {
        m_camera.SetPerspectiveVerticalFOV(glm::radians(fov));
    }

    ImGui::Spacing();
    ImGui::Text("Instances");
    ImGui::Separator();
    for (size_t i = 0; i < m_instances.size(); i++) {
        auto& instance = m_instances.at(i);
        auto name = instance.name + std::to_string(i);
        ImGui::PushID(name.c_str());
        if (ImGui::CollapsingHeader(name.c_str())) {
            ImGui::DragFloat3("Translation", glm::value_ptr(instance.transform.translation), 0.01f);
            ImGui::DragFloat3("Rotation", glm::value_ptr(instance.transform.rotation), 0.01f);
            float scale = instance.transform.scale.x;
            ImGui::DragFloat("Scale", &scale, 0.01f);
            instance.transform.scale = glm::vec3(scale);
            ImGui::ColorEdit4("Color", glm::value_ptr(instance.color));
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Light");
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Point Light 1")) {
        ImGui::DragFloat3("Position##pl1", glm::value_ptr(m_lights.pointLight1.pos), 0.1f);
        ImGui::ColorEdit3("Color##pl1", glm::value_ptr(m_lights.pointLight1.color));
        ImGui::DragFloat("Intensity##pl1", &m_lights.pointLight1.intensity, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Max Distance##pl1", &m_lights.pointLight1.maxDistance, 0.1f, 0.0f, 100.0f);
    }

    if (ImGui::CollapsingHeader("Point Light 2")) {
        ImGui::DragFloat3("Position##pl2", glm::value_ptr(m_lights.pointLight2.pos), 0.1f);
        ImGui::ColorEdit3("Color##pl2", glm::value_ptr(m_lights.pointLight2.color));
        ImGui::DragFloat("Intensity##pl2", &m_lights.pointLight2.intensity, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Max Distance##pl2", &m_lights.pointLight2.maxDistance, 0.1f, 0.0f, 100.0f);
    }

    if (ImGui::CollapsingHeader("Spot Light")) {
        ImGui::DragFloat3("Position##sl", glm::value_ptr(m_lights.spotLight.pos), 0.1f);
        ImGui::DragFloat3("Direction##sl", glm::value_ptr(m_lights.spotLight.dir), 0.01f);
        m_lights.spotLight.dir = glm::normalize(m_lights.spotLight.dir);
        ImGui::ColorEdit3("Color##sl", glm::value_ptr(m_lights.spotLight.color));
        float innerDeg = glm::degrees(acos(m_lights.spotLight.cosineInclinationAngle));
        if (ImGui::DragFloat("Inner Angle##sl", &innerDeg, 0.1f, 0.1f, 90.0f))
            m_lights.spotLight.cosineInclinationAngle = cos(glm::radians(innerDeg));
        float outerDeg = glm::degrees(acos(m_lights.spotLight.cosineExclusivityAngle));
        if (ImGui::DragFloat("Outer Angle##sl", &outerDeg, 0.1f, 0.1f, 90.0f))
            m_lights.spotLight.cosineExclusivityAngle = cos(glm::radians(outerDeg));
        ImGui::DragFloat("Intensity##sl", &m_lights.spotLight.intensity, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Max Distance##sl", &m_lights.spotLight.maxDistance, 0.1f, 0.0f, 100.0f);
    }

    if (ImGui::CollapsingHeader("Directional Light")) {
        ImGui::DragFloat3("Direction##dl", glm::value_ptr(m_lights.directionalLight.dir), 0.01f);
        m_lights.directionalLight.dir = glm::normalize(m_lights.directionalLight.dir);
        ImGui::ColorEdit3("Color##dl", glm::value_ptr(m_lights.directionalLight.color));
        ImGui::DragFloat("Intensity##dl", &m_lights.directionalLight.intensity, 0.01f, 0.0f, 10.0f);
    }

    if (ImGui::CollapsingHeader("Ambient")) {
        ImGui::ColorEdit3("Color##amb", glm::value_ptr(m_lights.ambientArgs));
        ImGui::DragFloat("Strength##amb", &m_lights.ambientArgs.w, 0.01f, 0.0f, 1.0f);
    }

    ImGui::DragFloat("Diffuse Strength", &m_lights.diffuseStrength, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Specular Strength", &m_lights.specularStrength, 0.01f, 0.0f, 2.0f);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("RT Spheres");
    ImGui::Separator();

    for (int i = 0; i < static_cast<int>(m_spheres.size()); i++) {
        ImGui::PushID(i);
        std::string header = "Sphere " + std::to_string(i);
        if (ImGui::CollapsingHeader(header.c_str())) {
            bool changed = false;
            changed |= ImGui::DragFloat3("Center", glm::value_ptr(m_spheres[i].center), 0.05f);
            changed |= ImGui::DragFloat("Radius", &m_spheres[i].radius, 0.05f, 0.01f, 100.0f);
            changed |= ImGui::ColorEdit4("Color", glm::value_ptr(m_spheres[i].color));
            changed |= ImGui::DragFloat("Reflectivity", &m_spheres[i].reflectivity, 0.01f, 0.0f, 1.0f);
            changed |= ImGui::DragFloat("Refractivity", &m_spheres[i].refractivity, 0.01f, 0.0f, 1.0f);
            changed |= ImGui::DragFloat("IOR", &m_spheres[i].indexOfRefraction, 0.01f, 0.1f, 5.0f);
            if (changed) m_spheresDirty = true;
        }
        ImGui::PopID();
    }

    ImGui::End();

    ImGui::Render();

    try {
        (void)m_vulkanDevice->get().waitForFences(*m_inFlightFences[m_currentFrame], true, UINT64_MAX);
    } catch (const vk::SystemError& e) {
        throw VulkanException(static_cast<vk::Result>(e.code().value()),
                            std::string("Failed to wait for fence: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    uint32_t imageIndex = m_swapChainManager->acquireNextImage(*m_imageAvailableSemaphores[m_currentFrame]);

    if (imageIndex == UINT32_MAX) {
        recreateSwapChain();
        return;
    }

    if (m_imagesInFlight[imageIndex]) {
        try {
            (void)m_vulkanDevice->get().waitForFences(m_imagesInFlight[imageIndex], true, UINT64_MAX);
        } catch (const vk::SystemError& e) {
            throw VulkanException(static_cast<vk::Result>(e.code().value()),
                                std::string("Failed to wait for image fence: ") + e.what(),
                                __FUNCTION__, __FILE__, __LINE__);
        }
    }
    m_imagesInFlight[imageIndex] = *m_inFlightFences[m_currentFrame];

    updateSphereBuffer();

    m_commandBuffers[m_currentFrame].reset();

    vk::CommandBufferBeginInfo beginInfo;
    m_commandBuffers[m_currentFrame].begin(beginInfo);

    // ── Compute dispatch ──────────────────────────────────────
    m_commandBuffers[m_currentFrame].bindPipeline(vk::PipelineBindPoint::eCompute,
        *m_pipelineManager->getPipeline("rt_main"));
    m_commandBuffers[m_currentFrame].bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        *m_rtPipelineLayout,
        0,
        *m_rtDescriptorSet,
        nullptr);

    uint32_t groupX = (m_windowWidth  + 7) / 8;
    uint32_t groupY = (m_windowHeight + 7) / 8;
    m_commandBuffers[m_currentFrame].dispatch(groupX, groupY, 1);

    // Barrier: compute write → fragment read
    {
        vk::ImageMemoryBarrier barrier;
        barrier.setOldLayout(vk::ImageLayout::eGeneral);
        barrier.setNewLayout(vk::ImageLayout::eGeneral);
        barrier.setImage(*m_rtOutputImage);
        barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite);
        barrier.setDstAccessMask(vk::AccessFlagBits::eShaderRead);

        m_commandBuffers[m_currentFrame].pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eFragmentShader,
            {}, nullptr, nullptr, barrier);
    }

    // ── Fullscreen display ────────────────────────────────────

    vk::Viewport viewport;
    viewport.setX(0.0f);
    viewport.setY(0.0f);
    viewport.setWidth(static_cast<float>(m_swapChainManager->getExtent().width));
    viewport.setHeight(static_cast<float>(m_swapChainManager->getExtent().height));
    viewport.setMinDepth(0.0f);
    viewport.setMaxDepth(1.0f);
    m_commandBuffers[m_currentFrame].setViewport(0, viewport);

    vk::Rect2D scissor;
    scissor.setOffset({0, 0});
    scissor.setExtent(m_swapChainManager->getExtent());
    m_commandBuffers[m_currentFrame].setScissor(0, scissor);

    std::array<vk::ClearValue, 2> clearValues;
    clearValues[0].color = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f};
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    vk::RenderPassBeginInfo renderPassInfo;
    renderPassInfo.setRenderPass(*m_renderPassManager->get());
    renderPassInfo.setFramebuffer(*m_swapChainFramebuffers[imageIndex]);
    renderPassInfo.setRenderArea(vk::Rect2D{{0, 0}, m_swapChainManager->getExtent()});
    renderPassInfo.setClearValues(clearValues);

    m_commandBuffers[m_currentFrame].beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    m_commandBuffers[m_currentFrame].bindPipeline(vk::PipelineBindPoint::eGraphics,
        *m_pipelineManager->getPipeline("fullscreen"));
    m_commandBuffers[m_currentFrame].bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *m_fullscreenPipelineLayout,
        0,
        *m_fullscreenDescriptorSet,
        nullptr);

    // Fullscreen triangle — 3 vertices, no vertex/index buffers
    m_commandBuffers[m_currentFrame].draw(3, 1, 0, 0);

    ImGui_ImplVulkan_RenderDrawData(
        ImGui::GetDrawData(),
        *m_commandBuffers[m_currentFrame]
    );

    m_commandBuffers[m_currentFrame].endRenderPass();

    m_commandBuffers[m_currentFrame].end();

    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::SubmitInfo submitInfo;
    submitInfo.setWaitSemaphores(*m_imageAvailableSemaphores[m_currentFrame]);
    submitInfo.setWaitDstStageMask(waitStages);
    submitInfo.setCommandBuffers(*m_commandBuffers[m_currentFrame]);
    submitInfo.setSignalSemaphores(*m_renderFinishedSemaphores[m_currentFrame]);

    try {
        m_vulkanDevice->get().resetFences(*m_inFlightFences[m_currentFrame]);
        m_vulkanDevice->getGraphicsQueue().submit(submitInfo, *m_inFlightFences[m_currentFrame]);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to submit draw command buffer: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    m_swapChainManager->presentImage(imageIndex, *m_renderFinishedSemaphores[m_currentFrame]);

    m_currentFrame = (m_currentFrame + 1) % m_framesInFlight;
}

void Application::mainLoop() {
    LOG_INFO("Entering main loop...");

    while (!m_windowManager->shouldClose()) {
        glfwPollEvents();

        drawFrame();

        // ReSharper disable once CppDFAConstantConditions
        if (m_framebufferResized) {
            // ReSharper disable once CppDFAUnreachableCode
            recreateSwapChain();
        }
    }

    m_vulkanDevice->waitIdle();
    LOG_INFO("Main loop exited");
}

vk::Format Application::findDepthFormat(const std::vector<vk::Format>& candidates) const {
    for (const vk::Format format : candidates) {
        const auto props = m_vulkanDevice->getPhysical().getFormatProperties(format);
        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
            return format;
        }
    }
    throw VulkanException(vk::Result::eErrorFormatNotSupported,
                        "Failed to find supported depth format",
                        __FUNCTION__, __FILE__, __LINE__);
}

}
