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
#include "vulkan/AccelerationStructure.hpp"

#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"
#include "imgui.h"
#include "SceneConfig.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <array>
#include <chrono>
#include <cstring>

namespace RYRayTracing {

// ── Lifecycle ──────────────────────────────────────────────────────

Application::Application()
    : m_depthFormat(vk::Format::eUndefined)
    , m_currentFrame(0)
    , m_framesInFlight(0)
    , m_windowWidth(1920)
    , m_windowHeight(1080)
    , m_sceneManager(m_windowWidth, m_windowHeight)
    , m_framebufferResized(false)
    , m_fpsLastTime(std::chrono::steady_clock::now()) {
    Logger::init("pathtracing.log");
    LOG_INFO("=== GPU Hardware Path Tracer ===");
}

Application::~Application() {
    cleanup();
    LOG_INFO("=== Application Shutting Down ===");
    glfwTerminate();
    Logger::shutdown();
}

void Application::run() {
    try {
        LOG_INFO("Starting GPU Hardware Path Tracer");
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

// ── Init: Components ──────────────────────────────────────────────

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
    uiCtx.setSpheresDirty   = [this]() { m_sceneManager.setSpheresDirty(); m_tlasNeedsRebuild = true; };
    uiCtx.setMaterialsDirty = [this]() { m_sceneManager.setMaterialsDirty(); };
    uiCtx.setModelRefsDirty = [this]() { m_sceneManager.setModelRefsDirty(); m_tlasNeedsRebuild = true; m_accumDirty = true; };
    uiCtx.setAccumDirty     = [this]() { m_accumDirty = true; };
    m_editorUI = std::make_unique<EditorUI>(uiCtx);
}

// ── Init: Vulkan ──────────────────────────────────────────────────

void Application::initVulkan() {
    LOG_INFO("Initializing Vulkan...");

    WindowConfig windowConfig;
    windowConfig.width = m_windowWidth;
    windowConfig.height = m_windowHeight;
    windowConfig.title = "GPU Hardware Path Tracer";
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
    m_descriptorManager.createLayouts(GeometryManager::kMaxTextures);

    createDepthResources();
    createFramebuffers();
    createCommandPool();
    createCommandBuffers();
    createUniformBuffers();

    uint32_t fif = static_cast<uint32_t>(m_framesInFlight);
    m_sceneManager.setFramesInFlight(fif);
    m_geometryManager.setFramesInFlight(fif);

    m_sceneManager.createLightBuffers(*m_vulkanDevice);
    m_sceneManager.createMaterialBuffers(*m_vulkanDevice);

    createRtStorageImage();
    createAccumBuffer();
    createSyncObjects();

    m_geometryManager.createDummyTexture(*m_vulkanDevice);
    m_geometryManager.createTextures(*m_vulkanDevice);
    m_geometryManager.createEnvmap(*m_vulkanDevice);
    m_geometryManager.createGeometryBuffers(*m_vulkanDevice,
        m_sceneManager.modelRefs(),
        m_sceneManager.modelRefSourceIdx(),
        m_sceneManager.modelRefTransforms());
    createInstanceDataBuffer();
    buildAccelerationStructures();

    setupDescriptors();
    createRTPipeline();
    createSBT();
    createFullscreenPipeline();

    LOG_INFO("Full hardware RT pipeline initialized");
}

// ── Descriptor setup ──────────────────────────────────────────────

void Application::setupDescriptors() {
    uint32_t fif = static_cast<uint32_t>(m_framesInFlight);
    m_descriptorManager.createDescriptorPool(fif, GeometryManager::kMaxTextures);

    // TLAS sets (alloc only; TLAS handle written per-frame)
    m_descriptorManager.createTLASSets(fif);

    // Per-frame + scene bindings
    auto extractBuf = [](const std::vector<Buffer>& bufs) {
        std::vector<vk::Buffer> handles;
        for (auto& b : bufs) handles.push_back(*b.get());
        return handles;
    };

    std::vector<vk::Buffer> cameraHandles;
    for (auto& buf : m_cameraUniformBuffers)
        cameraHandles.push_back(*buf->get());

    std::vector<vk::ImageView> rtViews;
    for (auto& v : m_rtOutputImageViews) rtViews.push_back(*v);

    std::vector<vk::ImageView> texViews(
        GeometryManager::kMaxTextures,
        m_geometryManager.dummyTextureView());
    for (size_t t = 0; t < m_geometryManager.textures().size(); t++) {
        if (m_geometryManager.textures()[t])
            texViews[t] = *m_geometryManager.textures()[t]->getImageView();
    }

    PtDescriptorBindings b;
    b.cameraBuffers    = &cameraHandles;
    b.lightBuffers     = nullptr; // filled below
    b.rtImageViews     = &rtViews;
    b.instanceDataBuffer = *m_instanceDataBuffer->get();
    b.accumImageView   = *m_accumImageView;
    b.materialBuffers  = nullptr;
    b.vertexBuffers    = nullptr;
    b.indexBuffers     = nullptr;
    b.modelRefBuffers  = nullptr;
    b.maxTextures      = GeometryManager::kMaxTextures;
    b.textureViews     = &texViews;
    b.dummyTextureView = m_geometryManager.dummyTextureView();
    b.textureSampler   = m_geometryManager.textureSampler();
    b.envmapInfo       = m_geometryManager.envmapTexture()->getDescriptorInfo();

    auto lightHandles    = extractBuf(m_sceneManager.lightBuffers());
    auto matHandles      = extractBuf(m_sceneManager.materialBuffers());
    auto vertHandles     = extractBuf(m_geometryManager.vertexBuffers());
    auto idxHandles      = extractBuf(m_geometryManager.indexBuffers());
    auto mrHandles       = extractBuf(m_geometryManager.modelRefBuffers());

    b.lightBuffers    = &lightHandles;
    b.materialBuffers = &matHandles;
    b.vertexBuffers   = &vertHandles;
    b.indexBuffers    = &idxHandles;
    b.modelRefBuffers = &mrHandles;

    m_descriptorManager.createPerFrameSets(fif, b);
    m_descriptorManager.createSceneSets(fif, b);

    m_descriptorManager.createFullscreenSets(fif, rtViews, *m_rtOutputSampler);
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

// ── AS / instance data ────────────────────────────────────────────

void Application::buildAccelerationStructures() {
    LOG_INFO("Building acceleration structures...");

    if (m_sphereGeomVerts.empty()) {
        auto [verts, indices] = tessellateSphere(glm::vec3(0.0f), 1.0f, 3);
        m_sphereGeomVerts = std::move(verts);
        m_sphereGeomIndices = std::move(indices);
        LOG_INFO("Unit sphere tessellated: " + std::to_string(m_sphereGeomVerts.size())
                 + " verts, " + std::to_string(m_sphereGeomIndices.size()) + " indices");
    }

    for (auto& blas : m_blases) destroyBLAS(*m_vulkanDevice->get(), blas);
    m_blases.clear();
    destroyBLAS(*m_vulkanDevice->get(), m_sphereUnitBLAS);
    destroyTLAS(*m_vulkanDevice->get(), m_tlas);

    std::vector<GPUVertex> sphereGPUverts;
    for (const auto& v : m_sphereGeomVerts) {
        GPUVertex gv;
        gv.position = v;
        gv.normal = glm::normalize(v);
        gv.texCoord = glm::vec2(0.0f);
        gv._pad[0] = 0.0f; gv._pad[1] = 0.0f;
        sphereGPUverts.push_back(gv);
    }
    vk::DeviceSize sphereVertSize = sphereGPUverts.size() * sizeof(GPUVertex);
    vk::DeviceSize sphereIdxSize = m_sphereGeomIndices.size() * sizeof(uint32_t);

    auto cmd = m_commandManager->beginSingleTimeCommands();

    // Sphere vertex buffer
    vk::raii::Buffer sphereVertBuf = nullptr;
    vk::raii::DeviceMemory sphereVertMem = nullptr;
    {
        vk::BufferCreateInfo bufInfo;
        bufInfo.setSize(sphereVertSize);
        bufInfo.setUsage(vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR
                       | vk::BufferUsageFlagBits::eShaderDeviceAddress
                       | vk::BufferUsageFlagBits::eStorageBuffer
                       | vk::BufferUsageFlagBits::eTransferDst);
        bufInfo.setSharingMode(vk::SharingMode::eExclusive);
        sphereVertBuf = vk::raii::Buffer(m_vulkanDevice->get(), bufInfo);
        auto memReqs = sphereVertBuf.getMemoryRequirements();
        vk::MemoryAllocateFlagsInfo flagsInfo{vk::MemoryAllocateFlagBits::eDeviceAddress};
        vk::MemoryAllocateInfo aInfo;
        aInfo.setAllocationSize(memReqs.size);
        aInfo.setMemoryTypeIndex(m_vulkanDevice->findMemoryType(memReqs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal));
        aInfo.setPNext(&flagsInfo);
        sphereVertMem = vk::raii::DeviceMemory(m_vulkanDevice->get(), aInfo);
        sphereVertBuf.bindMemory(*sphereVertMem, 0);
    }
    // Staging buffer must live until after cmd submit+waitIdle below
    Buffer stagingVert(m_vulkanDevice.get(), {sphereVertSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent});
    stagingVert.copyFrom(sphereGPUverts.data(), sphereVertSize);
    cmd.copyBuffer(*stagingVert.get(), *sphereVertBuf, vk::BufferCopy{0, 0, sphereVertSize});

    // Sphere index buffer
    vk::raii::Buffer sphereIdxBuf = nullptr;
    vk::raii::DeviceMemory sphereIdxMem = nullptr;
    {
        vk::BufferCreateInfo bufInfo;
        bufInfo.setSize(sphereIdxSize);
        bufInfo.setUsage(vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR
                       | vk::BufferUsageFlagBits::eShaderDeviceAddress
                       | vk::BufferUsageFlagBits::eStorageBuffer
                       | vk::BufferUsageFlagBits::eTransferDst);
        bufInfo.setSharingMode(vk::SharingMode::eExclusive);
        sphereIdxBuf = vk::raii::Buffer(m_vulkanDevice->get(), bufInfo);
        auto memReqs = sphereIdxBuf.getMemoryRequirements();
        vk::MemoryAllocateFlagsInfo flagsInfo{vk::MemoryAllocateFlagBits::eDeviceAddress};
        vk::MemoryAllocateInfo aInfo;
        aInfo.setAllocationSize(memReqs.size);
        aInfo.setMemoryTypeIndex(m_vulkanDevice->findMemoryType(memReqs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal));
        aInfo.setPNext(&flagsInfo);
        sphereIdxMem = vk::raii::DeviceMemory(m_vulkanDevice->get(), aInfo);
        sphereIdxBuf.bindMemory(*sphereIdxMem, 0);
    }
    Buffer stagingIdx(m_vulkanDevice.get(), {sphereIdxSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent});
    stagingIdx.copyFrom(m_sphereGeomIndices.data(), sphereIdxSize);
    cmd.copyBuffer(*stagingIdx.get(), *sphereIdxBuf, vk::BufferCopy{0, 0, sphereIdxSize});

    // Barrier
    {
        vk::MemoryBarrier uploadBarrier;
        uploadBarrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
        uploadBarrier.setDstAccessMask(vk::AccessFlagBits::eAccelerationStructureReadKHR);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            {}, uploadBarrier, nullptr, nullptr);
    }
    cmd.end();
    {
        vk::SubmitInfo submitInfo;
        submitInfo.setCommandBuffers(*cmd);
        vk::raii::Fence fence(m_vulkanDevice->get(), vk::FenceCreateInfo{});
        m_vulkanDevice->getGraphicsQueue().submit(submitInfo, *fence);
        (void)m_vulkanDevice->get().waitForFences(*fence, true, UINT64_MAX);
    }

    vk::DeviceAddress sphereVertAddr = m_vulkanDevice->get().getBufferAddress({*sphereVertBuf});
    vk::DeviceAddress sphereIdxAddr  = m_vulkanDevice->get().getBufferAddress({*sphereIdxBuf});

    m_sphereUnitBLAS = buildBLAS(m_vulkanDevice.get(),
        *sphereVertBuf, static_cast<uint32_t>(m_sphereGeomVerts.size()),
        *sphereIdxBuf, 0, static_cast<uint32_t>(m_sphereGeomIndices.size()),
        sphereVertAddr, sphereIdxAddr);

    // Model BLAS
    if (!m_geometryManager.vertexBuffers().empty() && !m_geometryManager.indexBuffers().empty()) {
        vk::DeviceAddress baseVertAddr = m_vulkanDevice->get().getBufferAddress(
            {*m_geometryManager.vertexBuffers()[0].get()});
        vk::DeviceAddress baseIdxAddr = m_vulkanDevice->get().getBufferAddress(
            {*m_geometryManager.indexBuffers()[0].get()});

        for (size_t s = 0; s < m_geometryManager.modelSources().size(); s++) {
            const auto& src = m_geometryManager.modelSources()[s];
            vk::DeviceAddress srcVertAddr = baseVertAddr + src.vertexOffset * sizeof(GPUVertex);
            BLAS blas = buildBLAS(m_vulkanDevice.get(),
                *m_geometryManager.vertexBuffers()[0].get(),
                static_cast<uint32_t>(src.positions.size()),
                *m_geometryManager.indexBuffers()[0].get(),
                src.firstIndex, src.indexCount, srcVertAddr, baseIdxAddr);
            m_blases.push_back(std::move(blas));
        }
    }
    LOG_INFO("All BLAS built (" + std::to_string(m_blases.size()) + " models + unit sphere)");
}

void Application::createInstanceDataBuffer() {
    m_instanceData.clear();
    for (size_t i = 0; i < m_sceneManager.modelRefs().size(); i++) {
        InstanceData id;
        id.materialId = m_sceneManager.modelRefs()[i].materialId;
        id.textureIndex = m_sceneManager.modelRefs()[i].textureIndex >= 0
            ? static_cast<uint32_t>(m_sceneManager.modelRefs()[i].textureIndex) : ~0u;
        id.modelRefIndex = static_cast<uint32_t>(i);
        id._pad = 0;
        m_instanceData.push_back(id);
    }
    for (size_t i = 0; i < m_sceneManager.spheres().size(); i++) {
        InstanceData id;
        id.materialId = m_sceneManager.spheres()[i].materialIndex;
        id.textureIndex = ~0u;
        id.modelRefIndex = ~0u;
        id._pad = 0;
        m_instanceData.push_back(id);
    }
    vk::DeviceSize bufSize = std::max<vk::DeviceSize>(
        sizeof(InstanceData) * m_instanceData.size(), 256);
    m_instanceDataBuffer = std::make_unique<Buffer>(
        Buffer::createBuffer(m_vulkanDevice.get(), bufSize,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent));
    m_instanceDataBuffer->copyFrom(m_instanceData.data(),
        m_instanceData.size() * sizeof(InstanceData));
    m_instanceDataDirty = static_cast<int>(m_framesInFlight);
    LOG_INFO("Instance data buffer: " + std::to_string(m_instanceData.size()) + " instances");
}

// ── RT Pipeline + SBT ─────────────────────────────────────────────

void Application::createRTPipeline() {
    LOG_INFO("Creating ray tracing pipeline...");
    m_rgenShader  = std::make_unique<ShaderModule>(
        ShaderModule::createRayGenShader(m_vulkanDevice.get(), "shaders/pathtracer.rgen.spv"));
    m_rchitShader = std::make_unique<ShaderModule>(
        ShaderModule::createClosestHitShader(m_vulkanDevice.get(), "shaders/pathtracer.rchit.spv"));
    m_rmissShader = std::make_unique<ShaderModule>(
        ShaderModule::createMissShader(m_vulkanDevice.get(), "shaders/pathtracer.rmiss.spv"));
    m_smissShader = std::make_unique<ShaderModule>(
        ShaderModule::createMissShader(m_vulkanDevice.get(), "shaders/pathtracer.smiss.spv"));

    std::array<vk::DescriptorSetLayout, 3> setLayouts = {
        m_descriptorManager.tlasLayout(),
        m_descriptorManager.perFrameLayout(),
        m_descriptorManager.sceneLayout()
    };
    vk::PushConstantRange pcRange;
    pcRange.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR
                        | vk::ShaderStageFlagBits::eClosestHitKHR
                        | vk::ShaderStageFlagBits::eMissKHR);
    pcRange.setSize(sizeof(RTGlobalConstants));
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setSetLayouts(setLayouts);
    pipelineLayoutInfo.setPushConstantRanges(pcRange);
    m_rtPipelineLayout = m_vulkanDevice->get().createPipelineLayout(pipelineLayoutInfo);

    std::array<vk::PipelineShaderStageCreateInfo, 4> stages;
    stages[0].setStage(vk::ShaderStageFlagBits::eRaygenKHR);
    stages[0].setModule(*m_rgenShader->get()); stages[0].setPName("rayGen");
    stages[1].setStage(vk::ShaderStageFlagBits::eMissKHR);
    stages[1].setModule(*m_rmissShader->get()); stages[1].setPName("miss");
    stages[2].setStage(vk::ShaderStageFlagBits::eMissKHR);
    stages[2].setModule(*m_smissShader->get()); stages[2].setPName("shadowMiss");
    stages[3].setStage(vk::ShaderStageFlagBits::eClosestHitKHR);
    stages[3].setModule(*m_rchitShader->get()); stages[3].setPName("closestHit");

    std::array<vk::RayTracingShaderGroupCreateInfoKHR, 4> groups;
    for (auto& g : groups) {
        g.setGeneralShader(VK_SHADER_UNUSED_KHR);
        g.setAnyHitShader(VK_SHADER_UNUSED_KHR);
        g.setClosestHitShader(VK_SHADER_UNUSED_KHR);
        g.setIntersectionShader(VK_SHADER_UNUSED_KHR);
    }
    groups[0].setType(vk::RayTracingShaderGroupTypeKHR::eGeneral); groups[0].setGeneralShader(0);
    groups[1].setType(vk::RayTracingShaderGroupTypeKHR::eGeneral); groups[1].setGeneralShader(1);
    groups[2].setType(vk::RayTracingShaderGroupTypeKHR::eGeneral); groups[2].setGeneralShader(2);
    groups[3].setType(vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup);
    groups[3].setClosestHitShader(3);

    vk::RayTracingPipelineCreateInfoKHR pipelineInfo;
    pipelineInfo.setStages(stages); pipelineInfo.setGroups(groups);
    pipelineInfo.setMaxPipelineRayRecursionDepth(4);
    pipelineInfo.setLayout(*m_rtPipelineLayout);
    auto result = m_vulkanDevice->get().createRayTracingPipelinesKHR(nullptr, nullptr, pipelineInfo);
    m_rtPipeline = std::move(result[0]);

    auto& rtProps = m_vulkanDevice->getRTPipelineProps();
    uint32_t handleSize = rtProps.shaderGroupHandleSize;
    // Stride must be aligned to shaderGroupBaseAlignment so that each SBT
    // region's deviceAddress is a multiple of shaderGroupBaseAlignment.
    uint32_t baseAlign = rtProps.shaderGroupBaseAlignment;
    m_sbtStride = (handleSize + baseAlign - 1) & ~(baseAlign - 1);
    m_sbtHitGroupOffset = 3 * m_sbtStride;
    m_sbtMissOffset = 1 * m_sbtStride;
    m_sbtShadowMissOffset = 2 * m_sbtStride;
    LOG_INFO("RT pipeline created");
}

void Application::createSBT() {
    auto& rtProps = m_vulkanDevice->getRTPipelineProps();
    uint32_t handleSize = rtProps.shaderGroupHandleSize;
    uint32_t groupCount = 4;
    vk::DeviceSize sbtSize = groupCount * m_sbtStride;

    m_sbtBuffer = std::make_unique<Buffer>(
        Buffer::createBuffer(m_vulkanDevice.get(), sbtSize,
            vk::BufferUsageFlagBits::eShaderBindingTableKHR
            | vk::BufferUsageFlagBits::eTransferDst
            | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            vk::MemoryPropertyFlagBits::eDeviceLocal));

    auto handles = m_rtPipeline.getRayTracingShaderGroupHandlesKHR<uint8_t>(0, groupCount, sbtSize);
    Buffer staging(m_vulkanDevice.get(), {sbtSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent});

    uint8_t* mapped = static_cast<uint8_t*>(staging.map(0, sbtSize));
    memset(mapped, 0, sbtSize);
    for (uint32_t g = 0; g < groupCount; g++)
        memcpy(mapped + g * m_sbtStride, handles.data() + g * handleSize, handleSize);
    staging.unmap();

    auto& cmd = m_commandBuffers[0];
    cmd.reset();
    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cmd.begin(beginInfo);
    cmd.copyBuffer(*staging.get(), *m_sbtBuffer->get(), vk::BufferCopy{0, 0, sbtSize});
    cmd.end();
    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*cmd);
    m_vulkanDevice->getGraphicsQueue().submit(submitInfo, nullptr);
    m_vulkanDevice->waitIdle();
    LOG_INFO("SBT created: " + std::to_string(groupCount) + " groups");
}

