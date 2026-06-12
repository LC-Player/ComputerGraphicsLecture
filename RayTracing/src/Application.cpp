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
#include "Welzl.h"
#include "SceneConfig.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <array>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdint>

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
    createDescriptorSetLayouts();
    createDepthResources();
    createFramebuffers();
    createCommandPool();
    createUniformBuffers();
    createLightBuffer();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();
    createSyncObjects();

    createRtStorageImage();
    createRtComputePipeline();
    createSphereBuffer();
    createMaterialBuffer();
    createModelDataBuffers();
    createTextures();
    createDummyTexture();
    createEnvmap();
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

    ImGui::GetIO().Fonts->AddFontFromFileTTF("assets/fonts/CascadiaMono.ttf", 12.0f);

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
    m_camera.SetPerspective(glm::radians(60.0f), 0.1f, 200.0f);
    m_cameraTransform.translation = { 0.0f, 0.0f, 0.0f };
    m_cameraTransform.rotation = { 0, 0, 0 };

    // ── Load scene from XML ──
    SceneConfig cfg = loadSceneConfig("assets/SceneConfig.xml");
    m_ambientStrength = cfg.ambientStrength;

    // ── Lights ──
    for (const auto& pl : cfg.lights) {
        LightData l{};
        l.color = pl.color;
        l.intensity = pl.intensity;
        if (pl.type == LightType::Directional) {
            l.type = 2;
            l.direction = pl.direction;
        } else if (pl.type == LightType::Spot) {
            l.type = 1;
            l.position = pl.position;
            l.direction = pl.direction;
            l.maxDistance = pl.maxDistance;
            l.innerCos = std::cos(glm::radians(pl.innerAngle));
            l.outerCos = std::cos(glm::radians(pl.outerAngle));
        } else {
            l.type = 0;
            l.position = pl.position;
            l.maxDistance = pl.maxDistance;
        }
        m_lights.push_back(l);
    }
    setLightsDirty();

    // ── Models & Materials ──
    for (const auto& pm : cfg.models) {
        if (!pm.display) continue;

        int srcIdx = addModelSource(pm.filename);
        if (srcIdx < 0) continue;

        MaterialData mat{};
        mat.diffuseColor = pm.diffuseColor;
        mat.metallic = pm.metallic;
        mat.roughness = pm.roughness;
        mat.transparency = pm.transparency;
        mat.ior = pm.ior;
        int matIdx = static_cast<int>(m_materials.size());
        m_materials.push_back(mat);

        ModelRef ref{};
        ref.materialId = static_cast<uint32_t>(matIdx);
        ref.color = glm::vec4(pm.diffuseColor, 1.0f);
        m_modelRefs.push_back(ref);
        m_modelRefSourceIdx.push_back(srcIdx);

        Transform t;
        t.scale = pm.scale;
        t.rotation = glm::radians(pm.rotation);
        t.translation = pm.translation;
        m_modelRefTransforms.push_back(t);
    }
    setMaterialsDirty();
    setModelRefsDirty();

    // ── Editor UI ──────────────────────────────────────────────
    EditorUIContext uiCtx;
    uiCtx.cameraTransform = &m_cameraTransform;
    uiCtx.camera = &m_camera;
    uiCtx.modelRefs = &m_modelRefs;
    uiCtx.modelRefSourceIdx = &m_modelRefSourceIdx;
    uiCtx.modelRefTransforms = &m_modelRefTransforms;
    uiCtx.modelSources = &m_modelSources;
    uiCtx.materials = &m_materials;
    uiCtx.maxModelRefs = kMaxModelRefs;
    uiCtx.lights = &m_lights;
    uiCtx.spheres = &m_spheres;
    uiCtx.maxSpheres = kMaxSpheres;
    uiCtx.maxMaterials = kMaxMaterials;
    uiCtx.ambientStrength = &m_ambientStrength;
    uiCtx.currentFps = &m_currentFps;
    uiCtx.setLightsDirty = [this]() { setLightsDirty(); };
    uiCtx.setSpheresDirty = [this]() { setSpheresDirty(); };
    uiCtx.setMaterialsDirty = [this]() { setMaterialsDirty(); };
    uiCtx.setModelRefsDirty = [this]() { setModelRefsDirty(); };
    m_editorUI = std::make_unique<EditorUI>(uiCtx);
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
    m_rtPipelineLayout = nullptr;
    m_fullscreenPipelineLayout = nullptr;
    m_rtComputeShader.reset();
    m_fullscreenVertShader.reset();
    m_fullscreenFragShader.reset();
    m_cameraUniformBuffers.clear();
    m_mappedCameraUniformData.clear();
    m_lightBuffers.clear();
    m_commandBuffers.clear();
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

    // RT resource: b0-b9 = spheres, output, materials, vertices, indices, modelRefs, textures, envmap, bvh, bvhTriRemap
    {
        std::array<vk::DescriptorSetLayoutBinding, 10> bindings;
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageImage;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[4].binding = 4;
        bindings[4].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[5].binding = 5;
        bindings[5].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[6].binding = 6;
        bindings[6].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[6].descriptorCount = kMaxTextures;
        bindings[6].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[7].binding = 7;
        bindings[7].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[7].descriptorCount = 1;
        bindings[7].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[8].binding = 8;
        bindings[8].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[8].descriptorCount = 1;
        bindings[8].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[9].binding = 9;
        bindings[9].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[9].descriptorCount = 1;
        bindings[9].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo;
        layoutInfo.setBindings(bindings);
        m_rtResourceSetLayout = m_vulkanDevice->get().createDescriptorSetLayout(layoutInfo);
    }

    LOG_INFO("Descriptor set layouts created");
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
    if (m_lights.empty()) {
        m_lights.push_back({});
    }
    for (size_t i = 0; i < m_framesInFlight; i++) {
        auto buf = std::make_unique<Buffer>(
            Buffer::createBuffer(m_vulkanDevice.get(),
                kMaxLights * sizeof(LightData),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
        buf->copyFrom(m_lights.data(), m_lights.size() * sizeof(LightData));
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

    uint32_t fif = static_cast<uint32_t>(m_framesInFlight);

    std::array<vk::DescriptorPoolSize, 4> poolSizes;
    poolSizes[0].type = vk::DescriptorType::eUniformBuffer;
    poolSizes[0].descriptorCount = fif; // camera UBO
    poolSizes[1].type = vk::DescriptorType::eCombinedImageSampler;
    poolSizes[1].descriptorCount = fif * (kMaxTextures + 2); // frames in flight * (kMaxTexture, 1 env map, 1 full screen),
    poolSizes[2].type = vk::DescriptorType::eStorageBuffer;
    poolSizes[2].descriptorCount = fif * 8; // lights, sphere, materials, vertices, indices, modelRefs, BVH nodes, BVH triRemap
    poolSizes[3].type = vk::DescriptorType::eStorageImage;
    poolSizes[3].descriptorCount = fif; // RT output

    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = fif * 4;
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
    if (!isLightsDirty() || m_lights.empty()) return;
    m_lightBuffers[currentFrame]->copyFrom(m_lights.data(),
        m_lights.size() * sizeof(LightData));
}

void Application::updateSphereBuffer(size_t currentFrame) {
    if (!isSpheresDirty() || m_spheres.empty()) return;
    m_sphereBuffers[currentFrame]->copyFrom(m_spheres.data(), m_spheres.size() * sizeof(SphereData));
}

void Application::updateMaterialBuffer(size_t currentFrame) {
    if (!isMaterialsDirty() || m_materials.empty()) return;
    m_materialBuffers[currentFrame]->copyFrom(m_materials.data(), m_materials.size() * sizeof(MaterialData));
}

void Application::updateModelRefBuffer(size_t currentFrame) {
    if (!isModelRefsDirty() || m_modelRefs.empty()) return;
    m_modelRefBuffers[currentFrame]->copyFrom(m_modelRefs.data(), m_modelRefs.size() * sizeof(ModelRef));
}


void Application::cleanupSwapChain() {
    m_swapChainFramebuffers.clear();
    m_depthImageView = nullptr;
    m_depthImage = nullptr;
    m_depthImageMemory = nullptr;
    m_renderPassManager.reset();
    m_fullscreenPipelineLayout = nullptr;
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
    m_rtDescriptorSets.clear();
    m_fullscreenDescriptorSets.clear();
    m_sphereBuffers.clear();
    m_materialBuffers.clear();
    m_vertexBuffers.clear();
    m_indexBuffers.clear();
    m_modelRefBuffers.clear();
    m_bvhBuffers.clear();
    m_bvhTriRemapBuffers.clear();
    m_textures.clear();
    m_envmapTexture.reset();
    m_dummyTextureView = nullptr;
    m_dummyTextureImage = nullptr;
    m_dummyTextureMemory = nullptr;
    m_textureSampler = nullptr;
}

// ── Model data (merged buffers) ─────────────────────────────────────

int Application::addModelSource(const std::string& objPath, const std::string& texturePath) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, objPath.c_str())) {
        LOG_ERROR("Failed to load model: " + warn + err);
        return -1;
    }

    ModelSource src;
    src.name = objPath;
    std::unordered_map<uint64_t, uint32_t> uniqueMap;

    for (const auto& shape : shapes) {
        for (const auto& idx : shape.mesh.indices) {
            uint64_t key = static_cast<uint64_t>(idx.vertex_index) << 32
                         | static_cast<uint64_t>(static_cast<uint16_t>(idx.texcoord_index)) << 16
                         | static_cast<uint64_t>(static_cast<uint16_t>(idx.normal_index));

            if (!uniqueMap.contains(key)) {
                uniqueMap[key] = static_cast<uint32_t>(src.positions.size());
                glm::vec3 pos = {
                    attrib.vertices[3 * idx.vertex_index + 0],
                    attrib.vertices[3 * idx.vertex_index + 1],
                    attrib.vertices[3 * idx.vertex_index + 2]
                };
                src.positions.push_back(pos);

                if (idx.normal_index >= 0) {
                    src.normals.emplace_back(
                        attrib.normals[3 * idx.normal_index + 0],
                        attrib.normals[3 * idx.normal_index + 1],
                        attrib.normals[3 * idx.normal_index + 2]
                    );
                } else {
                    src.normals.emplace_back(0.0f, 0.0f, 0.0f);
                }

                if (idx.texcoord_index >= 0) {
                    src.texCoords.emplace_back(
                        attrib.texcoords[2 * idx.texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]
                    );
                } else {
                    src.texCoords.emplace_back(0.0f, 0.0f);
                }
            }
            src.indices.push_back(uniqueMap[key]);
        }
    }

    // Compute smooth vertex normals if the OBJ has none
    bool hasNormals = false;
    for (const auto& n : src.normals) {
        if (glm::dot(n, n) > 0.0f) {
            hasNormals = true;
            break;
        }
    }
    if (!hasNormals && src.indices.size() >= 3) {
        std::vector<glm::vec3> accum(src.positions.size(), glm::vec3(0.0f));
        for (size_t i = 0; i + 2 < src.indices.size(); i += 3) {
            uint32_t i0 = src.indices[i];
            uint32_t i1 = src.indices[i + 1];
            uint32_t i2 = src.indices[i + 2];
            glm::vec3 faceN = glm::cross(
                src.positions[i1] - src.positions[i0],
                src.positions[i2] - src.positions[i0]);
            accum[i0] += faceN;
            accum[i1] += faceN;
            accum[i2] += faceN;
        }
        for (auto& n : accum) {
            float len2 = glm::dot(n, n);
            n = (len2 > 1e-12f) ? n / std::sqrt(len2) : glm::vec3(0.0f, 1.0f, 0.0f);
        }
        src.normals = std::move(accum);
    }

    WelzlSphere ws = computeMinEnclosingSphere(src.positions);
    src.boundingSphereCenter = ws.center;
    src.boundingSphereRadius = ws.radius;
    src.texturePath = texturePath;

    // Build object-space BVH
    ModelSourceBVH bvh;
    buildSAHBVH(src, bvh);
    m_modelBVHs.push_back(std::move(bvh));

    int idx = static_cast<int>(m_modelSources.size());
    m_modelSources.push_back(std::move(src));
    LOG_INFO("Loaded model '" + objPath + "': " +
             std::to_string(m_modelSources.back().positions.size()) + " vertices, " +
             std::to_string(m_modelSources.back().indices.size()) + " indices" +
             (texturePath.empty() ? "" : " (textured)"));
    return idx;
}

