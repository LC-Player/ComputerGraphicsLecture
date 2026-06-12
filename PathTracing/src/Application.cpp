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

// ── Init Components ────────────────────────────────────────────────

void Application::initComponents() {
    m_camera.SetAspectRatio(static_cast<float>(m_windowWidth) / m_windowHeight);
    m_camera.SetPerspective(glm::radians(60.0f), 0.1f, 200.0f);
    m_cameraTransform.translation = { 0.0f, 0.0f, 0.0f };
    m_cameraTransform.rotation = { 0, 0, 0 };

    SceneConfig cfg = loadSceneConfig("assets/SceneConfig.xml");
    m_ambientStrength = cfg.ambientStrength;

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
    uiCtx.setAccumDirty = [this]() { setAccumDirty(); };
    m_editorUI = std::make_unique<EditorUI>(uiCtx);
}

// ── Init Vulkan ─────────────────────────────────────────────────────

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
    createDescriptorSetLayouts();
    createDepthResources();
    createFramebuffers();
    createCommandPool();
    createCommandBuffers();
    createUniformBuffers();
    createLightBuffer();
    createMaterialBuffer();
    createRtStorageImage();
    createAccumBuffer();
    createSyncObjects();

    // Create scene resources before descriptor sets
    createDummyTexture();
    createTextures();
    createEnvmap();
    createModelDataBuffers();
    createGeometryBuffers();
    createInstanceDataBuffer();

    // Build acceleration structures (uses its own one-shot command buffer)
    buildAccelerationStructures();

    // Now create descriptor pool and sets (all resources ready)
    createDescriptorPool();
    createDescriptorSets();

    // TLAS is built and its descriptor is written per-frame in drawFrame().
    // No init-time TLAS build needed — first frame handles it.

    createRTPipeline();
    createSBT();
    createFullscreenPipeline();
    createFullscreenDescriptorSet();

    LOG_INFO("Full hardware RT pipeline initialized");
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

// ── Acceleration Structures ────────────────────────────────────────

void Application::buildAccelerationStructures() {
    LOG_INFO("Building acceleration structures...");

    // Tessellate unit sphere
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

    // Convert sphere geometry to GPU vertices
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

    // Upload sphere geometry via one-shot staging (flat scope so nothing dangles)
    vk::raii::Buffer sphereVertBuf = nullptr;
    vk::raii::DeviceMemory sphereVertMem = nullptr;
    vk::raii::Buffer sphereIdxBuf = nullptr;
    vk::raii::DeviceMemory sphereIdxMem = nullptr;

    auto cmd = m_commandManager->beginSingleTimeCommands();

    // Vertex buffer
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
        vk::MemoryAllocateFlagsInfo flagsInfo;
        flagsInfo.setFlags(vk::MemoryAllocateFlagBits::eDeviceAddress);
        vk::MemoryAllocateInfo aInfo;
        aInfo.setAllocationSize(memReqs.size);
        aInfo.setMemoryTypeIndex(m_vulkanDevice->findMemoryType(memReqs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal));
        aInfo.setPNext(&flagsInfo);
        sphereVertMem = vk::raii::DeviceMemory(m_vulkanDevice->get(), aInfo);
        sphereVertBuf.bindMemory(*sphereVertMem, 0);
    }
    // staging must live until after submit
    Buffer stagingVert(m_vulkanDevice.get(), {sphereVertSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent});
    stagingVert.copyFrom(sphereGPUverts.data(), sphereVertSize);
    cmd.copyBuffer(*stagingVert.get(), *sphereVertBuf, vk::BufferCopy{0, 0, sphereVertSize});

    // Index buffer
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
        vk::MemoryAllocateFlagsInfo flagsInfo;
        flagsInfo.setFlags(vk::MemoryAllocateFlagBits::eDeviceAddress);
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

    // Barrier: transfer → AS read
    {
        vk::MemoryBarrier uploadBarrier;
        uploadBarrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
        uploadBarrier.setDstAccessMask(vk::AccessFlagBits::eAccelerationStructureReadKHR);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            {}, uploadBarrier, nullptr, nullptr);
    }

    // Submit and wait
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

    // Build unit sphere BLAS (handles its own cmd/submit/wait)
    m_sphereUnitBLAS = buildBLAS(m_vulkanDevice.get(),
        *sphereVertBuf, static_cast<uint32_t>(m_sphereGeomVerts.size()),
        *sphereIdxBuf, 0, static_cast<uint32_t>(m_sphereGeomIndices.size()),
        sphereVertAddr, sphereIdxAddr);

    // Build model BLAS (each handles its own cmd/submit/wait)
    if (!m_vertexBuffers.empty() && !m_indexBuffers.empty()) {
        vk::DeviceAddress baseVertAddr = m_vulkanDevice->get().getBufferAddress(
            {*m_vertexBuffers[0]->get()});
        vk::DeviceAddress baseIdxAddr = m_vulkanDevice->get().getBufferAddress(
            {*m_indexBuffers[0]->get()});

        for (size_t s = 0; s < m_modelSources.size(); s++) {
            const auto& src = m_modelSources[s];
            // Vertex data for this source starts at vertexOffset in the merged buffer.
            // Indices are LOCAL (0-based), so the BLAS vertex address must point to the source offset.
            vk::DeviceAddress srcVertAddr = baseVertAddr + src.vertexOffset * sizeof(GPUVertex);
            BLAS blas = buildBLAS(m_vulkanDevice.get(),
                *m_vertexBuffers[0]->get(), static_cast<uint32_t>(src.positions.size()),
                *m_indexBuffers[0]->get(), src.firstIndex, src.indexCount,
                srcVertAddr, baseIdxAddr);
            m_blases.push_back(std::move(blas));
        }
    }

    LOG_INFO("All BLAS built (" + std::to_string(m_blases.size()) + " models + unit sphere)");
}

// ── Instance data buffer ───────────────────────────────────────────