// ── RT output + accum ────────────────────────────────────────────

void Application::createRtStorageImage() {
    vk::ImageCreateInfo imageInfo;
    imageInfo.setImageType(vk::ImageType::e2D);
    imageInfo.setExtent(vk::Extent3D{static_cast<uint32_t>(m_windowWidth), static_cast<uint32_t>(m_windowHeight), 1});
    imageInfo.setMipLevels(1); imageInfo.setArrayLayers(1);
    imageInfo.setFormat(vk::Format::eR8G8B8A8Unorm);
    imageInfo.setTiling(vk::ImageTiling::eOptimal);
    imageInfo.setInitialLayout(vk::ImageLayout::eUndefined);
    imageInfo.setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled
                       | vk::ImageUsageFlagBits::eTransferDst);
    imageInfo.setSharingMode(vk::SharingMode::eExclusive);
    imageInfo.setSamples(vk::SampleCountFlagBits::e1);

    for (size_t i = 0; i < m_framesInFlight; i++) {
        m_rtOutputImages.emplace_back(m_vulkanDevice->get().createImage(imageInfo));
        auto memReqs = m_rtOutputImages[i].getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo;
        allocInfo.setAllocationSize(memReqs.size);
        allocInfo.setMemoryTypeIndex(m_vulkanDevice->findMemoryType(memReqs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal));
        m_rtOutputImagesMemory.emplace_back(m_vulkanDevice->get().allocateMemory(allocInfo));
        m_rtOutputImages[i].bindMemory(*m_rtOutputImagesMemory[i], 0);

        auto& cmd = m_commandBuffers[0];
        cmd.reset();
        vk::CommandBufferBeginInfo beginInfo;
        cmd.begin(beginInfo);
        vk::ImageMemoryBarrier barrier;
        barrier.setOldLayout(vk::ImageLayout::eUndefined);
        barrier.setNewLayout(vk::ImageLayout::eGeneral);
        barrier.setImage(*m_rtOutputImages[i]);
        barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        barrier.setDstAccessMask(vk::AccessFlagBits::eShaderWrite);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eRayTracingShaderKHR,
            {}, nullptr, nullptr, barrier);
        cmd.end();
        vk::SubmitInfo submitInfo;
        submitInfo.setCommandBuffers(*m_commandBuffers[0]);
        m_vulkanDevice->getGraphicsQueue().submit(submitInfo, nullptr);
        m_vulkanDevice->waitIdle();

        vk::ImageViewCreateInfo viewInfo;
        viewInfo.setImage(*m_rtOutputImages[i]);
        viewInfo.setViewType(vk::ImageViewType::e2D);
        viewInfo.setFormat(vk::Format::eR8G8B8A8Unorm);
        viewInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        m_rtOutputImageViews.emplace_back(m_vulkanDevice->get().createImageView(viewInfo));
    }
    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.setMagFilter(vk::Filter::eLinear); samplerInfo.setMinFilter(vk::Filter::eLinear);
    samplerInfo.setAddressModeU(vk::SamplerAddressMode::eClampToEdge);
    samplerInfo.setAddressModeV(vk::SamplerAddressMode::eClampToEdge);
    samplerInfo.setAddressModeW(vk::SamplerAddressMode::eClampToEdge);
    m_rtOutputSampler = m_vulkanDevice->get().createSampler(samplerInfo);
    LOG_INFO("RT storage image created: " + std::to_string(m_windowWidth) + "x" + std::to_string(m_windowHeight));
}

