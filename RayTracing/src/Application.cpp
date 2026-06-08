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
      , m_framebufferResized(false)
      , m_fpsLastTime(std::chrono::steady_clock::now()) {
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
    createDescriptorSetLayouts();
    createGraphicsPipeline();
    createDepthResources();
    createFramebuffers();
    createCommandPool();
#if 0
    createModels();
#endif
    createUniformBuffers();
    createLightBuffer();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();
    createSyncObjects();

    createRtStorageImage();
    createRtComputePipeline();
    initSpheres();
    createSphereBuffer();
    createRtResourceDescriptorSet();
    createFullscreenDescriptorSet();
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

    m_pointLights.push_back({{4.0f, 4.0f, 4.0f}, 20.0f, {1.0f, 1.0f, 1.0f}, 30.0f});
    m_pointLights.push_back({{-4.0f, 4.0f, 4.0f}, 20.0f, {1.0f, 1.0f, 1.0f}, 30.0f});
    setLightsDirty();
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

    m_perFrameDescriptorSets.clear();
    m_descriptorPool = nullptr;
    m_perFrameSetLayout = nullptr;
    m_samplerSetLayout = nullptr;
    m_rtResourceSetLayout = nullptr;
    m_imguiPool = nullptr;
    m_blinnPhongPipelineLayout = nullptr;
    m_rtPipelineLayout = nullptr;
    m_fullscreenPipelineLayout = nullptr;
    m_fragmentShader.reset();
    m_vertexShader.reset();
    m_rtComputeShader.reset();
    m_fullscreenVertShader.reset();
    m_fullscreenFragShader.reset();
    m_cameraUniformBuffers.clear();
    m_mappedCameraUniformData.clear();
    m_lightBuffers.clear();
    m_commandBuffers.clear();
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

void Application::createDescriptorSetLayouts() {
    LOG_INFO("Creating descriptor set layouts...");

    // Per-frame: b0 = camera UBO, b1 = light SSBO
    {
        std::array<vk::DescriptorSetLayoutBinding, 2> bindings;
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eCompute;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo;
        layoutInfo.setBindings(bindings);
        m_perFrameSetLayout = m_vulkanDevice->get().createDescriptorSetLayout(layoutInfo);
    }

    // Sampler: b0 = combined image sampler (shared by material textures and fullscreen display)
    {
        vk::DescriptorSetLayoutBinding binding;
        binding.binding = 0;
        binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo;
        layoutInfo.setBindings(binding);
        m_samplerSetLayout = m_vulkanDevice->get().createDescriptorSetLayout(layoutInfo);
    }

    // RT resource: b0 = spheres SSBO, b1 = output storage image
    {
        std::array<vk::DescriptorSetLayoutBinding, 2> bindings;
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageImage;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo;
        layoutInfo.setBindings(bindings);
        m_rtResourceSetLayout = m_vulkanDevice->get().createDescriptorSetLayout(layoutInfo);
    }

    LOG_INFO("Descriptor set layouts created");
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

        vk::PushConstantRange pcRange;
        pcRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
        pcRange.size = sizeof(RTGlobalConstants);

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {*m_perFrameSetLayout, *m_samplerSetLayout};
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setSetLayouts(setLayouts);
        pipelineLayoutInfo.setPushConstantRanges(pcRange);

        m_blinnPhongPipelineLayout = m_vulkanDevice->get().createPipelineLayout(pipelineLayoutInfo);
        LOG_INFO("Pipeline layout created");

        if (!m_pipelineManager) {
            m_pipelineManager = std::make_unique<PipelineManager>(m_vulkanDevice->get());
        }

        PipelineConfig pipelineConfig;
        pipelineConfig.vertexShader = *m_vertexShader->get();
        pipelineConfig.fragmentShader = *m_fragmentShader->get();
        pipelineConfig.pipelineLayout = *m_blinnPhongPipelineLayout;
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
    createUniformBuffersImpl(sizeof(CameraData), m_cameraUniformBuffers, m_mappedCameraUniformData);
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

void Application::createLightBuffer() {
    constexpr size_t kMaxLights = 16;
    if (m_pointLights.empty()) {
        m_pointLights.push_back({});
    }
    for (size_t i = 0; i < m_framesInFlight; i++) {
        auto buf = std::make_unique<Buffer>(
            Buffer::createBuffer(m_vulkanDevice.get(),
                kMaxLights * sizeof(PointLightData),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
        buf->copyFrom(m_pointLights.data(), m_pointLights.size() * sizeof(PointLightData));
        m_lightBuffers.emplace_back(std::move(buf));
    }
    m_lightsDirty = 0;
    LOG_INFO("Light SSBO created (max " + std::to_string(kMaxLights) + " lights)");
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

    uint32_t fif = static_cast<uint32_t>(m_framesInFlight);

    std::array<vk::DescriptorPoolSize, 4> poolSizes;
    poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
    poolSizes[0].descriptorCount = fif; // camera UBO per frame
    poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
    poolSizes[1].descriptorCount = modelCount + fif; // material textures + fullscreen per frame
    poolSizes[2].type = vk::DescriptorType::eStorageBuffer;
    poolSizes[2].descriptorCount = fif * 2; // spheres + lights SSBO per frame
    poolSizes[3].type = vk::DescriptorType::eStorageImage;
    poolSizes[3].descriptorCount = fif; // RT output per frame

    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = fif * 3 + modelCount; // per-frame + RT resource + fullscreen + materials
    poolInfo.setPoolSizes(poolSizes);
    m_descriptorPool = m_vulkanDevice->get().createDescriptorPool(poolInfo);

    LOG_INFO("Descriptor pool created");
}

void Application::createDescriptorSets() {
    LOG_INFO("Creating descriptor sets...");

    // Per-frame sets: camera UBO (b0) + light UBO (b1)
    std::vector<vk::DescriptorSetLayout> layouts(m_framesInFlight, *m_perFrameSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = *m_descriptorPool;
    allocInfo.setSetLayouts(layouts);
    m_perFrameDescriptorSets = m_vulkanDevice->get().allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < m_framesInFlight; ++i) {
        vk::DescriptorBufferInfo cameraBufferInfo;
        cameraBufferInfo.buffer = *m_cameraUniformBuffers[i]->get();
        cameraBufferInfo.offset = 0;
        cameraBufferInfo.range = sizeof(CameraData);

        vk::DescriptorBufferInfo lightBufferInfo;
        lightBufferInfo.buffer = *m_lightBuffers[i]->get();
        lightBufferInfo.offset = 0;
        lightBufferInfo.range = VK_WHOLE_SIZE;

        std::array<vk::WriteDescriptorSet, 2> writes;
        writes[0].dstSet = *m_perFrameDescriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[0].setBufferInfo(cameraBufferInfo);

        writes[1].dstSet = *m_perFrameDescriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].setBufferInfo(lightBufferInfo);

        m_vulkanDevice->get().updateDescriptorSets(writes, nullptr);
    }

    for (auto& model : m_models) {
        model.createTextureDescriptorSet(m_vulkanDevice->get(), m_descriptorPool, m_samplerSetLayout);
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
    CameraData data{viewProj, glm::inverse(viewProj), glm::vec4(m_cameraTransform.translation, 0.0f)};
    memcpy(m_mappedCameraUniformData[currentFrame], &data, sizeof(data));
}

void Application::updateLightBuffer(size_t currentFrame) {
    if (!isLightsDirty() || m_pointLights.empty()) return;
    m_lightBuffers[currentFrame]->copyFrom(m_pointLights.data(),
        m_pointLights.size() * sizeof(PointLightData));
}

void Application::updateSphereBuffer(size_t currentFrame) {
    if (!isSpheresDirty() || m_spheres.empty()) return;
    m_sphereBuffers[currentFrame]->copyFrom(m_spheres.data(), m_spheres.size() * sizeof(SphereData));
}


void Application::cleanupSwapChain() {
    m_perFrameDescriptorSets.clear();
    for (auto& model : m_models) {
        model.resetTextureDescriptorSet();
    }
    m_swapChainFramebuffers.clear();
    m_depthImageView = nullptr;
    m_depthImage = nullptr;
    m_depthImageMemory = nullptr;
    m_blinnPhongPipelineLayout = nullptr;
    m_pipelineManager.reset();
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
    m_sphereBuffers.clear();
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

    for (size_t i = 0; i < m_framesInFlight; i++) {
        m_rtOutputImages.emplace_back(m_vulkanDevice->get().createImage(imageInfo));

        auto memReqs = m_rtOutputImages[i].getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = m_vulkanDevice->findMemoryType(
            memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        m_rtOutputImagesMemory.emplace_back(m_vulkanDevice->get().allocateMemory(allocInfo));
        m_rtOutputImages[i].bindMemory(*m_rtOutputImagesMemory[i], 0);

        // Transition to GENERAL layout once (compute writes + fragment reads both use GENERAL)
        {
            vk::CommandBuffer cmd = m_commandBuffers[0];
            cmd.reset();
            vk::CommandBufferBeginInfo beginInfo;
            cmd.begin(beginInfo);

            vk::ImageMemoryBarrier barrier;
            barrier.setOldLayout(vk::ImageLayout::eUndefined);
            barrier.setNewLayout(vk::ImageLayout::eGeneral);
            barrier.setImage(*m_rtOutputImages[i]);
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
        viewInfo.image = *m_rtOutputImages[i];
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = vk::Format::eR8G8B8A8Unorm;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        m_rtOutputImageViews.emplace_back(m_vulkanDevice->get().createImageView(viewInfo));
    }

    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
    m_rtOutputSampler = m_vulkanDevice->get().createSampler(samplerInfo);

    LOG_INFO("RT storage image created: " + std::to_string(m_windowWidth) + "x" + std::to_string(m_windowHeight));
}

// ── RT resource descriptor set ───────────────────────────────────

void Application::createRtResourceDescriptorSet() {
    std::vector<vk::DescriptorSetLayout> layouts(m_framesInFlight, *m_rtResourceSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = *m_descriptorPool;
    allocInfo.setSetLayouts(layouts);
    m_rtDescriptorSets = m_vulkanDevice->get().allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < m_framesInFlight; ++i) {
        // b0: sphere SSBO
        {
            vk::DescriptorBufferInfo bufferInfo;
            bufferInfo.buffer = *m_sphereBuffers[i]->get();
            bufferInfo.offset = 0;
            bufferInfo.range = VK_WHOLE_SIZE;

            vk::WriteDescriptorSet write;
            write.dstSet = *m_rtDescriptorSets[i];
            write.dstBinding = 0;
            write.dstArrayElement = 0;
            write.descriptorType = vk::DescriptorType::eStorageBuffer;
            write.setBufferInfo(bufferInfo);
            m_vulkanDevice->get().updateDescriptorSets(write, nullptr);
        }

        // b1: output storage image
        {
            vk::DescriptorImageInfo imageInfo;
            imageInfo.imageView = *m_rtOutputImageViews[i];
            imageInfo.imageLayout = vk::ImageLayout::eGeneral;

            vk::WriteDescriptorSet write;
            write.dstSet = *m_rtDescriptorSets[i];
            write.dstBinding = 1;
            write.dstArrayElement = 0;
            write.descriptorType = vk::DescriptorType::eStorageImage;
            write.setImageInfo(imageInfo);
            m_vulkanDevice->get().updateDescriptorSets(write, nullptr);
        }
    }

    LOG_INFO("RT resource descriptor set created");
}

// ── RT compute pipeline ───────────────────────────────────────────

void Application::createRtComputePipeline() {
    try {
        std::string shaderPath = "shaders/raytracer.spv";
        LOG_INFO("Loading RT compute shader: " + shaderPath);
        m_rtComputeShader = std::make_unique<ShaderModule>(
            ShaderModule::createComputeShader(m_vulkanDevice.get(), shaderPath));

        vk::PushConstantRange pcRange;
        pcRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pcRange.size = sizeof(RTGlobalConstants);

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {*m_perFrameSetLayout, *m_rtResourceSetLayout};
        vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
        pipelineLayoutInfo.setSetLayouts(setLayouts);
        pipelineLayoutInfo.setPushConstantRanges(pcRange);
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

// ── Sphere buffer (SSBO) ──────────────────────────────────────────

void Application::createSphereBuffer() {
    if (m_spheres.empty()) {
        m_spheres.push_back({}); // ensure at least one entry for buffer creation
    }
    for (size_t i = 0; i < m_framesInFlight; i++) {
        // Host-visible for easy per-frame updates
        auto sphereBuffer = std::make_unique<Buffer>(
            Buffer::createBuffer(m_vulkanDevice.get(),
                m_spheres.size() * sizeof(SphereData),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
        sphereBuffer->copyFrom(m_spheres.data(), m_spheres.size() * sizeof(SphereData));
        m_sphereBuffers.emplace_back(std::move(sphereBuffer));
    }
    m_spheresDirty = 0;
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
        pipelineLayoutInfo.setSetLayouts(*m_samplerSetLayout);
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
    std::vector<vk::DescriptorSetLayout> layouts(m_framesInFlight, *m_samplerSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = *m_descriptorPool;
    allocInfo.setSetLayouts(layouts);
    m_fullscreenDescriptorSets = m_vulkanDevice->get().allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < m_framesInFlight; i++) {
        vk::DescriptorImageInfo imageInfo;
        imageInfo.imageView = *m_rtOutputImageViews[i];
        imageInfo.sampler = *m_rtOutputSampler;
        imageInfo.imageLayout = vk::ImageLayout::eGeneral;

        vk::WriteDescriptorSet write;
        write.dstSet = *m_fullscreenDescriptorSets[i];
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.setImageInfo(imageInfo);
        m_vulkanDevice->get().updateDescriptorSets(write, nullptr);
    }

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
        0.0f, 1.0f, 0.0f           // refl, ior, pad
    });

    // Center sphere
    m_spheres.push_back({
        glm::vec3(0.0f, 0.0f, 0.0f),
        1.0f,
        glm::vec4(0.9f, 0.3f, 0.2f, 1.0f), // red
        0.0f, 1.5f, 0.0f
    });

    // Left sphere
    m_spheres.push_back({
        glm::vec3(-2.5f, 0.0f, 0.5f),
        1.0f,
        glm::vec4(0.2f, 0.4f, 0.9f, 1.0f), // blue
        0.0f, 1.5f, 0.0f
    });

    // Right sphere
    m_spheres.push_back({
        glm::vec3(2.5f, 0.0f, 0.5f),
        1.0f,
        glm::vec4(0.9f, 0.8f, 0.1f, 1.0f), // yellow
        0.0f, 1.5f, 0.0f
    });

    // Small sphere on top
    m_spheres.push_back({
        glm::vec3(0.0f, 2.0f, 1.0f),
        0.6f,
        glm::vec4(0.8f, 0.2f, 0.7f, 1.0f), // pink
        0.0f, 1.5f, 0.0f
    });

    setSpheresDirty();
    LOG_INFO("Default scene initialized with " + std::to_string(m_spheres.size()) + " spheres");
}

void Application::cleanupRtSwapChainResources() {
    m_fullscreenDescriptorSets.clear();
    m_fullscreenPipelineLayout = nullptr;
    m_rtDescriptorSets.clear();
    m_rtPipelineLayout = nullptr;
    m_rtOutputSampler = nullptr;
    m_rtOutputImageViews.clear();
    m_rtOutputImages.clear();
    m_rtOutputImagesMemory.clear();
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
    createDescriptorSetLayouts();
    createGraphicsPipeline();
    createDepthResources();
    createFramebuffers();
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();

    // Recreate RT resources (size-dependent)
    createRtStorageImage();
    createRtComputePipeline();
    createRtResourceDescriptorSet();
    createFullscreenDescriptorSet();
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

    ImGui::Text("FPS: %.1f", m_currentFps);
    ImGui::Spacing();

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
    ImGui::Text("Point Lights");
    ImGui::Separator();

    for (int i = 0; i < static_cast<int>(m_pointLights.size()); i++) {
        ImGui::PushID(i);
        std::string header = "Light " + std::to_string(i);
        if (ImGui::CollapsingHeader(header.c_str())) {
            bool changed = false;
            changed |= ImGui::DragFloat3("Position", glm::value_ptr(m_pointLights[i].position), 0.1f);
            changed |= ImGui::ColorEdit3("Color", glm::value_ptr(m_pointLights[i].color));
            changed |= ImGui::DragFloat("Intensity", &m_pointLights[i].intensity, 0.1f, 0.0f, 100.0f);
            changed |= ImGui::DragFloat("Max Distance", &m_pointLights[i].maxDistance, 0.1f, 0.0f, 100.0f);
            if (changed) {
                setLightsDirty();
            }
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Add Light")) {
        m_pointLights.push_back({{0.0f, 3.0f, 0.0f}, 10.0f, {1.0f, 1.0f, 1.0f}, 20.0f});
        setLightsDirty();
    }
    if (m_pointLights.size() > 1) {
        ImGui::SameLine();
        if (ImGui::Button("Remove Light")) {
            m_pointLights.pop_back();
            setLightsDirty();
        }
    }

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
            changed |= ImGui::DragFloat("IOR", &m_spheres[i].indexOfRefraction, 0.01f, 0.1f, 5.0f);
            if (changed) {
                setSpheresDirty();
            }
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

    updateUniformBuffer(m_currentFrame);
    updateLightBuffer(m_currentFrame);
    updateSphereBuffer(m_currentFrame);

    m_commandBuffers[m_currentFrame].reset();

    vk::CommandBufferBeginInfo beginInfo;
    m_commandBuffers[m_currentFrame].begin(beginInfo);

    // ── Compute dispatch ──────────────────────────────────────
    m_commandBuffers[m_currentFrame].bindPipeline(vk::PipelineBindPoint::eCompute,
        *m_pipelineManager->getPipeline("rt_main"));
    std::array<vk::DescriptorSet, 2> rtSets = {
        *m_perFrameDescriptorSets[m_currentFrame],
        *m_rtDescriptorSets[m_currentFrame]
    };
    m_commandBuffers[m_currentFrame].bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        *m_rtPipelineLayout,
        0,
        rtSets,
        nullptr);

    RTGlobalConstants rtPC{};
    rtPC.sphereCount = static_cast<uint32_t>(m_spheres.size());
    rtPC.lightCount = static_cast<uint32_t>(m_pointLights.size());
    rtPC.ambientStrength = 0.1f;
    rtPC.diffuseStrength = 0.5f;
    rtPC.specularStrength = 1.0f;
    m_commandBuffers[m_currentFrame].pushConstants<RTGlobalConstants>(
        *m_rtPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, rtPC);

    uint32_t groupX = (m_windowWidth  + 7) / 8;
    uint32_t groupY = (m_windowHeight + 7) / 8;
    m_commandBuffers[m_currentFrame].dispatch(groupX, groupY, 1);

    // Barrier: compute write → fragment read
    {
        vk::ImageMemoryBarrier barrier;
        barrier.setOldLayout(vk::ImageLayout::eGeneral);
        barrier.setNewLayout(vk::ImageLayout::eGeneral);
        barrier.setImage(*m_rtOutputImages[m_currentFrame]);
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
        *m_fullscreenDescriptorSets[m_currentFrame],
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

    // FPS calculation: update every second
    m_fpsFrameCount++;
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - m_fpsLastTime).count();
    if (elapsed >= 1.0f) {
        m_currentFps = static_cast<float>(m_fpsFrameCount) / elapsed;
        m_fpsFrameCount = 0;
        m_fpsLastTime = now;
    }

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