void Application::createInstanceDataBuffer() {
    m_instanceData.clear();

    for (size_t i = 0; i < m_modelRefs.size(); i++) {
        InstanceData id;
        id.materialId = m_modelRefs[i].materialId;
        id.textureIndex = m_modelRefs[i].textureIndex >= 0
            ? static_cast<uint32_t>(m_modelRefs[i].textureIndex) : ~0u;
        id.modelRefIndex = static_cast<uint32_t>(i);
        id._pad = 0;
        m_instanceData.push_back(id);
    }

    for (size_t i = 0; i < m_spheres.size(); i++) {
        InstanceData id;
        id.materialId = m_spheres[i].materialIndex;
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
    setInstanceDataDirty();

    LOG_INFO("Instance data buffer: " + std::to_string(m_instanceData.size()) + " instances");
}

// ── RT Pipeline + SBT ──────────────────────────────────────────────

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
        *m_tlasSetLayout, *m_perFrameSetLayout, *m_sceneSetLayout
    };

    vk::PushConstantRange pcRange;
    pcRange.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eMissKHR);
    pcRange.setSize(sizeof(RTGlobalConstants));

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setSetLayouts(setLayouts);
    pipelineLayoutInfo.setPushConstantRanges(pcRange);
    m_rtPipelineLayout = m_vulkanDevice->get().createPipelineLayout(pipelineLayoutInfo);

    // Shader stages: [0]=rgen, [1]=rmiss, [2]=smiss, [3]=rchit
    std::array<vk::PipelineShaderStageCreateInfo, 4> stages;
    stages[0].setStage(vk::ShaderStageFlagBits::eRaygenKHR);
    stages[0].setModule(*m_rgenShader->get());
    stages[0].setPName("rayGen");

    stages[1].setStage(vk::ShaderStageFlagBits::eMissKHR);
    stages[1].setModule(*m_rmissShader->get());
    stages[1].setPName("miss");

    stages[2].setStage(vk::ShaderStageFlagBits::eMissKHR);
    stages[2].setModule(*m_smissShader->get());
    stages[2].setPName("shadowMiss");

    stages[3].setStage(vk::ShaderStageFlagBits::eClosestHitKHR);
    stages[3].setModule(*m_rchitShader->get());
    stages[3].setPName("closestHit");

    // Groups: [0]=raygen, [1]=miss, [2]=shadow miss, [3]=hit
    std::array<vk::RayTracingShaderGroupCreateInfoKHR, 4> groups;
    for (auto& g : groups) {
        g.setGeneralShader(VK_SHADER_UNUSED_KHR);
        g.setAnyHitShader(VK_SHADER_UNUSED_KHR);
        g.setClosestHitShader(VK_SHADER_UNUSED_KHR);
        g.setIntersectionShader(VK_SHADER_UNUSED_KHR);
    }

    groups[0].setType(vk::RayTracingShaderGroupTypeKHR::eGeneral);
    groups[0].setGeneralShader(0);

    groups[1].setType(vk::RayTracingShaderGroupTypeKHR::eGeneral);
    groups[1].setGeneralShader(1);

    groups[2].setType(vk::RayTracingShaderGroupTypeKHR::eGeneral);
    groups[2].setGeneralShader(2);

    groups[3].setType(vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup);
    groups[3].setClosestHitShader(3);

    vk::RayTracingPipelineCreateInfoKHR pipelineInfo;
    pipelineInfo.setStages(stages);
    pipelineInfo.setGroups(groups);
    pipelineInfo.setMaxPipelineRayRecursionDepth(4);
    pipelineInfo.setLayout(*m_rtPipelineLayout);

    auto result = m_vulkanDevice->get().createRayTracingPipelinesKHR(nullptr, nullptr, pipelineInfo);
    m_rtPipeline = std::move(result[0]);

    auto& rtProps = m_vulkanDevice->getRTPipelineProps();
    uint32_t handleSize = rtProps.shaderGroupHandleSize;
    uint32_t handleAlign = rtProps.shaderGroupHandleAlignment;
    m_sbtStride = (handleSize + handleAlign - 1) & ~(handleAlign - 1);

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
    vk::BufferCopy copyRegion{0, 0, sbtSize};
    cmd.copyBuffer(*staging.get(), *m_sbtBuffer->get(), copyRegion);
    cmd.end();

    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*cmd);
    m_vulkanDevice->getGraphicsQueue().submit(submitInfo, nullptr);
    m_vulkanDevice->waitIdle();

    LOG_INFO("SBT created: " + std::to_string(groupCount) + " groups");
}

// ── RT Storage Image ───────────────────────────────────────────────

void Application::createRtStorageImage() {
    vk::ImageCreateInfo imageInfo;
    imageInfo.setImageType(vk::ImageType::e2D);
    imageInfo.setExtent(vk::Extent3D{static_cast<uint32_t>(m_windowWidth), static_cast<uint32_t>(m_windowHeight), 1});
    imageInfo.setMipLevels(1);
    imageInfo.setArrayLayers(1);
    imageInfo.setFormat(vk::Format::eR8G8B8A8Unorm);
    imageInfo.setTiling(vk::ImageTiling::eOptimal);
    imageInfo.setInitialLayout(vk::ImageLayout::eUndefined);
    imageInfo.setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled);
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
        barrier.setSrcAccessMask(vk::AccessFlagBits::eNone);
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
    samplerInfo.setMagFilter(vk::Filter::eLinear);
    samplerInfo.setMinFilter(vk::Filter::eLinear);
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
    imageInfo.setMipLevels(1);
    imageInfo.setArrayLayers(1);
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

    // Transition to GENERAL
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
    barrier.setSrcAccessMask(vk::AccessFlagBits::eNone);
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

    LOG_INFO("Accumulation buffer created (shared across frames)");
}

// ── Descriptor Set Layouts ────────────────────────────────────────