void Application::createAccumBuffer() {
    vk::ImageCreateInfo imageInfo;
    imageInfo.setImageType(vk::ImageType::e2D);
    imageInfo.setExtent(vk::Extent3D{static_cast<uint32_t>(m_windowWidth), static_cast<uint32_t>(m_windowHeight), 1});
    imageInfo.setMipLevels(1); imageInfo.setArrayLayers(1);
    imageInfo.setFormat(vk::Format::eR32G32B32A32Sfloat);
    imageInfo.setTiling(vk::ImageTiling::eOptimal);
    imageInfo.setInitialLayout(vk::ImageLayout::eUndefined);
    imageInfo.setUsage(vk::ImageUsageFlagBits::eStorage);
    imageInfo.setSharingMode(vk::SharingMode::eExclusive);
    imageInfo.setSamples(vk::SampleCountFlagBits::e1);
    m_accumImage = m_vulkanDevice->get().createImage(imageInfo);
    auto memReqs = m_accumImage.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo;
    allocInfo.setAllocationSize(memReqs.size);
    allocInfo.setMemoryTypeIndex(m_vulkanDevice->findMemoryType(memReqs.memoryTypeBits,
        vk::MemoryPropertyFlagBits::eDeviceLocal));
    m_accumImageMemory = m_vulkanDevice->get().allocateMemory(allocInfo);
    m_accumImage.bindMemory(*m_accumImageMemory, 0);

    auto& cmd = m_commandBuffers[0];
    cmd.reset();
    vk::CommandBufferBeginInfo beginInfo;
    cmd.begin(beginInfo);
    vk::ImageMemoryBarrier barrier;
    barrier.setOldLayout(vk::ImageLayout::eUndefined);
    barrier.setNewLayout(vk::ImageLayout::eGeneral);
    barrier.setImage(*m_accumImage);
    barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    barrier.setDstAccessMask(vk::AccessFlagBits::eShaderWrite);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eRayTracingShaderKHR,
        {}, nullptr, nullptr, barrier);
    cmd.end();
    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*m_commandBuffers[0]);
    m_vulkanDevice->getGraphicsQueue().submit(submitInfo, nullptr);
    m_vulkanDevice->waitIdle();

    vk::ImageViewCreateInfo viewInfo;
    viewInfo.setImage(*m_accumImage);
    viewInfo.setViewType(vk::ImageViewType::e2D);
    viewInfo.setFormat(vk::Format::eR32G32B32A32Sfloat);
    viewInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    m_accumImageView = m_vulkanDevice->get().createImageView(viewInfo);
    LOG_INFO("Accumulation buffer created");
}

