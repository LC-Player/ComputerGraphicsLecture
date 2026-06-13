// Application.cpp

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
#include "SceneConfig.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <array>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdint>

namespace RYRayTracing {

// ── Lifecycle ──────────────────────────────────────────────────────

Application::Application()
    : m_depthFormat(vk::Format::eUndefined)
    , m_currentFrame(0)
    , m_framesInFlight(0)
    , m_windowWidth(1920)
    , m_windowHeight(1080)
    , m_framebufferResized(false)
    , m_sceneManager(m_windowWidth, m_windowHeight)
    , m_fpsLastTime(std::chrono::steady_clock::now()) {
    Logger::init("raytracing.log");
    LOG_INFO("=== GPU RT Renderer ===");
}

Application::~Application() {
    cleanup();
    LOG_INFO("=== Application Shutting Down ===");
    glfwTerminate();
    Logger::shutdown();
}

void Application::run() {
    try {
        LOG_INFO("Starting GPU RT Renderer");
        initComponents();
        initVulkan();
        initImGui();
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

// ── Init: Components (scene data, no Vulkan needed yet) ───────────

void Application::initComponents() {
    SceneConfig cfg = loadSceneConfig("assets/SceneConfig.xml");

    m_sceneManager.loadCameraAndLights(cfg);

    for (const auto& pm : cfg.models) {
        if (!pm.display) continue;

        int srcIdx = m_geometryManager.addModelSource(pm.filename);
        if (srcIdx < 0) continue;

        int matIdx = m_sceneManager.addMaterial(
            pm.diffuseColor, pm.metallic, pm.roughness, pm.transparency, pm.ior);

        Transform t;
        t.scale = pm.scale;
        t.rotation = glm::radians(pm.rotation);
        t.translation = pm.translation;

        m_sceneManager.addModelRef(srcIdx, matIdx, t, pm.diffuseColor);
    }
    m_sceneManager.setMaterialsDirty();
    m_sceneManager.setModelRefsDirty();

    // Editor UI context: raw pointers into managers (non-owning, lifetime guaranteed by member order)
    EditorUIContext uiCtx;
    uiCtx.cameraTransform   = &m_sceneManager.cameraTransform();
    uiCtx.camera            = &m_sceneManager.camera();
    uiCtx.modelRefs         = &m_sceneManager.modelRefs();
    uiCtx.modelRefSourceIdx = &m_sceneManager.modelRefSourceIdx();
    uiCtx.modelRefTransforms= &m_sceneManager.modelRefTransforms();
    uiCtx.modelSources      = &m_geometryManager.modelSources();
    uiCtx.materials         = &m_sceneManager.materials();
    uiCtx.maxModelRefs      = SceneManager::kMaxModelRefs;
    uiCtx.lights            = &m_sceneManager.lights();
    uiCtx.spheres           = &m_sceneManager.spheres();
    uiCtx.maxSpheres        = SceneManager::kMaxSpheres;
    uiCtx.maxMaterials      = SceneManager::kMaxMaterials;
    uiCtx.ambientStrength   = &m_sceneManager.ambientStrength();
    uiCtx.currentFps        = &m_currentFps;
    uiCtx.setLightsDirty    = [this]() { m_sceneManager.setLightsDirty(); };
    uiCtx.setSpheresDirty   = [this]() { m_sceneManager.setSpheresDirty(); };
    uiCtx.setMaterialsDirty = [this]() { m_sceneManager.setMaterialsDirty(); };
    uiCtx.setModelRefsDirty = [this]() { m_sceneManager.setModelRefsDirty(); };
    m_editorUI = std::make_unique<EditorUI>(uiCtx);
}

// ── Init: Vulkan ──────────────────────────────────────────────────

void Application::initVulkan() {
    LOG_INFO("Initializing Vulkan...");

    WindowConfig windowConfig;
    windowConfig.width = m_windowWidth;
    windowConfig.height = m_windowHeight;
    windowConfig.title = "GPU RT Renderer";
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

    m_descriptorManager.init(m_vulkanDevice->get());
    m_descriptorManager.createLayouts();

    createDepthResources();
    createFramebuffers();
    createCommandPool();
    createUniformBuffers();

    m_sceneManager.setFramesInFlight(static_cast<uint32_t>(m_framesInFlight));
    m_geometryManager.setFramesInFlight(static_cast<uint32_t>(m_framesInFlight));

    // Scene data GPU buffers
    m_sceneManager.createLightBuffers(*m_vulkanDevice);
    m_sceneManager.createSphereBuffers(*m_vulkanDevice);
    m_sceneManager.createMaterialBuffers(*m_vulkanDevice);

    // Descriptor pool + per-frame sets
    m_descriptorManager.createDescriptorPool(static_cast<uint32_t>(m_framesInFlight),
                                              GeometryManager::kMaxTextures);
    setupPerFrameDescriptors();
    createCommandBuffers();
    createSyncObjects();

    // RT output image + compute pipeline
    createRtStorageImage();
    createRtComputePipeline();

    // Geometry + textures
    m_geometryManager.createDummyTexture(*m_vulkanDevice);
    m_geometryManager.createTextures(*m_vulkanDevice);
    m_geometryManager.createGeometryBuffers(*m_vulkanDevice,
        m_sceneManager.modelRefs(),
        m_sceneManager.modelRefSourceIdx(),
        m_sceneManager.modelRefTransforms());
    m_geometryManager.createEnvmap(*m_vulkanDevice);

    // Late descriptor sets (need RT images + geometry buffers)
    setupRtResourceDescriptors();
    setupFullscreenDescriptors();
    createFullscreenPipeline();

    LOG_INFO("Full rendering pipeline initialized (RT compute + display)");
}

// ── Descriptor setup helpers ──────────────────────────────────────

void Application::setupPerFrameDescriptors() {
    std::vector<vk::Buffer> cameraHandles;
    std::vector<vk::Buffer> lightHandles;
    for (auto& buf : m_cameraUniformBuffers)
        cameraHandles.push_back(*buf->get());
    for (auto& buf : m_sceneManager.lightBuffers())
        lightHandles.push_back(*buf.get());
    m_descriptorManager.createPerFrameSets(static_cast<uint32_t>(m_framesInFlight),
                                            cameraHandles, lightHandles);
}

void Application::setupRtResourceDescriptors() {
    // Build per-frame vk::Buffer handle vectors from GeometryManager + SceneManager
    auto extractBuf = [](const std::vector<Buffer>& bufs) {
        std::vector<vk::Buffer> handles;
        for (auto& b : bufs) handles.push_back(*b.get());
        return handles;
    };

    std::vector<vk::Buffer> sphereHandles   = extractBuf(m_sceneManager.sphereBuffers());
    std::vector<vk::Buffer> materialHandles = extractBuf(m_sceneManager.materialBuffers());
    std::vector<vk::Buffer> vertexHandles   = extractBuf(m_geometryManager.vertexBuffers());
    std::vector<vk::Buffer> indexHandles    = extractBuf(m_geometryManager.indexBuffers());
    std::vector<vk::Buffer> modelRefHandles = extractBuf(m_geometryManager.modelRefBuffers());
    std::vector<vk::Buffer> bvhHandles      = extractBuf(m_geometryManager.bvhBuffers());
    std::vector<vk::Buffer> bvhRemapHandles = extractBuf(m_geometryManager.bvhTriRemapBuffers());

    std::vector<vk::ImageView> rtViews;
    for (auto& v : m_rtOutputImageViews) rtViews.push_back(*v);

    std::vector<vk::ImageView> texViews(
        GeometryManager::kMaxTextures,
        m_geometryManager.dummyTextureView());
    for (size_t t = 0; t < m_geometryManager.textures().size(); t++) {
        if (m_geometryManager.textures()[t])
            texViews[t] = *m_geometryManager.textures()[t]->getImageView();
    }

    RtResourceBindings b;
    b.sphereBuffers     = &sphereHandles;
    b.rtImageViews      = &rtViews;
    b.materialBuffers   = &materialHandles;
    b.vertexBuffers     = &vertexHandles;
    b.indexBuffers      = &indexHandles;
    b.modelRefBuffers   = &modelRefHandles;
    b.maxTextures       = GeometryManager::kMaxTextures;
    b.textureViews      = &texViews;
    b.dummyTextureView  = m_geometryManager.dummyTextureView();
    b.textureSampler    = m_geometryManager.textureSampler();
    b.envmapInfo        = m_geometryManager.envmapTexture()->getDescriptorInfo();
    b.bvhBuffers        = &bvhHandles;
    b.bvhTriRemapBuffers= &bvhRemapHandles;

    m_descriptorManager.createRtResourceSets(static_cast<uint32_t>(m_framesInFlight), b);
}

void Application::setupFullscreenDescriptors() {
    std::vector<vk::ImageView> rtViews;
    for (auto& v : m_rtOutputImageViews) rtViews.push_back(*v);
    m_descriptorManager.createFullscreenSets(static_cast<uint32_t>(m_framesInFlight),
                                              rtViews, *m_rtOutputSampler);
}

// ── Init: ImGui ───────────────────────────────────────────────────

void Application::initImGui() {
    m_descriptorManager.createImGuiPool();

    ImGui::CreateContext();
    ImGui::GetIO().Fonts->AddFontFromFileTTF("assets/fonts/CascadiaMono.ttf", 12.0f);
    ImGui_ImplGlfw_InitForVulkan(m_windowManager->getHandle(), true);

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance = *m_vulkanInstance->get();
    init_info.PhysicalDevice = *m_vulkanDevice->getPhysical();
    init_info.Device = *m_vulkanDevice->get();
    init_info.Queue = m_vulkanDevice->getGraphicsQueue();
    init_info.DescriptorPool = m_descriptorManager.imguiPool();
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

// ── Cleanup ───────────────────────────────────────────────────────

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

    m_rtPipelineLayout = nullptr;
    m_fullscreenPipelineLayout = nullptr;
    m_rtComputeShader.reset();
    m_fullscreenVertShader.reset();
    m_fullscreenFragShader.reset();
    m_cameraUniformBuffers.clear();
    m_mappedCameraUniformData.clear();
    m_commandBuffers.clear();

    // Vulkan wrappers (unique_ptr members) are destroyed automatically in reverse
    // declaration order after cleanup() returns. Declaration order ensures:
    //   DescriptorManager → GeometryManager → SceneManager → … → VulkanDevice
    // is destroyed in the correct dependency order.

    LOG_INFO("Cleanup completed");
}

void Application::cleanupSwapChain() {
    m_swapChainFramebuffers.clear();
    m_depthImageView = nullptr;
    m_depthImage = nullptr;
    m_depthImageMemory = nullptr;
    m_fullscreenPipelineLayout = nullptr;
    // renderPassManager and swapChainManager destroyed by C++ destructor ordering
}

void Application::cleanupSyncObjects() {
    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();
    m_imagesInFlight.clear();
}

void Application::cleanupRtResources() {
    cleanupRtSwapChainResources();
    m_rtOutputSampler = nullptr;
    // SceneManager, GeometryManager, DescriptorManager destructors handle their own resources
}

void Application::cleanupRtSwapChainResources() {
    m_rtOutputImageViews.clear();
    m_rtOutputImages.clear();
    m_rtOutputImagesMemory.clear();
}

// ── Vulkan instance / device / swapchain ──────────────────────────

void Application::createInstance() {
    LOG_INFO("Creating Vulkan instance...");
    InstanceConfig config;
    config.applicationName = "GPU RT Renderer";
    config.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    config.engineName = "GPU RT Renderer";
    config.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    config.apiVersion = VK_API_VERSION_1_3;
    config.enableValidation = Validation::shouldEnableValidation();
    m_vulkanInstance = std::make_unique<VulkanInstance>(config);
    LOG_INFO("Vulkan instance created");
}

void Application::createDevice() {
    LOG_INFO("Creating Vulkan device...");
    m_windowManager->createSurface(m_vulkanInstance->get());
    DeviceConfig deviceConfig;
    deviceConfig.requiredFeatures.samplerAnisotropy = true;
    m_vulkanDevice = std::make_unique<VulkanDevice>(
        m_vulkanInstance->get(), m_windowManager->getSurface(), deviceConfig);
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
    m_depthFormat = findDepthFormat(
        {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint});
    RenderPassConfig config;
    config.colorFormat = m_swapChainManager->getImageFormat();
    config.depthFormat = m_depthFormat;
    config.clearColors = true;
    config.clearDepth = true;
    m_renderPassManager = std::make_unique<RenderPassManager>(m_vulkanDevice->get(), config);
    LOG_INFO("Render pass created");
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
        m_swapChainFramebuffers.emplace_back(
            m_vulkanDevice->get().createFramebuffer(framebufferInfo));
    }
    LOG_INFO("Framebuffers created: " + std::to_string(m_swapChainFramebuffers.size()));
}

void Application::createCommandPool() {
    CommandPoolConfig config;
    config.queueFamilyIndex = m_vulkanDevice->getGraphicsQueueFamily();
    m_commandManager = std::make_unique<CommandManager>(m_vulkanDevice->get(), config);
    LOG_INFO("Command pool created");
}

void Application::createUniformBuffers() {
    m_framesInFlight = m_swapChainManager->getImageCount();
    createUniformBuffersImpl(sizeof(CameraData), m_cameraUniformBuffers, m_mappedCameraUniformData);
    LOG_INFO("Uniform buffers created");
}

void Application::createUniformBuffersImpl(vk::DeviceSize bufferSize,
    std::vector<std::unique_ptr<Buffer>>& bufferOut,
    std::vector<void*>& mappedDataOut) const {
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

    m_depthImage = m_vulkanDevice->get().createImage(imageInfo);
    auto memReq = m_depthImage.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = m_vulkanDevice->findMemoryType(
        memReq.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    m_depthImageMemory = m_vulkanDevice->get().allocateMemory(allocInfo);
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
    m_depthImageView = m_vulkanDevice->get().createImageView(viewInfo);

    LOG_INFO("Depth resources created");
}

void Application::createCommandBuffers() {
    m_commandBuffers = m_commandManager->allocateCommandBuffers(m_swapChainFramebuffers.size());
    LOG_INFO("Command buffers created: " + std::to_string(m_commandBuffers.size()));
}

void Application::createSyncObjects() {
    m_framesInFlight = m_swapChainManager->getImageCount();
    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();
    m_imagesInFlight.resize(m_framesInFlight, nullptr);

    for (size_t i = 0; i < m_framesInFlight; i++) {
        m_imageAvailableSemaphores.emplace_back(m_vulkanDevice->get().createSemaphore({}));
        m_renderFinishedSemaphores.emplace_back(m_vulkanDevice->get().createSemaphore({}));
        m_inFlightFences.emplace_back(
            m_vulkanDevice->get().createFence({vk::FenceCreateFlagBits::eSignaled}));
    }
    LOG_INFO("Sync objects created for " + std::to_string(m_framesInFlight) + " frames");
}

// ── RT output image ───────────────────────────────────────────────

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

        // Transition to GENERAL
        auto& cmd = m_commandBuffers[0];
        cmd.reset();
        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
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
        barrier.setDstAccessMask(vk::AccessFlagBits::eShaderWrite);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                            vk::PipelineStageFlagBits::eComputeShader,
                            {}, nullptr, nullptr, barrier);
        cmd.end();
        vk::SubmitInfo submitInfo;
        submitInfo.setCommandBuffers(*m_commandBuffers[0]);
        m_vulkanDevice->getGraphicsQueue().submit(submitInfo, nullptr);
        m_vulkanDevice->waitIdle();

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

    LOG_INFO("RT storage image created: " + std::to_string(m_windowWidth)
             + "x" + std::to_string(m_windowHeight));
}

// ── Pipeline creation ─────────────────────────────────────────────

void Application::createRtComputePipeline() {
    std::string shaderPath = "shaders/raytracer.spv";
    LOG_INFO("Loading RT compute shader: " + shaderPath);
    m_rtComputeShader = std::make_unique<ShaderModule>(
        ShaderModule::createComputeShader(m_vulkanDevice.get(), shaderPath));

    vk::PushConstantRange pcRange;
    pcRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pcRange.size = sizeof(RTGlobalConstants);

    std::array<vk::DescriptorSetLayout, 2> setLayouts = {
        m_descriptorManager.perFrameLayout(),
        m_descriptorManager.rtResourceLayout()
    };
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
}

void Application::createFullscreenPipeline() {
    std::string shaderPath = "shaders/fullscreen.spv";
    LOG_INFO("Loading fullscreen shader: " + shaderPath);
    m_fullscreenVertShader = std::make_unique<ShaderModule>(
        ShaderModule::createVertexShader(m_vulkanDevice.get(), shaderPath));
    m_fullscreenFragShader = std::make_unique<ShaderModule>(
        ShaderModule::createFragmentShader(m_vulkanDevice.get(), shaderPath));

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setSetLayouts(m_descriptorManager.samplerLayout());
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
    config.vertexBindingDescription = vk::VertexInputBindingDescription{};
    config.vertexBindingDescriptions.clear();
    config.vertexAttributeDescriptions.clear();
    m_pipelineManager->createPipeline("fullscreen", config);
    LOG_INFO("Fullscreen pipeline created");
}

// ── Per-frame update ──────────────────────────────────────────────

void Application::updateUniformBuffer(size_t currentFrame) {
    glm::mat4 viewProj = m_sceneManager.camera().GetViewProj()
                       * glm::inverse(m_sceneManager.cameraTransform()());
    CameraData data{viewProj, glm::inverse(viewProj),
                    glm::vec4(m_sceneManager.cameraTransform().translation, 0.0f)};
    memcpy(m_mappedCameraUniformData[currentFrame], &data, sizeof(data));
}

// ── Frame rendering (decomposed) ──────────────────────────────────

void Application::drawFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    m_editorUI->draw();
    ImGui::Render();