void Application::createDescriptorSetLayouts() {
    LOG_INFO("Creating descriptor set layouts...");

    // Set 0: TLAS
    {
        vk::DescriptorSetLayoutBinding binding;
        binding.setBinding(0);
        binding.setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR);
        binding.setDescriptorCount(1);
        binding.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR
                            | vk::ShaderStageFlagBits::eClosestHitKHR);
        vk::DescriptorSetLayoutCreateInfo layoutInfo;
        layoutInfo.setBindings(binding);
        m_tlasSetLayout = m_vulkanDevice->get().createDescriptorSetLayout(layoutInfo);
    }

    // Set 1: per-frame (Camera b0, Lights b1, Output u2, InstanceData b3, Accum b4)
    {
        std::array<vk::DescriptorSetLayoutBinding, 5> bindings;
        bindings[0] = {0, vk::DescriptorType::eUniformBuffer, 1,
            vk::ShaderStageFlagBits::eRaygenKHR};
        bindings[1] = {1, vk::DescriptorType::eStorageBuffer, 1,
            vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eMissKHR | vk::ShaderStageFlagBits::eRaygenKHR};
        bindings[2] = {2, vk::DescriptorType::eStorageImage, 1,
            vk::ShaderStageFlagBits::eRaygenKHR};
        bindings[3] = {3, vk::DescriptorType::eStorageBuffer, 1,
            vk::ShaderStageFlagBits::eClosestHitKHR};
        bindings[4] = {4, vk::DescriptorType::eStorageImage, 1,
            vk::ShaderStageFlagBits::eRaygenKHR};
        vk::DescriptorSetLayoutCreateInfo layoutInfo;
        layoutInfo.setBindings(bindings);
        m_perFrameSetLayout = m_vulkanDevice->get().createDescriptorSetLayout(layoutInfo);
    }

    // Set 2: scene (Materials b0, Vertices b1, Indices b2, ModelRefs b3, Textures t4, Envmap t5)
    {
        std::array<vk::DescriptorSetLayoutBinding, 6> bindings;
        bindings[0] = {0, vk::DescriptorType::eStorageBuffer, 1,
            vk::ShaderStageFlagBits::eClosestHitKHR};
        bindings[1] = {1, vk::DescriptorType::eStorageBuffer, 1,
            vk::ShaderStageFlagBits::eClosestHitKHR};
        bindings[2] = {2, vk::DescriptorType::eStorageBuffer, 1,
            vk::ShaderStageFlagBits::eClosestHitKHR};
        bindings[3] = {3, vk::DescriptorType::eStorageBuffer, 1,
            vk::ShaderStageFlagBits::eClosestHitKHR};
        bindings[4] = {4, vk::DescriptorType::eCombinedImageSampler, kMaxTextures,
            vk::ShaderStageFlagBits::eClosestHitKHR};
        bindings[5] = {5, vk::DescriptorType::eCombinedImageSampler, 1,
            vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eMissKHR | vk::ShaderStageFlagBits::eRaygenKHR};
        vk::DescriptorSetLayoutCreateInfo layoutInfo;
        layoutInfo.setBindings(bindings);
        m_sceneSetLayout = m_vulkanDevice->get().createDescriptorSetLayout(layoutInfo);
    }

    // Sampler for fullscreen
    {
        vk::DescriptorSetLayoutBinding binding{0, vk::DescriptorType::eCombinedImageSampler, 1,
            vk::ShaderStageFlagBits::eFragment};
        vk::DescriptorSetLayoutCreateInfo layoutInfo;
        layoutInfo.setBindings(binding);
        m_samplerSetLayout = m_vulkanDevice->get().createDescriptorSetLayout(layoutInfo);
    }

    LOG_INFO("Descriptor set layouts created");
}

void Application::createDescriptorPool() {
    uint32_t fif = static_cast<uint32_t>(m_framesInFlight);

    std::array<vk::DescriptorPoolSize, 5> poolSizes;
    poolSizes[0] = {vk::DescriptorType::eAccelerationStructureKHR, fif};
    poolSizes[1] = {vk::DescriptorType::eUniformBuffer, fif};
    poolSizes[2] = {vk::DescriptorType::eStorageBuffer, fif * 8};
    poolSizes[3] = {vk::DescriptorType::eStorageImage, fif * 2};
    poolSizes[4] = {vk::DescriptorType::eCombinedImageSampler, static_cast<uint32_t>(fif * (kMaxTextures + 2))};

    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
    poolInfo.setMaxSets(fif * 4);
    poolInfo.setPoolSizes(poolSizes);
    m_descriptorPool = m_vulkanDevice->get().createDescriptorPool(poolInfo);
    LOG_INFO("Descriptor pool created");
}