// ── Fullscreen pipeline ──────────────────────────────────────────

void Application::createFullscreenPipeline() {
    std::string shaderPath = "shaders/fullscreen.spv";
    m_fullscreenVertShader = std::make_unique<ShaderModule>(
        ShaderModule::createVertexShader(m_vulkanDevice.get(), shaderPath));
    m_fullscreenFragShader = std::make_unique<ShaderModule>(
        ShaderModule::createFragmentShader(m_vulkanDevice.get(), shaderPath));

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setSetLayouts(m_descriptorManager.samplerLayout());
    m_fullscreenPipelineLayout = m_vulkanDevice->get().createPipelineLayout(pipelineLayoutInfo);

    if (!m_pipelineManager)
        m_pipelineManager = std::make_unique<PipelineManager>(m_vulkanDevice->get());

    PipelineConfig config;
    config.vertexShader = *m_fullscreenVertShader->get();
    config.fragmentShader = *m_fullscreenFragShader->get();
    config.vertexEntryPoint = "vertMain";
    config.fragmentEntryPoint = "fragMain";
    config.pipelineLayout = *m_fullscreenPipelineLayout;
    config.renderPass = *m_renderPassManager->get();
    config.topology = vk::PrimitiveTopology::eTriangleList;
    config.depthTestEnable = false; config.depthWriteEnable = false;
    config.blendEnable = false;
    config.vertexBindingDescription = vk::VertexInputBindingDescription{};
    m_pipelineManager->createPipeline("fullscreen", config);
    LOG_INFO("Fullscreen pipeline created");
}