    uint32_t imageIndex;
    if (!beginFrame(imageIndex)) return;

    recordComputePass();
    recordGraphicsPass(imageIndex);
    submitFrame(imageIndex);
    updateFPS();
    m_currentFrame = (m_currentFrame + 1) % m_framesInFlight;
}

bool Application::beginFrame(uint32_t& outImageIndex) {
    (void)m_vulkanDevice->get().waitForFences(
        *m_inFlightFences[m_currentFrame], true, UINT64_MAX);

    outImageIndex = m_swapChainManager->acquireNextImage(
        *m_imageAvailableSemaphores[m_currentFrame]);

    if (outImageIndex == UINT32_MAX) {
        recreateSwapChain();
        return false;
    }

    if (m_imagesInFlight[outImageIndex]) {
        (void)m_vulkanDevice->get().waitForFences(
            m_imagesInFlight[outImageIndex], true, UINT64_MAX);
    }
    m_imagesInFlight[outImageIndex] = *m_inFlightFences[m_currentFrame];

    updateUniformBuffer(m_currentFrame);
    m_sceneManager.updateLightBuffer(m_currentFrame);
    m_sceneManager.updateSphereBuffer(m_currentFrame);
    m_sceneManager.updateMaterialBuffer(m_currentFrame);
    if (m_sceneManager.isModelRefsDirty())
        m_geometryManager.updateModelRefBuffer(m_currentFrame, m_sceneManager.modelRefs());
    m_geometryManager.syncBVHBuffers(m_currentFrame);

    m_commandBuffers[m_currentFrame].reset();
    vk::CommandBufferBeginInfo beginInfo;
    m_commandBuffers[m_currentFrame].begin(beginInfo);
    return true;
}