void Application::createDummyTexture() {
    // 1x1 white pixel
    uint32_t white = 0xFFFFFFFF;
    vk::ImageCreateInfo imageInfo;
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent = vk::Extent3D{1, 1, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = vk::Format::eR8G8B8A8Unorm;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    m_dummyTextureImage = m_vulkanDevice->get().createImage(imageInfo);

    auto memReqs = m_dummyTextureImage.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = m_vulkanDevice->findMemoryType(memReqs.memoryTypeBits,
        vk::MemoryPropertyFlagBits::eDeviceLocal);
    m_dummyTextureMemory = m_vulkanDevice->get().allocateMemory(allocInfo);
    m_dummyTextureImage.bindMemory(*m_dummyTextureMemory, 0);

    // Upload pixel
    {
        Buffer staging(m_vulkanDevice.get(), {4, vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent});
        staging.copyFrom(&white, 4);

        auto& cmd = m_commandBuffers[0];
        cmd.reset();
        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(beginInfo);

        vk::ImageMemoryBarrier barrier;
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.image = *m_dummyTextureImage;
        barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, barrier);

        vk::BufferImageCopy region;
        region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        region.imageExtent = vk::Extent3D{1, 1, 1};
        cmd.copyBufferToImage(*staging.get(), *m_dummyTextureImage,
            vk::ImageLayout::eTransferDstOptimal, region);

        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader, {}, nullptr, nullptr, barrier);

        cmd.end();
        vk::SubmitInfo submitInfo;
        submitInfo.setCommandBuffers(*cmd);
        m_vulkanDevice->getGraphicsQueue().submit(submitInfo, nullptr);
        m_vulkanDevice->waitIdle();
    }

    vk::ImageViewCreateInfo viewInfo;
    viewInfo.image = *m_dummyTextureImage;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = vk::Format::eR8G8B8A8Unorm;
    viewInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    m_dummyTextureView = m_vulkanDevice->get().createImageView(viewInfo);

    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
    m_textureSampler = m_vulkanDevice->get().createSampler(samplerInfo);

    LOG_INFO("Dummy texture and sampler created");
}