// ── Vulkan boilerplate ───────────────────────────────────────────

void Application::createInstance() {
    InstanceConfig config;
    config.applicationName = "GPU Path Tracer";
    config.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    config.engineName = "GPU Path Tracer";
    config.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    config.apiVersion = VK_API_VERSION_1_3;
    config.enableValidation = Validation::shouldEnableValidation();
    m_vulkanInstance = std::make_unique<VulkanInstance>(config);
    LOG_INFO("Vulkan instance created");
}

void Application::createDevice() {
    m_windowManager->createSurface(m_vulkanInstance->get());
    DeviceConfig deviceConfig;
    deviceConfig.requiredFeatures.samplerAnisotropy = true;
    deviceConfig.requireRayTracing = true;
    m_vulkanDevice = std::make_unique<VulkanDevice>(m_vulkanInstance->get(),
        m_windowManager->getSurface(), deviceConfig);
    LOG_INFO("Vulkan device created with ray tracing");
}

void Application::createSwapChain() {
    m_swapChainManager = std::make_unique<SwapChainManager>(
        *m_vulkanDevice, m_windowManager->getSurface(), m_windowWidth, m_windowHeight);
}

void Application::createRenderPass() {
    m_depthFormat = findDepthFormat({vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint});
    RenderPassConfig config;
    config.colorFormat = m_swapChainManager->getImageFormat();
    config.depthFormat = m_depthFormat;
    config.clearColors = true; config.clearDepth = true;
    m_renderPassManager = std::make_unique<RenderPassManager>(m_vulkanDevice->get(), config);
}