void Application::createDescriptorSets() {
    LOG_INFO("Creating descriptor sets...");

    // Set 0: TLAS
    {
        std::vector<vk::DescriptorSetLayout> layouts(m_framesInFlight, *m_tlasSetLayout);
        vk::DescriptorSetAllocateInfo allocInfo;
        allocInfo.setDescriptorPool(*m_descriptorPool);
        allocInfo.setSetLayouts(layouts);
        m_tlasDescSets = m_vulkanDevice->get().allocateDescriptorSets(allocInfo);
    }

    // Set 1: per-frame
    {
        std::vector<vk::DescriptorSetLayout> layouts(m_framesInFlight, *m_perFrameSetLayout);
        vk::DescriptorSetAllocateInfo allocInfo;
        allocInfo.setDescriptorPool(*m_descriptorPool);
        allocInfo.setSetLayouts(layouts);
        m_perFrameDescSets = m_vulkanDevice->get().allocateDescriptorSets(allocInfo);

        for (size_t i = 0; i < m_framesInFlight; ++i) {
            std::vector<vk::WriteDescriptorSet> writes;

            vk::DescriptorBufferInfo camInfo{*m_cameraUniformBuffers[i]->get(), 0, sizeof(CameraData)};
            vk::WriteDescriptorSet camWrite;
            camWrite.setDstSet(*m_perFrameDescSets[i]);
            camWrite.setDstBinding(0);
            camWrite.setDescriptorType(vk::DescriptorType::eUniformBuffer);
            camWrite.setDescriptorCount(1);
            camWrite.setBufferInfo(camInfo);
            writes.push_back(camWrite);

            vk::DescriptorBufferInfo lightInfo{*m_lightBuffers[i]->get(), 0, VK_WHOLE_SIZE};
            vk::WriteDescriptorSet lightWrite;
            lightWrite.setDstSet(*m_perFrameDescSets[i]);
            lightWrite.setDstBinding(1);
            lightWrite.setDescriptorType(vk::DescriptorType::eStorageBuffer);
            lightWrite.setDescriptorCount(1);
            lightWrite.setBufferInfo(lightInfo);
            writes.push_back(lightWrite);

            vk::DescriptorImageInfo imgInfo{nullptr, *m_rtOutputImageViews[i], vk::ImageLayout::eGeneral};
            vk::WriteDescriptorSet imgWrite;
            imgWrite.setDstSet(*m_perFrameDescSets[i]);
            imgWrite.setDstBinding(2);
            imgWrite.setDescriptorType(vk::DescriptorType::eStorageImage);
            imgWrite.setDescriptorCount(1);
            imgWrite.setImageInfo(imgInfo);
            writes.push_back(imgWrite);

            vk::DescriptorBufferInfo instInfo{*m_instanceDataBuffer->get(), 0, VK_WHOLE_SIZE};
            vk::WriteDescriptorSet instWrite;
            instWrite.setDstSet(*m_perFrameDescSets[i]);
            instWrite.setDstBinding(3);
            instWrite.setDescriptorType(vk::DescriptorType::eStorageBuffer);
            instWrite.setDescriptorCount(1);
            instWrite.setBufferInfo(instInfo);
            writes.push_back(instWrite);

            vk::DescriptorImageInfo accumInfo{nullptr, *m_accumImageView, vk::ImageLayout::eGeneral};
            vk::WriteDescriptorSet accumWrite;
            accumWrite.setDstSet(*m_perFrameDescSets[i]);
            accumWrite.setDstBinding(4);
            accumWrite.setDescriptorType(vk::DescriptorType::eStorageImage);
            accumWrite.setDescriptorCount(1);
            accumWrite.setImageInfo(accumInfo);
            writes.push_back(accumWrite);

            m_vulkanDevice->get().updateDescriptorSets(writes, nullptr);
        }
    }

    // Set 2: scene
    {
        std::vector<vk::DescriptorSetLayout> layouts(m_framesInFlight, *m_sceneSetLayout);
        vk::DescriptorSetAllocateInfo allocInfo;
        allocInfo.setDescriptorPool(*m_descriptorPool);
        allocInfo.setSetLayouts(layouts);
        m_sceneDescSets = m_vulkanDevice->get().allocateDescriptorSets(allocInfo);

        for (size_t i = 0; i < m_framesInFlight; ++i) {
            std::vector<vk::WriteDescriptorSet> writes;

            if (!m_materialBuffers.empty()) {
                vk::DescriptorBufferInfo bufInfo{*m_materialBuffers[i]->get(), 0, VK_WHOLE_SIZE};
                vk::WriteDescriptorSet w;
                w.setDstSet(*m_sceneDescSets[i]);
                w.setDstBinding(0);
                w.setDescriptorType(vk::DescriptorType::eStorageBuffer);
                w.setDescriptorCount(1);
                w.setBufferInfo(bufInfo);
                writes.push_back(w);
            }
            if (!m_vertexBuffers.empty()) {
                vk::DescriptorBufferInfo bufInfo{*m_vertexBuffers[i]->get(), 0, VK_WHOLE_SIZE};
                vk::WriteDescriptorSet w;
                w.setDstSet(*m_sceneDescSets[i]);
                w.setDstBinding(1);
                w.setDescriptorType(vk::DescriptorType::eStorageBuffer);
                w.setDescriptorCount(1);
                w.setBufferInfo(bufInfo);
                writes.push_back(w);
            }
            if (!m_indexBuffers.empty()) {
                vk::DescriptorBufferInfo bufInfo{*m_indexBuffers[i]->get(), 0, VK_WHOLE_SIZE};
                vk::WriteDescriptorSet w;
                w.setDstSet(*m_sceneDescSets[i]);
                w.setDstBinding(2);
                w.setDescriptorType(vk::DescriptorType::eStorageBuffer);
                w.setDescriptorCount(1);
                w.setBufferInfo(bufInfo);
                writes.push_back(w);
            }
            if (!m_modelRefBuffers.empty()) {
                vk::DescriptorBufferInfo bufInfo{*m_modelRefBuffers[i]->get(), 0, VK_WHOLE_SIZE};
                vk::WriteDescriptorSet w;
                w.setDstSet(*m_sceneDescSets[i]);
                w.setDstBinding(3);
                w.setDescriptorType(vk::DescriptorType::eStorageBuffer);
                w.setDescriptorCount(1);
                w.setBufferInfo(bufInfo);
                writes.push_back(w);
            }

            // Textures
            {
                std::array<vk::DescriptorImageInfo, kMaxTextures> texInfos;
                for (size_t t = 0; t < kMaxTextures; t++) {
                    texInfos[t].setSampler(*m_textureSampler);
                    texInfos[t].setImageView(t < m_textures.size() && m_textures[t]
                        ? m_textures[t]->getImageView() : *m_dummyTextureView);
                    texInfos[t].setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
                }
                vk::WriteDescriptorSet texWrite;
                texWrite.setDstSet(*m_sceneDescSets[i]);
                texWrite.setDstBinding(4);
                texWrite.setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
                texWrite.setDescriptorCount(static_cast<uint32_t>(kMaxTextures));
                texWrite.setImageInfo(texInfos);
                writes.push_back(texWrite);
            }

            // Envmap
            {
                vk::DescriptorImageInfo envInfo = m_envmapTexture->getDescriptorInfo();
                vk::WriteDescriptorSet w;
                w.setDstSet(*m_sceneDescSets[i]);
                w.setDstBinding(5);
                w.setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
                w.setDescriptorCount(1);
                w.setImageInfo(envInfo);
                writes.push_back(w);
            }

            m_vulkanDevice->get().updateDescriptorSets(writes, nullptr);
        }
    }

    LOG_INFO("Descriptor sets created");
}

// ── Fullscreen Pipeline ──────────────────────────────────────────

void Application::createFullscreenPipeline() {
    std::string shaderPath = "shaders/fullscreen.spv";
    m_fullscreenVertShader = std::make_unique<ShaderModule>(
        ShaderModule::createVertexShader(m_vulkanDevice.get(), shaderPath));
    m_fullscreenFragShader = std::make_unique<ShaderModule>(
        ShaderModule::createFragmentShader(m_vulkanDevice.get(), shaderPath));

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setSetLayouts(*m_samplerSetLayout);
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
    config.depthTestEnable = false;
    config.depthWriteEnable = false;
    config.blendEnable = false;
    config.vertexBindingDescription = vk::VertexInputBindingDescription{};

    m_pipelineManager->createPipeline("fullscreen", config);
    LOG_INFO("Fullscreen pipeline created");
}