void Application::createTextures() {
    while (m_textures.size() < m_modelSources.size()) {
        size_t i = m_textures.size();
        if (!m_modelSources[i].texturePath.empty()) {
            TextureConfig cfg;
            cfg.filepath = m_modelSources[i].texturePath;
            m_textures.push_back(std::make_unique<Texture>(m_vulkanDevice.get(), cfg));
        } else {
            m_textures.push_back(nullptr);
        }
    }
}

void Application::createEnvmap() {
    TextureConfig cfg;
    cfg.filepath = "assets/textures/envmap.jpg";
    cfg.addressMode = vk::SamplerAddressMode::eRepeat;
    cfg.magFilter = vk::Filter::eLinear;
    cfg.minFilter = vk::Filter::eLinear;
    m_envmapTexture = std::make_unique<Texture>(m_vulkanDevice.get(), cfg);
    LOG_INFO("Envmap texture created");
}

void Application::createModelDataBuffers() {
    createGeometryBuffers();
}

void Application::createGeometryBuffers() {
    m_vertexBuffers.clear();
    m_indexBuffers.clear();
    m_modelRefBuffers.clear();

    std::vector<GPUVertex> mergedVerts;
    std::vector<uint32_t> mergedIndices;

    // Build per-source offsets (each model source gets one copy)
    for (auto& src : m_modelSources) {
        src.vertexOffset = static_cast<uint32_t>(mergedVerts.size());
        src.firstIndex  = static_cast<uint32_t>(mergedIndices.size());
        src.indexCount  = static_cast<uint32_t>(src.indices.size());

        for (size_t v = 0; v < src.positions.size(); v++) {
            GPUVertex gv;
            gv.position = src.positions[v];
            gv.normal = v < src.normals.size() ? src.normals[v] : glm::vec3(0.0f);
            gv.texCoord = v < src.texCoords.size() ? src.texCoords[v] : glm::vec2(0.0f);
            gv._pad[0] = 0.0f;
            gv._pad[1] = 0.0f;
            mergedVerts.push_back(gv);
        }
        mergedIndices.insert(mergedIndices.end(), src.indices.begin(), src.indices.end());
    }

    if (mergedVerts.empty()) {
        mergedVerts.push_back({});
        mergedIndices.push_back(0);
    }

    // Apply source offsets to model refs
    for (size_t i = 0; i < m_modelRefs.size(); i++) {
        const auto& src = m_modelSources[m_modelRefSourceIdx[i]];
        m_modelRefs[i].vertexOffset = src.vertexOffset;
        m_modelRefs[i].firstIndex  = src.firstIndex;
        m_modelRefs[i].indexCount  = src.indexCount;
        m_modelRefs[i].boundingSphereCenter = src.boundingSphereCenter;
        m_modelRefs[i].boundingSphereRadius = src.boundingSphereRadius;
        m_modelRefs[i].invTransform = glm::inverse(m_modelRefTransforms[i].transform());
        m_modelRefs[i].textureIndex = m_modelRefSourceIdx[i];
    }

    // Post-process BVH trees
    m_flatBVHNodes.clear();
    std::vector<uint32_t> mergedTriRemap;
    int32_t globalNodeOffset = 0;
    for (size_t s = 0; s < m_modelSources.size(); ++s) {
        if (s >= m_modelBVHs.size()) break;
        const auto& bvhSrc = m_modelBVHs[s];

        // Convert triRemap: local triangle index → global index buffer offset
        uint32_t remapBase = static_cast<uint32_t>(mergedTriRemap.size());
        for (uint32_t localTri : bvhSrc.triRemap) {
            mergedTriRemap.push_back(
                m_modelSources[s].firstIndex + localTri * 3);
        }

        // Offset nodes
        for (BVHNode node : bvhSrc.nodes) {
            if (node.splitAxis >= 0) {
                node.leftOrFirst += globalNodeOffset;
                node.rightOrCount += globalNodeOffset;
            } else {
                // leftOrFirst is position in triRemap → offset into merged array
                node.leftOrFirst += static_cast<int32_t>(remapBase);
            }
            m_flatBVHNodes.push_back(node);
        }
        for (size_t r = 0; r < m_modelRefs.size(); ++r) {
            if (m_modelRefSourceIdx[r] == static_cast<int>(s))
                m_modelRefs[r].bvhRoot = globalNodeOffset + bvhSrc.rootIndex;
        }
        m_modelSources[s].bvhRoot = globalNodeOffset + bvhSrc.rootIndex;
        globalNodeOffset += static_cast<int32_t>(bvhSrc.nodes.size());
    }

    for (size_t i = 0; i < m_framesInFlight; i++) {
        m_vertexBuffers.push_back(std::make_unique<Buffer>(
            Buffer::createBuffer(m_vulkanDevice.get(),
                kMaxVertices * sizeof(GPUVertex),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));
        m_vertexBuffers.back()->copyFrom(mergedVerts.data(), mergedVerts.size() * sizeof(GPUVertex));

        m_indexBuffers.push_back(std::make_unique<Buffer>(
            Buffer::createBuffer(m_vulkanDevice.get(),
                kMaxIndices * sizeof(uint32_t),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));
        m_indexBuffers.back()->copyFrom(mergedIndices.data(), mergedIndices.size() * sizeof(uint32_t));

        m_modelRefBuffers.push_back(std::make_unique<Buffer>(
            Buffer::createBuffer(m_vulkanDevice.get(),
                kMaxModelRefs * sizeof(ModelRef),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));
        m_modelRefBuffers.back()->copyFrom(m_modelRefs.data(), m_modelRefs.size() * sizeof(ModelRef));
    }

    // BVH SSBO
    m_bvhBuffers.clear();
    for (size_t i = 0; i < m_framesInFlight; i++) {
        m_bvhBuffers.push_back(std::make_unique<Buffer>(
            Buffer::createBuffer(m_vulkanDevice.get(),
                kMaxBVHNodes * sizeof(BVHNode),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));
        if (!m_flatBVHNodes.empty())
            m_bvhBuffers.back()->copyFrom(m_flatBVHNodes.data(),
                m_flatBVHNodes.size() * sizeof(BVHNode));
    }
    setBVHDirty();

    // BVH tri-remap SSBO (maps BVH leaf positions → global index buffer offsets)
    static constexpr size_t kMaxTriRemap = 1'000'000;
    m_bvhTriRemapBuffers.clear();
    for (size_t i = 0; i < m_framesInFlight; i++) {
        m_bvhTriRemapBuffers.push_back(std::make_unique<Buffer>(
            Buffer::createBuffer(m_vulkanDevice.get(),
                kMaxTriRemap * sizeof(uint32_t),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));
        if (!mergedTriRemap.empty())
            m_bvhTriRemapBuffers.back()->copyFrom(mergedTriRemap.data(),
                mergedTriRemap.size() * sizeof(uint32_t));
    }

    setModelRefsDirty();
    LOG_INFO("Geometry built: " + std::to_string(mergedVerts.size()) + " vertices, " +
             std::to_string(mergedIndices.size()) + " indices, " +
             std::to_string(m_modelRefs.size()) + " model refs");
}

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
            submitInfo.setCommandBuffers(*m_commandBuffers[0]);
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
    m_rtDescriptorSets.clear(); // free old descriptors back to pool

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

        // b2: material SSBO
        {
            vk::DescriptorBufferInfo bufferInfo;
            bufferInfo.buffer = *m_materialBuffers[i]->get();
            bufferInfo.offset = 0;
            bufferInfo.range = VK_WHOLE_SIZE;

            vk::WriteDescriptorSet write;
            write.dstSet = *m_rtDescriptorSets[i];
            write.dstBinding = 2;
            write.dstArrayElement = 0;
            write.descriptorType = vk::DescriptorType::eStorageBuffer;
            write.setBufferInfo(bufferInfo);
            m_vulkanDevice->get().updateDescriptorSets(write, nullptr);
        }

        // b3: merged vertex SSBO
        if (!m_vertexBuffers.empty()) {
            vk::DescriptorBufferInfo bufferInfo;
            bufferInfo.buffer = *m_vertexBuffers[i]->get();
            bufferInfo.offset = 0;
            bufferInfo.range = VK_WHOLE_SIZE;

            vk::WriteDescriptorSet write;
            write.dstSet = *m_rtDescriptorSets[i];
            write.dstBinding = 3;
            write.dstArrayElement = 0;
            write.descriptorType = vk::DescriptorType::eStorageBuffer;
            write.setBufferInfo(bufferInfo);
            m_vulkanDevice->get().updateDescriptorSets(write, nullptr);
        }

        // b4: merged index SSBO
        if (!m_indexBuffers.empty()) {
            vk::DescriptorBufferInfo bufferInfo;
            bufferInfo.buffer = *m_indexBuffers[i]->get();
            bufferInfo.offset = 0;
            bufferInfo.range = VK_WHOLE_SIZE;

            vk::WriteDescriptorSet write;
            write.dstSet = *m_rtDescriptorSets[i];
            write.dstBinding = 4;
            write.dstArrayElement = 0;
            write.descriptorType = vk::DescriptorType::eStorageBuffer;
            write.setBufferInfo(bufferInfo);
            m_vulkanDevice->get().updateDescriptorSets(write, nullptr);
        }

        // b5: ModelRef SSBO
        if (!m_modelRefBuffers.empty()) {
            vk::DescriptorBufferInfo bufferInfo;
            bufferInfo.buffer = *m_modelRefBuffers[i]->get();
            bufferInfo.offset = 0;
            bufferInfo.range = VK_WHOLE_SIZE;

            vk::WriteDescriptorSet write;
            write.dstSet = *m_rtDescriptorSets[i];
            write.dstBinding = 5;
            write.dstArrayElement = 0;
            write.descriptorType = vk::DescriptorType::eStorageBuffer;
            write.setBufferInfo(bufferInfo);
            m_vulkanDevice->get().updateDescriptorSets(write, nullptr);
        }

        // b6: texture array (combined image samplers)
        std::array<vk::DescriptorImageInfo, kMaxTextures> texInfos;
        for (size_t t = 0; t < kMaxTextures; t++) {
            if (t < m_textures.size() && m_textures[t]) {
                texInfos[t].imageView = m_textures[t]->getImageView();
            } else {
                texInfos[t].imageView = *m_dummyTextureView;
            }
            texInfos[t].sampler = *m_textureSampler;
            texInfos[t].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }

        vk::WriteDescriptorSet texWrite;
        texWrite.dstSet = *m_rtDescriptorSets[i];
        texWrite.dstBinding = 6;
        texWrite.dstArrayElement = 0;
        texWrite.descriptorCount = kMaxTextures;
        texWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        texWrite.setImageInfo(texInfos);
        m_vulkanDevice->get().updateDescriptorSets(texWrite, nullptr);

        // b7: envmap
        {
            vk::WriteDescriptorSet write;
            write.dstSet = *m_rtDescriptorSets[i];
            write.dstBinding = 7;
            write.dstArrayElement = 0;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            std::array writeInfos = { m_envmapTexture->getDescriptorInfo() };
            write.setImageInfo(writeInfos);
            m_vulkanDevice->get().updateDescriptorSets(write, nullptr);
        }

        // b8: BVH nodes SSBO
        if (!m_bvhBuffers.empty()) {
            vk::DescriptorBufferInfo bufferInfo;
            bufferInfo.buffer = *m_bvhBuffers[i]->get();
            bufferInfo.offset = 0;
            bufferInfo.range = VK_WHOLE_SIZE;

            vk::WriteDescriptorSet write;
            write.dstSet = *m_rtDescriptorSets[i];
            write.dstBinding = 8;
            write.dstArrayElement = 0;
            write.descriptorType = vk::DescriptorType::eStorageBuffer;
            std::array writeInfos = { bufferInfo };
            write.setBufferInfo(writeInfos);
            m_vulkanDevice->get().updateDescriptorSets(write, nullptr);
        }

        // b9: BVH tri-remap SSBO
        if (!m_bvhTriRemapBuffers.empty()) {
            vk::DescriptorBufferInfo bufferInfo;
            bufferInfo.buffer = *m_bvhTriRemapBuffers[i]->get();
            bufferInfo.offset = 0;
            bufferInfo.range = VK_WHOLE_SIZE;

            vk::WriteDescriptorSet write;
            write.dstSet = *m_rtDescriptorSets[i];
            write.dstBinding = 9;
            write.dstArrayElement = 0;
            write.descriptorType = vk::DescriptorType::eStorageBuffer;
            std::array writeInfos = { bufferInfo };
            write.setBufferInfo(writeInfos);
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
    for (size_t i = 0; i < m_framesInFlight; i++) {
        auto sphereBuffer = std::make_unique<Buffer>(
            Buffer::createBuffer(m_vulkanDevice.get(),
                kMaxSpheres * sizeof(SphereData),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
        if (!m_spheres.empty())
            sphereBuffer->copyFrom(m_spheres.data(), m_spheres.size() * sizeof(SphereData));
        m_sphereBuffers.emplace_back(std::move(sphereBuffer));
    }
    m_spheresDirty = 0;
    LOG_INFO("Sphere buffer created with " + std::to_string(m_spheres.size()) + " spheres");
}

// ── Material buffer (SSBO) ─────────────────────────────────────────

void Application::createMaterialBuffer() {
    for (size_t i = 0; i < m_framesInFlight; i++) {
        auto buf = std::make_unique<Buffer>(
            Buffer::createBuffer(m_vulkanDevice.get(),
                kMaxMaterials * sizeof(MaterialData),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
        if (!m_materials.empty())
            buf->copyFrom(m_materials.data(), m_materials.size() * sizeof(MaterialData));
        m_materialBuffers.emplace_back(std::move(buf));
    }
    m_materialsDirty = 0;
    LOG_INFO("Material buffer created with " + std::to_string(m_materials.size()) + " materials");
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

void Application::cleanupRtSwapChainResources() {
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
    m_framesInFlight = m_swapChainManager->getImageCount();
    createRenderPass();
    createDepthResources();
    createFramebuffers();

    createRtStorageImage();

    // Update output image bindings on existing descriptor sets in-place
    for (size_t i = 0; i < m_framesInFlight; ++i) {
        vk::DescriptorImageInfo imageInfo;
        imageInfo.imageView = *m_rtOutputImageViews[i];
        imageInfo.imageLayout = vk::ImageLayout::eGeneral;

        vk::WriteDescriptorSet rtWrite;
        rtWrite.dstSet = *m_rtDescriptorSets[i];
        rtWrite.dstBinding = 1;
        rtWrite.dstArrayElement = 0;
        rtWrite.descriptorType = vk::DescriptorType::eStorageImage;
        rtWrite.setImageInfo(imageInfo);
        m_vulkanDevice->get().updateDescriptorSets(rtWrite, nullptr);

        vk::DescriptorImageInfo samplerInfo;
        samplerInfo.imageView = *m_rtOutputImageViews[i];
        samplerInfo.imageLayout = vk::ImageLayout::eGeneral;
        samplerInfo.sampler = *m_rtOutputSampler;

        vk::WriteDescriptorSet fsWrite;
        fsWrite.dstSet = *m_fullscreenDescriptorSets[i];
        fsWrite.dstBinding = 0;
        fsWrite.dstArrayElement = 0;
        fsWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        fsWrite.setImageInfo(samplerInfo);
        m_vulkanDevice->get().updateDescriptorSets(fsWrite, nullptr);
    }

    createFullscreenPipeline();

    m_camera.SetAspectRatio(static_cast<float>(m_windowWidth) / m_windowHeight);

    m_framebufferResized = false;
}

void Application::drawFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    m_editorUI->draw();

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
    updateMaterialBuffer(m_currentFrame);
    updateModelRefBuffer(m_currentFrame);
    if (isBVHDirty() && !m_bvhBuffers.empty()) {
        m_bvhBuffers[m_currentFrame]->copyFrom(m_flatBVHNodes.data(),
            m_flatBVHNodes.size() * sizeof(BVHNode));
    }

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
    rtPC.lightCount = static_cast<uint32_t>(m_lights.size());
    rtPC.materialCount = static_cast<uint32_t>(m_materials.size());
    rtPC.modelRefCount = static_cast<uint32_t>(m_modelRefs.size());
    rtPC.ambientStrength = m_ambientStrength;
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