void Application::createFramebuffers() {
    m_swapChainFramebuffers.clear();
    auto& imageViews = m_swapChainManager->getImageViews();
    for (size_t i = 0; i < imageViews.size(); i++) {
        std::vector<vk::ImageView> attachments = {*imageViews[i], *m_depthImageView};
        vk::FramebufferCreateInfo info;
        info.setRenderPass(*m_renderPassManager->get());
        info.setAttachments(attachments);
        info.setWidth(m_swapChainManager->getExtent().width);
        info.setHeight(m_swapChainManager->getExtent().height);
        info.setLayers(1);
        m_swapChainFramebuffers.emplace_back(m_vulkanDevice->get().createFramebuffer(info));
    }
}

void Application::createCommandPool() {
    CommandPoolConfig config;
    config.queueFamilyIndex = m_vulkanDevice->getGraphicsQueueFamily();
    m_commandManager = std::make_unique<CommandManager>(m_vulkanDevice->get(), config);
}

void Application::createDepthResources() {
    vk::ImageCreateInfo imageInfo;
    imageInfo.setImageType(vk::ImageType::e2D);
    imageInfo.setExtent(vk::Extent3D{m_swapChainManager->getExtent().width,
        m_swapChainManager->getExtent().height, 1});
    imageInfo.setMipLevels(1); imageInfo.setArrayLayers(1);
    imageInfo.setFormat(m_depthFormat);
    imageInfo.setTiling(vk::ImageTiling::eOptimal);
    imageInfo.setInitialLayout(vk::ImageLayout::eUndefined);
    imageInfo.setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment);
    imageInfo.setSharingMode(vk::SharingMode::eExclusive);
    imageInfo.setSamples(vk::SampleCountFlagBits::e1);
    m_depthImage = m_vulkanDevice->get().createImage(imageInfo);
    auto memReqs = m_depthImage.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo;
    allocInfo.setAllocationSize(memReqs.size);
    allocInfo.setMemoryTypeIndex(m_vulkanDevice->findMemoryType(memReqs.memoryTypeBits,
        vk::MemoryPropertyFlagBits::eDeviceLocal));
    m_depthImageMemory = m_vulkanDevice->get().allocateMemory(allocInfo);
    m_depthImage.bindMemory(*m_depthImageMemory, 0);
    vk::ImageViewCreateInfo viewInfo;
    viewInfo.setImage(*m_depthImage);
    viewInfo.setViewType(vk::ImageViewType::e2D);
    viewInfo.setFormat(m_depthFormat);
    viewInfo.subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};
    m_depthImageView = m_vulkanDevice->get().createImageView(viewInfo);
}

void Application::createCommandBuffers() {
    m_commandBuffers = m_commandManager->allocateCommandBuffers(m_swapChainFramebuffers.size());
}

void Application::createSyncObjects() {
    m_framesInFlight = m_swapChainManager->getImageCount();
    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();
    m_imagesInFlight.resize(m_framesInFlight, nullptr);
    for (size_t i = 0; i < m_framesInFlight; i++) {
        m_imageAvailableSemaphores.emplace_back(m_vulkanDevice->get().createSemaphore({}));
        m_inFlightFences.emplace_back(m_vulkanDevice->get().createFence(
            {vk::FenceCreateFlagBits::eSignaled}));
    }
    // renderFinishedSemaphores are indexed by swapchain image index (one per image)
    uint32_t imageCount = m_swapChainManager->getImageCount();
    for (size_t i = 0; i < imageCount; i++) {
        m_renderFinishedSemaphores.emplace_back(m_vulkanDevice->get().createSemaphore({}));
    }
}

void Application::createUniformBuffers() {
    m_framesInFlight = m_swapChainManager->getImageCount();
    createUniformBuffersImpl(sizeof(CameraData), m_cameraUniformBuffers, m_mappedCameraUniformData);
}

void Application::createUniformBuffersImpl(vk::DeviceSize bufferSize,
    std::vector<std::unique_ptr<Buffer>>& bufferOut, std::vector<void*>& mappedDataOut) const {
    bufferOut.clear(); mappedDataOut.clear();
    bufferOut.reserve(m_framesInFlight); mappedDataOut.reserve(m_framesInFlight);
    for (size_t i = 0; i < m_framesInFlight; i++) {
        bufferOut.emplace_back(std::make_unique<Buffer>(
            Buffer::createUniformBuffer(m_vulkanDevice.get(), bufferSize)));
        mappedDataOut.push_back(bufferOut[i]->map(0, bufferSize));
    }
}

// ── Uniform update ───────────────────────────────────────────────

void Application::updateUniformBuffer(size_t currentFrame) {
    glm::mat4 viewProj = m_sceneManager.camera().GetViewProj()
                       * glm::inverse(m_sceneManager.cameraTransform()());
    CameraData data{viewProj, glm::inverse(viewProj),
                    glm::vec4(m_sceneManager.cameraTransform().translation, 0.0f)};
    memcpy(m_mappedCameraUniformData[currentFrame], &data, sizeof(data));
}

// ── Frame rendering (decomposed) ─────────────────────────────────

void Application::drawFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    m_editorUI->draw();
    ImGui::Render();

    uint32_t imageIndex;
    if (!beginFrame(imageIndex)) return;

    rebuildTLASIfNeeded();
    recordRTPass();
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
    if (outImageIndex == UINT32_MAX) { recreateSwapChain(); return false; }
    if (m_imagesInFlight[outImageIndex]) {
        (void)m_vulkanDevice->get().waitForFences(
            m_imagesInFlight[outImageIndex], true, UINT64_MAX);
    }
    m_imagesInFlight[outImageIndex] = *m_inFlightFences[m_currentFrame];

    updateUniformBuffer(m_currentFrame);
    m_sceneManager.updateLightBuffer(m_currentFrame);
    m_sceneManager.updateMaterialBuffer(m_currentFrame);
    if (m_sceneManager.isModelRefsDirty())
        m_geometryManager.updateModelRefBuffer(m_currentFrame, m_sceneManager.modelRefs());

    m_commandBuffers[m_currentFrame].reset();
    vk::CommandBufferBeginInfo beginInfo;
    m_commandBuffers[m_currentFrame].begin(beginInfo);
    return true;
}