void Application::recordComputePass() {
    auto& cmd = m_commandBuffers[m_currentFrame];

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute,
                     *m_pipelineManager->getPipeline("rt_main"));

    std::array<vk::DescriptorSet, 2> rtSets = {
        *m_descriptorManager.perFrameSets()[m_currentFrame],
        *m_descriptorManager.rtSets()[m_currentFrame]
    };
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           *m_rtPipelineLayout, 0, rtSets, nullptr);

    RTGlobalConstants rtPC{};
    rtPC.sphereCount = static_cast<uint32_t>(m_sceneManager.spheres().size());
    rtPC.lightCount = static_cast<uint32_t>(m_sceneManager.lights().size());
    rtPC.materialCount = static_cast<uint32_t>(m_sceneManager.materials().size());
    rtPC.modelRefCount = static_cast<uint32_t>(m_sceneManager.modelRefs().size());
    rtPC.ambientStrength = m_sceneManager.ambientStrength();
    cmd.pushConstants<RTGlobalConstants>(
        *m_rtPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, rtPC);

    uint32_t groupX = (m_windowWidth  + 7) / 8;
    uint32_t groupY = (m_windowHeight + 7) / 8;
    cmd.dispatch(groupX, groupY, 1);

    // Barrier: compute write → fragment read
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
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eFragmentShader,
                        {}, nullptr, nullptr, barrier);
}