void Application::createFullscreenDescriptorSet() {
    std::vector<vk::DescriptorSetLayout> layouts(m_framesInFlight, *m_samplerSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.setDescriptorPool(*m_descriptorPool);
    allocInfo.setSetLayouts(layouts);
    m_fullscreenDescSets = m_vulkanDevice->get().allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < m_framesInFlight; i++) {
        vk::DescriptorImageInfo imageInfo;
        imageInfo.setImageView(*m_rtOutputImageViews[i]);
        imageInfo.setSampler(*m_rtOutputSampler);
        imageInfo.setImageLayout(vk::ImageLayout::eGeneral);

        vk::WriteDescriptorSet write;
        write.setDstSet(*m_fullscreenDescSets[i]);
        write.setDstBinding(0);
        write.setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
        write.setDescriptorCount(1);
        write.setImageInfo(imageInfo);
        m_vulkanDevice->get().updateDescriptorSets(write, nullptr);
    }
    LOG_INFO("Fullscreen descriptor set created");
}

// ── Model Loading (no BVH) ──────────────────────────────────────

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
                src.positions.push_back({
                    attrib.vertices[3 * idx.vertex_index + 0],
                    attrib.vertices[3 * idx.vertex_index + 1],
                    attrib.vertices[3 * idx.vertex_index + 2]});
                src.normals.push_back(idx.normal_index >= 0 ? glm::vec3(
                    attrib.normals[3 * idx.normal_index + 0],
                    attrib.normals[3 * idx.normal_index + 1],
                    attrib.normals[3 * idx.normal_index + 2]) : glm::vec3(0.0f));
                src.texCoords.push_back(idx.texcoord_index >= 0 ? glm::vec2(
                    attrib.texcoords[2 * idx.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]) : glm::vec2(0.0f));
            }
            src.indices.push_back(uniqueMap[key]);
        }
    }

    bool hasNormals = false;
    for (const auto& n : src.normals) { if (glm::dot(n, n) > 0.0f) { hasNormals = true; break; } }
    if (!hasNormals && src.indices.size() >= 3) {
        std::vector<glm::vec3> accum(src.positions.size(), glm::vec3(0.0f));
        for (size_t i = 0; i + 2 < src.indices.size(); i += 3) {
            uint32_t i0 = src.indices[i], i1 = src.indices[i+1], i2 = src.indices[i+2];
            glm::vec3 faceN = glm::cross(
                src.positions[i1] - src.positions[i0],
                src.positions[i2] - src.positions[i0]);
            accum[i0] += faceN; accum[i1] += faceN; accum[i2] += faceN;
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
    src.bvhRoot = -1;

    int idx = static_cast<int>(m_modelSources.size());
    m_modelSources.push_back(std::move(src));
    LOG_INFO("Loaded model '" + objPath + "': " +
             std::to_string(m_modelSources.back().positions.size()) + " vertices, " +
             std::to_string(m_modelSources.back().indices.size()) + " indices");
    return idx;
}

void Application::createDummyTexture() {
    uint32_t white = 0xFFFFFFFF;
    vk::ImageCreateInfo imageInfo;
    imageInfo.setImageType(vk::ImageType::e2D);
    imageInfo.setExtent(vk::Extent3D{1, 1, 1});
    imageInfo.setMipLevels(1);
    imageInfo.setArrayLayers(1);
    imageInfo.setFormat(vk::Format::eR8G8B8A8Unorm);
    imageInfo.setTiling(vk::ImageTiling::eOptimal);
    imageInfo.setInitialLayout(vk::ImageLayout::eUndefined);
    imageInfo.setUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
    imageInfo.setSamples(vk::SampleCountFlagBits::e1);
    m_dummyTextureImage = m_vulkanDevice->get().createImage(imageInfo);

    auto memReqs = m_dummyTextureImage.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo;
    allocInfo.setAllocationSize(memReqs.size);
    allocInfo.setMemoryTypeIndex(m_vulkanDevice->findMemoryType(memReqs.memoryTypeBits,
        vk::MemoryPropertyFlagBits::eDeviceLocal));
    m_dummyTextureMemory = m_vulkanDevice->get().allocateMemory(allocInfo);
    m_dummyTextureImage.bindMemory(*m_dummyTextureMemory, 0);

    {
        Buffer staging(m_vulkanDevice.get(), {4, vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent});
        staging.copyFrom(&white, 4);

        auto& cmd = m_commandBuffers[0];
        cmd.reset();
        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        cmd.begin(beginInfo);

        vk::ImageMemoryBarrier barrier;
        barrier.setOldLayout(vk::ImageLayout::eUndefined);
        barrier.setNewLayout(vk::ImageLayout::eTransferDstOptimal);
        barrier.setImage(*m_dummyTextureImage);
        barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        barrier.setDstAccessMask(vk::AccessFlagBits::eTransferWrite);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, barrier);

        vk::BufferImageCopy region;
        region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        region.imageExtent = vk::Extent3D{1, 1, 1};
        cmd.copyBufferToImage(*staging.get(), *m_dummyTextureImage,
            vk::ImageLayout::eTransferDstOptimal, region);

        barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal);
        barrier.setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
        barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
        barrier.setDstAccessMask(vk::AccessFlagBits::eShaderRead);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader, {}, nullptr, nullptr, barrier);

        cmd.end();
        vk::SubmitInfo submitInfo;
        submitInfo.setCommandBuffers(*cmd);
        m_vulkanDevice->getGraphicsQueue().submit(submitInfo, nullptr);
        m_vulkanDevice->waitIdle();
    }

    vk::ImageViewCreateInfo viewInfo;
    viewInfo.setImage(*m_dummyTextureImage);
    viewInfo.setViewType(vk::ImageViewType::e2D);
    viewInfo.setFormat(vk::Format::eR8G8B8A8Unorm);
    viewInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    m_dummyTextureView = m_vulkanDevice->get().createImageView(viewInfo);

    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.setMagFilter(vk::Filter::eLinear);
    samplerInfo.setMinFilter(vk::Filter::eLinear);
    samplerInfo.setAddressModeU(vk::SamplerAddressMode::eRepeat);
    samplerInfo.setAddressModeV(vk::SamplerAddressMode::eRepeat);
    samplerInfo.setAddressModeW(vk::SamplerAddressMode::eRepeat);
    m_textureSampler = m_vulkanDevice->get().createSampler(samplerInfo);

    LOG_INFO("Dummy texture and sampler created");
}