void Application::rebuildTLASIfNeeded() {
    if (!m_tlasNeedsRebuild) return;
    m_tlasNeedsRebuild = false;

    std::vector<VkAccelerationStructureInstanceKHR> instances;
    for (size_t i = 0; i < m_sceneManager.modelRefs().size() && i < m_blases.size(); i++) {
        int srcIdx = m_sceneManager.modelRefSourceIdx()[i];
        if (srcIdx < 0 || srcIdx >= static_cast<int>(m_blases.size())) continue;
        instances.push_back(makeInstance(m_blases[srcIdx],
            m_sceneManager.modelRefTransforms()[i].transform(), static_cast<uint32_t>(i)));
    }
    uint32_t sphereBase = static_cast<uint32_t>(m_sceneManager.modelRefs().size());
    for (size_t s = 0; s < m_sceneManager.spheres().size(); s++) {
        glm::mat4 xform = glm::translate(glm::mat4(1.0f), m_sceneManager.spheres()[s].center)
                        * glm::scale(glm::mat4(1.0f), glm::vec3(m_sceneManager.spheres()[s].radius));
        instances.push_back(makeInstance(m_sphereUnitBLAS, xform, sphereBase + static_cast<uint32_t>(s)));
    }

    if (instances.empty()) {
        LOG_WARNING("No instances for TLAS — no models or spheres loaded. "
                    "Place model files in assets/models/ as referenced by SceneConfig.xml.");
        if (m_tlas.handle) {
            m_retiredTLAS.push_back(std::move(m_tlas));
            m_tlas = TLAS{};
        }
        return;
    }

    TLAS newTLAS = buildTLAS(m_vulkanDevice.get(), instances);
    if (m_tlas.handle) m_retiredTLAS.push_back(std::move(m_tlas));
    m_tlas = std::move(newTLAS);
}

void Application::recordRTPass() {
    auto& cmd = m_commandBuffers[m_currentFrame];

    // If TLAS is null (no geometry loaded), skip ray tracing and clear output to black
    if (!m_tlas.handle) {
        vk::ClearColorValue black{0.0f, 0.0f, 0.0f, 1.0f};
        vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        cmd.clearColorImage(*m_rtOutputImages[m_currentFrame],
            vk::ImageLayout::eGeneral, black, range);
        return;
    }

    // Write TLAS descriptor
    m_descriptorManager.writeTLASBinding(m_currentFrame, m_tlas.handle);

    // Barrier: AS build → RT
    {
        vk::MemoryBarrier barrier;
        barrier.setSrcAccessMask(vk::AccessFlagBits::eAccelerationStructureWriteKHR);
        barrier.setDstAccessMask(vk::AccessFlagBits::eAccelerationStructureReadKHR);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
                            vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                            {}, barrier, nullptr, nullptr);
    }

    cmd.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *m_rtPipeline);

    std::array<vk::DescriptorSet, 3> rtSets = {
        *m_descriptorManager.tlasSets()[m_currentFrame],
        *m_descriptorManager.perFrameSets()[m_currentFrame],
        *m_descriptorManager.sceneSets()[m_currentFrame]
    };
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR,
                           *m_rtPipelineLayout, 0, rtSets, nullptr);

    if (m_accumDirty) { m_totalAccumSamples = 0; m_accumDirty = false; }

    RTGlobalConstants pc{};
    pc.lightCount = static_cast<uint32_t>(m_sceneManager.lights().size());
    pc.materialCount = static_cast<uint32_t>(m_sceneManager.materials().size());
    pc.modelRefCount = static_cast<uint32_t>(m_sceneManager.modelRefs().size());
    pc.sphereCount = static_cast<uint32_t>(m_sceneManager.spheres().size());
    pc.sphereInstanceBase = static_cast<uint32_t>(m_sceneManager.modelRefs().size());
    pc.totalAccumSamples = m_totalAccumSamples++;
    pc.ambientStrength = m_sceneManager.ambientStrength();
    cmd.pushConstants<RTGlobalConstants>(*m_rtPipelineLayout,
        vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eMissKHR,
        0, pc);

    vk::DeviceAddress sbtAddr = m_vulkanDevice->get().getBufferAddress({*m_sbtBuffer->get()});
    vk::StridedDeviceAddressRegionKHR raygenRegion{sbtAddr, m_sbtStride, m_sbtStride};
    vk::StridedDeviceAddressRegionKHR missRegion{sbtAddr + m_sbtMissOffset, m_sbtStride, 2 * m_sbtStride};
    vk::StridedDeviceAddressRegionKHR hitRegion{sbtAddr + m_sbtHitGroupOffset, m_sbtStride, m_sbtStride};
    vk::StridedDeviceAddressRegionKHR callableRegion{0, 0, 0};

    cmd.traceRaysKHR(raygenRegion, missRegion, hitRegion, callableRegion,
                     m_windowWidth, m_windowHeight, 1);

    // Barrier: RT write → fragment read
    {
        vk::ImageMemoryBarrier barrier;
        barrier.setOldLayout(vk::ImageLayout::eGeneral);
        barrier.setNewLayout(vk::ImageLayout::eGeneral);
        barrier.setImage(*m_rtOutputImages[m_currentFrame]);
        barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        barrier.setSrcAccessMask(vk::AccessFlagBits::eShaderWrite);
        barrier.setDstAccessMask(vk::AccessFlagBits::eShaderRead);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                            vk::PipelineStageFlagBits::eFragmentShader,
                            {}, nullptr, nullptr, barrier);
    }
}