void Application::recordGraphicsPass(uint32_t imageIndex) {
    auto& cmd = m_commandBuffers[m_currentFrame];

    vk::Viewport viewport;
    viewport.setX(0.0f); viewport.setY(0.0f);
    viewport.setWidth(static_cast<float>(m_swapChainManager->getExtent().width));
    viewport.setHeight(static_cast<float>(m_swapChainManager->getExtent().height));
    viewport.setMinDepth(0.0f); viewport.setMaxDepth(1.0f);
    cmd.setViewport(0, viewport);

    vk::Rect2D scissor;
    scissor.setOffset({0, 0});
    scissor.setExtent(m_swapChainManager->getExtent());
    cmd.setScissor(0, scissor);

    std::array<vk::ClearValue, 2> clearValues;
    clearValues[0].color = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f};
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    vk::RenderPassBeginInfo renderPassInfo;
    renderPassInfo.setRenderPass(*m_renderPassManager->get());
    renderPassInfo.setFramebuffer(*m_swapChainFramebuffers[imageIndex]);
    renderPassInfo.setRenderArea(vk::Rect2D{{0, 0}, m_swapChainManager->getExtent()});
    renderPassInfo.setClearValues(clearValues);

    cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                     *m_pipelineManager->getPipeline("fullscreen"));
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                           *m_fullscreenPipelineLayout, 0,
                           *m_descriptorManager.fullscreenSets()[m_currentFrame], nullptr);
    cmd.draw(3, 1, 0, 0);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *cmd);
    cmd.endRenderPass();
}