void Application::createTextures() {
    while (m_textures.size() < m_modelSources.size()) {
        size_t i = m_textures.size();
        if (!m_modelSources[i].texturePath.empty()) {
            m_textures.push_back(std::make_unique<Texture>(m_vulkanDevice.get(),
                TextureConfig{m_modelSources[i].texturePath}));
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

void Application::createModelDataBuffers() { createGeometryBuffers(); }

void Application::createGeometryBuffers() {
    m_vertexBuffers.clear();
    m_indexBuffers.clear();
    m_modelRefBuffers.clear();

    std::vector<GPUVertex> mergedVerts;
    std::vector<uint32_t> mergedIndices;

    for (auto& src : m_modelSources) {
        src.vertexOffset = static_cast<uint32_t>(mergedVerts.size());
        src.firstIndex  = static_cast<uint32_t>(mergedIndices.size());
        src.indexCount  = static_cast<uint32_t>(src.indices.size());

        for (size_t v = 0; v < src.positions.size(); v++) {
            GPUVertex gv;
            gv.position = src.positions[v];
            gv.normal = v < src.normals.size() ? src.normals[v] : glm::vec3(0.0f);
            gv.texCoord = v < src.texCoords.size() ? src.texCoords[v] : glm::vec2(0.0f);
            gv._pad[0] = 0.0f; gv._pad[1] = 0.0f;
            mergedVerts.push_back(gv);
        }
        mergedIndices.insert(mergedIndices.end(), src.indices.begin(), src.indices.end());
    }

    if (mergedVerts.empty()) { mergedVerts.push_back({}); mergedIndices.push_back(0); }

    for (size_t i = 0; i < m_modelRefs.size(); i++) {
        const auto& src = m_modelSources[m_modelRefSourceIdx[i]];
        m_modelRefs[i].vertexOffset = src.vertexOffset;
        m_modelRefs[i].firstIndex  = src.firstIndex;
        m_modelRefs[i].indexCount  = src.indexCount;
        m_modelRefs[i].boundingSphereCenter = src.boundingSphereCenter;
        m_modelRefs[i].boundingSphereRadius = src.boundingSphereRadius;
        m_modelRefs[i].invTransform = glm::inverse(m_modelRefTransforms[i].transform());
        m_modelRefs[i].textureIndex = m_modelRefSourceIdx[i];
        m_modelRefs[i].bvhRoot = -1;
    }

    for (size_t i = 0; i < m_framesInFlight; i++) {
        m_vertexBuffers.push_back(std::make_unique<Buffer>(
            Buffer::createBuffer(m_vulkanDevice.get(),
                kMaxVertices * sizeof(GPUVertex),
                vk::BufferUsageFlagBits::eStorageBuffer
                | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR
                | vk::BufferUsageFlagBits::eShaderDeviceAddress,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));
        m_vertexBuffers.back()->copyFrom(mergedVerts.data(), mergedVerts.size() * sizeof(GPUVertex));

        m_indexBuffers.push_back(std::make_unique<Buffer>(
            Buffer::createBuffer(m_vulkanDevice.get(),
                kMaxIndices * sizeof(uint32_t),
                vk::BufferUsageFlagBits::eStorageBuffer
                | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR
                | vk::BufferUsageFlagBits::eShaderDeviceAddress,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));
        m_indexBuffers.back()->copyFrom(mergedIndices.data(), mergedIndices.size() * sizeof(uint32_t));

        m_modelRefBuffers.push_back(std::make_unique<Buffer>(
            Buffer::createBuffer(m_vulkanDevice.get(),
                kMaxModelRefs * sizeof(ModelRef),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)));
        m_modelRefBuffers.back()->copyFrom(m_modelRefs.data(), m_modelRefs.size() * sizeof(ModelRef));
    }

    setModelRefsDirty();
    LOG_INFO("Geometry built: " + std::to_string(mergedVerts.size()) + " vertices, " +
             std::to_string(mergedIndices.size()) + " indices, " +
             std::to_string(m_modelRefs.size()) + " model refs");
}

// ── Material Buffer ──────────────────────────────────────────────

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

// ── Uniform / Light Buffers ──────────────────────────────────────

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

void Application::createLightBuffer() {
    constexpr size_t kMaxLights = 16;
    if (m_lights.empty()) m_lights.push_back({});
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
    LOG_INFO("Light SSBO created");
}

// ── Vulkan Setup ─────────────────────────────────────────────────

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
    config.clearColors = true;
    config.clearDepth = true;
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
    imageInfo.setMipLevels(1);
    imageInfo.setArrayLayers(1);
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
        m_renderFinishedSemaphores.emplace_back(m_vulkanDevice->get().createSemaphore({}));
        m_inFlightFences.emplace_back(m_vulkanDevice->get().createFence(
            {vk::FenceCreateFlagBits::eSignaled}));
    }
}

// ── Buffer Updates ───────────────────────────────────────────────

void Application::updateUniformBuffer(size_t currentFrame) {
    glm::mat4 viewProj = m_camera.GetViewProj() * glm::inverse(m_cameraTransform());
    CameraData data{viewProj, glm::inverse(viewProj), glm::vec4(m_cameraTransform.translation, 0.0f)};
    memcpy(m_mappedCameraUniformData[currentFrame], &data, sizeof(data));
}

void Application::updateLightBuffer(size_t currentFrame) {
    if (!isLightsDirty() || m_lights.empty()) return;
    m_lightBuffers[currentFrame]->copyFrom(m_lights.data(), m_lights.size() * sizeof(LightData));
}

void Application::updateMaterialBuffer(size_t currentFrame) {
    if (!isMaterialsDirty() || m_materials.empty()) return;
    m_materialBuffers[currentFrame]->copyFrom(m_materials.data(),
        m_materials.size() * sizeof(MaterialData));
}

void Application::updateModelRefBuffer(size_t currentFrame) {
    if (!isModelRefsDirty() || m_modelRefs.empty()) return;
    m_modelRefBuffers[currentFrame]->copyFrom(m_modelRefs.data(),
        m_modelRefs.size() * sizeof(ModelRef));
}

// ── Draw Frame ──────────────────────────────────────────────────

void Application::drawFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    m_editorUI->draw();
    ImGui::Render();

    (void)m_vulkanDevice->get().waitForFences(*m_inFlightFences[m_currentFrame], true, UINT64_MAX);

    uint32_t imageIndex = m_swapChainManager->acquireNextImage(
        *m_imageAvailableSemaphores[m_currentFrame]);
    if (imageIndex == UINT32_MAX) { recreateSwapChain(); return; }

    if (m_imagesInFlight[imageIndex]) {
        (void)m_vulkanDevice->get().waitForFences(m_imagesInFlight[imageIndex], true, UINT64_MAX);
    }
    m_imagesInFlight[imageIndex] = *m_inFlightFences[m_currentFrame];

    updateUniformBuffer(m_currentFrame);
    updateLightBuffer(m_currentFrame);
    updateMaterialBuffer(m_currentFrame);
    updateModelRefBuffer(m_currentFrame);

    m_commandBuffers[m_currentFrame].reset();
    vk::CommandBufferBeginInfo beginInfo;
    m_commandBuffers[m_currentFrame].begin(beginInfo);

    // ── Rebuild TLAS (only when transforms change) ────────────
    if (m_tlasNeedsRebuild) {
        m_tlasNeedsRebuild = false;
        std::vector<VkAccelerationStructureInstanceKHR> instances;

        for (size_t i = 0; i < m_modelRefs.size() && i < m_blases.size(); i++) {
            int srcIdx = m_modelRefSourceIdx[i];
            if (srcIdx < 0 || srcIdx >= static_cast<int>(m_blases.size())) continue;
            instances.push_back(makeInstance(m_blases[srcIdx],
                m_modelRefTransforms[i].transform(), static_cast<uint32_t>(i)));
        }

        uint32_t sphereBase = static_cast<uint32_t>(m_modelRefs.size());
        for (size_t s = 0; s < m_spheres.size(); s++) {
            glm::mat4 xform = glm::translate(glm::mat4(1.0f), m_spheres[s].center)
                            * glm::scale(glm::mat4(1.0f), glm::vec3(m_spheres[s].radius));
            instances.push_back(makeInstance(m_sphereUnitBLAS, xform,
                sphereBase + static_cast<uint32_t>(s)));
        }

        TLAS newTLAS = buildTLAS(m_vulkanDevice.get(), instances);
        // Retire old TLAS so its buffers/AS stay alive until next cleanup cycle
        if (m_tlas.handle)
            m_retiredTLAS.push_back(std::move(m_tlas));
        m_tlas = std::move(newTLAS);
    }

    // Update TLAS descriptor
    {
        vk::WriteDescriptorSetAccelerationStructureKHR asInfo;
        asInfo.setAccelerationStructures(m_tlas.handle);
        vk::WriteDescriptorSet write;
        write.setDstSet(*m_tlasDescSets[m_currentFrame]);
        write.setDstBinding(0);
        write.setDescriptorCount(1);
        write.setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR);
        write.pNext = &asInfo;
        m_vulkanDevice->get().updateDescriptorSets(write, nullptr);
    }

    // Barrier: AS build → RT
    {
        vk::MemoryBarrier barrier;
        barrier.setSrcAccessMask(vk::AccessFlagBits::eAccelerationStructureWriteKHR);
        barrier.setDstAccessMask(vk::AccessFlagBits::eAccelerationStructureReadKHR);
        m_commandBuffers[m_currentFrame].pipelineBarrier(
            vk::PipelineStageFlagBits::eAccelerationStructureBuildKHR,
            vk::PipelineStageFlagBits::eRayTracingShaderKHR,
            {}, barrier, nullptr, nullptr);
    }

    // ── Trace rays ──────────────────────────────────────────
    m_commandBuffers[m_currentFrame].bindPipeline(
        vk::PipelineBindPoint::eRayTracingKHR, *m_rtPipeline);

    std::array<vk::DescriptorSet, 3> rtSets = {
        *m_tlasDescSets[m_currentFrame],
        *m_perFrameDescSets[m_currentFrame],
        *m_sceneDescSets[m_currentFrame]
    };
    m_commandBuffers[m_currentFrame].bindDescriptorSets(
        vk::PipelineBindPoint::eRayTracingKHR, *m_rtPipelineLayout, 0, rtSets, nullptr);

    if (m_accumDirty) {
        m_totalAccumSamples = 0;
        m_accumDirty = false;
    }

    RTGlobalConstants pc{};
    pc.lightCount = static_cast<uint32_t>(m_lights.size());
    pc.materialCount = static_cast<uint32_t>(m_materials.size());
    pc.modelRefCount = static_cast<uint32_t>(m_modelRefs.size());
    pc.sphereCount = static_cast<uint32_t>(m_spheres.size());
    pc.sphereInstanceBase = static_cast<uint32_t>(m_modelRefs.size());
    pc.totalAccumSamples = m_totalAccumSamples;
    m_totalAccumSamples++;
    pc.ambientStrength = m_ambientStrength;
    m_commandBuffers[m_currentFrame].pushConstants<RTGlobalConstants>(
        *m_rtPipelineLayout,
        vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR | vk::ShaderStageFlagBits::eMissKHR, 0, pc);

    vk::DeviceAddress sbtAddr = m_vulkanDevice->get().getBufferAddress({*m_sbtBuffer->get()});
    vk::StridedDeviceAddressRegionKHR raygenRegion{sbtAddr, m_sbtStride, m_sbtStride};
    vk::StridedDeviceAddressRegionKHR missRegion{sbtAddr + m_sbtMissOffset, m_sbtStride,
        2 * m_sbtStride};
    vk::StridedDeviceAddressRegionKHR hitRegion{sbtAddr + m_sbtHitGroupOffset, m_sbtStride,
        m_sbtStride};
    vk::StridedDeviceAddressRegionKHR callableRegion{0, 0, 0};

    m_commandBuffers[m_currentFrame].traceRaysKHR(
        raygenRegion, missRegion, hitRegion, callableRegion,
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
        m_commandBuffers[m_currentFrame].pipelineBarrier(
            vk::PipelineStageFlagBits::eRayTracingShaderKHR,
            vk::PipelineStageFlagBits::eFragmentShader,
            {}, nullptr, nullptr, barrier);
    }

    // ── Fullscreen display ──────────────────────────────────
    vk::Viewport viewport;
    viewport.setWidth(static_cast<float>(m_swapChainManager->getExtent().width));
    viewport.setHeight(static_cast<float>(m_swapChainManager->getExtent().height));
    viewport.setMinDepth(0.0f);
    viewport.setMaxDepth(1.0f);
    m_commandBuffers[m_currentFrame].setViewport(0, viewport);

    vk::Rect2D scissor{{0, 0}, m_swapChainManager->getExtent()};
    m_commandBuffers[m_currentFrame].setScissor(0, scissor);

    std::array<vk::ClearValue, 2> clearValues;
    clearValues[0].color = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f};
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    vk::RenderPassBeginInfo renderPassInfo;
    renderPassInfo.setRenderPass(*m_renderPassManager->get());
    renderPassInfo.setFramebuffer(*m_swapChainFramebuffers[imageIndex]);
    renderPassInfo.setRenderArea(scissor);
    renderPassInfo.setClearValues(clearValues);

    m_commandBuffers[m_currentFrame].beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    m_commandBuffers[m_currentFrame].bindPipeline(vk::PipelineBindPoint::eGraphics,
        *m_pipelineManager->getPipeline("fullscreen"));
    m_commandBuffers[m_currentFrame].bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, *m_fullscreenPipelineLayout,
        0, *m_fullscreenDescSets[m_currentFrame], nullptr);
    m_commandBuffers[m_currentFrame].draw(3, 1, 0, 0);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *m_commandBuffers[m_currentFrame]);
    m_commandBuffers[m_currentFrame].endRenderPass();
    m_commandBuffers[m_currentFrame].end();

    // Submit
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::SubmitInfo submitInfo;
    submitInfo.setWaitSemaphores(*m_imageAvailableSemaphores[m_currentFrame]);
    submitInfo.setWaitDstStageMask(waitStages);
    submitInfo.setCommandBuffers(*m_commandBuffers[m_currentFrame]);
    submitInfo.setSignalSemaphores(*m_renderFinishedSemaphores[m_currentFrame]);

    m_vulkanDevice->get().resetFences(*m_inFlightFences[m_currentFrame]);
    m_vulkanDevice->getGraphicsQueue().submit(submitInfo, *m_inFlightFences[m_currentFrame]);

    m_swapChainManager->presentImage(imageIndex, *m_renderFinishedSemaphores[m_currentFrame]);

    // FPS
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
    while (!m_windowManager->shouldClose()) {
        glfwPollEvents();
        drawFrame();
        if (m_framebufferResized) recreateSwapChain();
    }
    m_vulkanDevice->waitIdle();
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