void Application::recordGraphicsPass(uint32_t imageIndex) {
    auto& cmd = m_commandBuffers[m_currentFrame];

    vk::Viewport viewport;
    viewport.setWidth(static_cast<float>(m_swapChainManager->getExtent().width));
    viewport.setHeight(static_cast<float>(m_swapChainManager->getExtent().height));
    viewport.setMinDepth(0.0f); viewport.setMaxDepth(1.0f);
    cmd.setViewport(0, viewport);
    vk::Rect2D scissor{{0, 0}, m_swapChainManager->getExtent()};
    cmd.setScissor(0, scissor);

    std::array<vk::ClearValue, 2> clearValues;
    clearValues[0].color = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f};
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    vk::RenderPassBeginInfo renderPassInfo;
    renderPassInfo.setRenderPass(*m_renderPassManager->get());
    renderPassInfo.setFramebuffer(*m_swapChainFramebuffers[imageIndex]);
    renderPassInfo.setRenderArea(scissor);
    renderPassInfo.setClearValues(clearValues);

    cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                     *m_pipelineManager->getPipeline("fullscreen"));
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *m_fullscreenPipelineLayout,
                           0, *m_descriptorManager.fullscreenSets()[m_currentFrame], nullptr);
    cmd.draw(3, 1, 0, 0);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *cmd);
    cmd.endRenderPass();
}

void Application::submitFrame(uint32_t imageIndex) {
    m_commandBuffers[m_currentFrame].end();
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::SubmitInfo submitInfo;
    submitInfo.setWaitSemaphores(*m_imageAvailableSemaphores[m_currentFrame]);
    submitInfo.setWaitDstStageMask(waitStages);
    submitInfo.setCommandBuffers(*m_commandBuffers[m_currentFrame]);
    submitInfo.setSignalSemaphores(*m_renderFinishedSemaphores[imageIndex]);
    m_vulkanDevice->get().resetFences(*m_inFlightFences[m_currentFrame]);
    m_vulkanDevice->getGraphicsQueue().submit(submitInfo, *m_inFlightFences[m_currentFrame]);
    m_swapChainManager->presentImage(imageIndex, *m_renderFinishedSemaphores[imageIndex]);
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

void Application::mainLoop() {
    while (!m_windowManager->shouldClose()) {
        glfwPollEvents();
        drawFrame();
        if (m_framebufferResized) recreateSwapChain();
    }
    m_vulkanDevice->waitIdle();
}

// ── Swapchain recreation ──────────────────────────────────────────

void Application::recreateSwapChain() {
    if (m_windowWidth == 0 || m_windowHeight == 0) return;
    m_vulkanDevice->waitIdle();
    cleanupSwapChain();
    cleanupRtSwapChainResources();
    m_swapChainManager->recreate(m_windowWidth, m_windowHeight);
    m_framesInFlight = m_swapChainManager->getImageCount();
    // Recreate sync objects: renderFinishedSemaphores are per swapchain image,
    // so they must be rebuilt when image count changes.
    cleanupSyncObjects();
    createSyncObjects();
    createRenderPass();
    createDepthResources();
    createFramebuffers();
    createRtStorageImage();
    createAccumBuffer();

    for (size_t i = 0; i < m_framesInFlight; ++i) {
        m_descriptorManager.updatePerFrameImageBindings(i,
            *m_rtOutputImageViews[i], *m_accumImageView);
        m_descriptorManager.updateFullscreenBinding(i,
            *m_rtOutputImageViews[i], *m_rtOutputSampler);
    }
    createFullscreenPipeline();
    m_sceneManager.camera().SetAspectRatio(
        static_cast<float>(m_windowWidth) / m_windowHeight);
    m_framebufferResized = false;
    m_accumDirty = true;
}

vk::Format Application::findDepthFormat(const std::vector<vk::Format>& candidates) const {
    for (const vk::Format format : candidates) {
        const auto props = m_vulkanDevice->getPhysical().getFormatProperties(format);
        if (props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
            return format;
    }
    throw VulkanException(vk::Result::eErrorFormatNotSupported,
        "Failed to find supported depth format", __FUNCTION__, __FILE__, __LINE__);
}

// ── Cleanup ───────────────────────────────────────────────────────

void Application::cleanup() {
    if (m_vulkanDevice) m_vulkanDevice->waitIdle();
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
    m_rtPipeline = nullptr;
    m_sbtBuffer.reset();
    m_rgenShader.reset();
    m_rchitShader.reset();
    m_rmissShader.reset();
    m_smissShader.reset();
    m_fullscreenVertShader.reset();
    m_fullscreenFragShader.reset();
    m_cameraUniformBuffers.clear();
    m_mappedCameraUniformData.clear();
    m_commandBuffers.clear();
    // unique_ptr members destroyed in reverse declaration order after cleanup() returns
}

void Application::cleanupSwapChain() {
    m_swapChainFramebuffers.clear();
    m_depthImageView = nullptr;
    m_depthImage = nullptr;
    m_depthImageMemory = nullptr;
    m_fullscreenPipelineLayout = nullptr;
}

void Application::cleanupSyncObjects() {
    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();
    m_imagesInFlight.clear();
}

void Application::cleanupRtResources() {
    cleanupRtSwapChainResources();
    m_instanceDataBuffer.reset();
    for (auto& blas : m_blases) destroyBLAS(*m_vulkanDevice->get(), blas);
    m_blases.clear();
    destroyBLAS(*m_vulkanDevice->get(), m_sphereUnitBLAS);
    for (auto& tlas : m_retiredTLAS) destroyTLAS(*m_vulkanDevice->get(), tlas);
    m_retiredTLAS.clear();
    destroyTLAS(*m_vulkanDevice->get(), m_tlas);
}

void Application::cleanupRtSwapChainResources() {
    m_rtOutputSampler = nullptr;
    m_rtOutputImageViews.clear();
    m_rtOutputImages.clear();
    m_rtOutputImagesMemory.clear();
    m_accumImageView = nullptr;
    m_accumImage = nullptr;
    m_accumImageMemory = nullptr;
}

} // namespace RYRayTracing