void Application::submitFrame(uint32_t imageIndex) {
    auto& cmd = m_commandBuffers[m_currentFrame];
    cmd.end();

    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::SubmitInfo submitInfo;
    submitInfo.setWaitSemaphores(*m_imageAvailableSemaphores[m_currentFrame]);
    submitInfo.setWaitDstStageMask(waitStages);
    submitInfo.setCommandBuffers(*cmd);
    submitInfo.setSignalSemaphores(*m_renderFinishedSemaphores[m_currentFrame]);

    m_vulkanDevice->get().resetFences(*m_inFlightFences[m_currentFrame]);
    m_vulkanDevice->getGraphicsQueue().submit(submitInfo, *m_inFlightFences[m_currentFrame]);

    m_swapChainManager->presentImage(imageIndex, *m_renderFinishedSemaphores[m_currentFrame]);
}

void Application::updateFPS() {
    m_fpsFrameCount++;
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - m_fpsLastTime).count();
    if (elapsed >= 1.0f) {
        m_currentFps = static_cast<float>(m_fpsFrameCount) / elapsed;
        m_fpsFrameCount = 0;
        m_fpsLastTime = now;
    }
}

// ── Main loop + resize ────────────────────────────────────────────

void Application::mainLoop() {
    LOG_INFO("Entering main loop...");
    while (!m_windowManager->shouldClose()) {
        glfwPollEvents();
        drawFrame();
        if (m_framebufferResized) {
            recreateSwapChain();
        }
    }
    m_vulkanDevice->waitIdle();
    LOG_INFO("Main loop exited");
}

void Application::recreateSwapChain() {
    if (m_windowWidth == 0 || m_windowHeight == 0) return;

    m_vulkanDevice->waitIdle();
    cleanupSwapChain();
    cleanupRtSwapChainResources();

    m_swapChainManager->recreate(m_windowWidth, m_windowHeight);
    m_framesInFlight = m_swapChainManager->getImageCount();
    createRenderPass();
    createDepthResources();
    createFramebuffers();
    createRtStorageImage();

    // Rebind RT output to existing descriptor sets in-place
    for (size_t i = 0; i < m_framesInFlight; ++i) {
        m_descriptorManager.updateRtOutputBinding(i, *m_rtOutputImageViews[i]);
        m_descriptorManager.updateFullscreenBinding(i, *m_rtOutputImageViews[i],
                                                     *m_rtOutputSampler);
    }
    createFullscreenPipeline();

    m_sceneManager.camera().SetAspectRatio(
        static_cast<float>(m_windowWidth) / m_windowHeight);
    m_framebufferResized = false;
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

} // namespace RYRayTracing