// ── Cleanup ─────────────────────────────────────────────────────

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

    m_perFrameDescSets.clear();
    m_sceneDescSets.clear();
    m_fullscreenDescSets.clear();
    m_tlasDescSets.clear();
    m_descriptorPool = nullptr;
    m_tlasSetLayout = nullptr;
    m_perFrameSetLayout = nullptr;
    m_sceneSetLayout = nullptr;
    m_samplerSetLayout = nullptr;
    m_imguiPool = nullptr;
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
    m_lightBuffers.clear();
    m_commandBuffers.clear();
    m_commandManager.reset();
    m_pipelineManager.reset();
    m_renderPassManager.reset();
    m_swapChainManager.reset();
    m_vulkanDevice.reset();
    m_windowManager.reset();
    m_vulkanInstance.reset();
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

void Application::cleanupRtResources() {
    cleanupRtSwapChainResources();
    m_tlasDescSets.clear();
    m_fullscreenDescSets.clear();
    m_sceneDescSets.clear();
    m_perFrameDescSets.clear();
    m_materialBuffers.clear();
    m_vertexBuffers.clear();
    m_indexBuffers.clear();
    m_modelRefBuffers.clear();
    m_instanceDataBuffer.reset();
    m_sphereDataBuffers.clear();
    m_textures.clear();
    m_envmapTexture.reset();
    m_dummyTextureView = nullptr;
    m_dummyTextureImage = nullptr;
    m_dummyTextureMemory = nullptr;
    m_textureSampler = nullptr;
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
    createAccumBuffer();

    for (size_t i = 0; i < m_framesInFlight; ++i) {
        vk::DescriptorImageInfo imgInfo{nullptr, *m_rtOutputImageViews[i], vk::ImageLayout::eGeneral};
        vk::WriteDescriptorSet rtWrite;
        rtWrite.setDstSet(*m_perFrameDescSets[i]);
        rtWrite.setDstBinding(2);
        rtWrite.setDescriptorType(vk::DescriptorType::eStorageImage);
        rtWrite.setDescriptorCount(1);
        rtWrite.setImageInfo(imgInfo);
        m_vulkanDevice->get().updateDescriptorSets(rtWrite, nullptr);

        vk::DescriptorImageInfo accumInfo{nullptr, *m_accumImageView, vk::ImageLayout::eGeneral};
        vk::WriteDescriptorSet accumWrite;
        accumWrite.setDstSet(*m_perFrameDescSets[i]);
        accumWrite.setDstBinding(4);
        accumWrite.setDescriptorType(vk::DescriptorType::eStorageImage);
        accumWrite.setDescriptorCount(1);
        accumWrite.setImageInfo(accumInfo);
        m_vulkanDevice->get().updateDescriptorSets(accumWrite, nullptr);

        vk::DescriptorImageInfo samplerInfo{*m_rtOutputSampler, *m_rtOutputImageViews[i],
            vk::ImageLayout::eGeneral};
        vk::WriteDescriptorSet fsWrite;
        fsWrite.setDstSet(*m_fullscreenDescSets[i]);
        fsWrite.setDstBinding(0);
        fsWrite.setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
        fsWrite.setDescriptorCount(1);
        fsWrite.setImageInfo(samplerInfo);
        m_vulkanDevice->get().updateDescriptorSets(fsWrite, nullptr);
    }

    createFullscreenPipeline();
    m_camera.SetAspectRatio(static_cast<float>(m_windowWidth) / m_windowHeight);
    m_framebufferResized = false;
    m_accumDirty = true;
}

} // namespace RYRayTracing
