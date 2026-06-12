#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <chrono>
#include <vector>
#include <memory>
#include <array>

#include "Camera.h"
#include "Transform.h"
#include "Light.h"
#include "Scene.h"
#include "vulkan/Texture.hpp"
#include "vulkan/AccelerationStructure.hpp"
#include "EditorUI.h"

namespace RYRayTracing {
    class WindowManager;
    class VulkanInstance;
    class VulkanDevice;
    class SwapChainManager;
    class Buffer;
    class ShaderModule;
    class RenderPassManager;
    class PipelineManager;
    class CommandManager;
    class Texture;
}

namespace RYRayTracing {

struct RTGlobalConstants {
    uint32_t sphereCount;
    uint32_t lightCount;
    uint32_t materialCount;
    uint32_t modelRefCount;
    uint32_t sphereInstanceBase;
    uint32_t totalAccumSamples;   // total samples accumulated (resets on camera/scene change)
    float ambientStrength;
    uint32_t _pad;
};

struct InstanceData {
    uint32_t materialId;
    uint32_t textureIndex;   // ~0u = use diffuse color
    uint32_t modelRefIndex;  // ~0u = not a model ref
    uint32_t _pad;
};

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();

private:
    std::unique_ptr<VulkanInstance> m_vulkanInstance;
    std::unique_ptr<WindowManager> m_windowManager;
    std::unique_ptr<VulkanDevice> m_vulkanDevice;
    std::unique_ptr<SwapChainManager> m_swapChainManager;
    std::unique_ptr<RenderPassManager> m_renderPassManager;
    std::unique_ptr<PipelineManager> m_pipelineManager;
    std::unique_ptr<CommandManager> m_commandManager;

    // ── Descriptor set layouts ──────────────────────────────────
    /// Set 0: TLAS (acceleration structure)
    vk::raii::DescriptorSetLayout m_tlasSetLayout = nullptr;

    /// Set 1: per-frame (Camera UBO, Lights SSBO, Output Image, InstanceData SSBO)
    vk::raii::DescriptorSetLayout m_perFrameSetLayout = nullptr;

    /// Set 2: scene data (Materials, Vertices, Indices, ModelRefs, Textures, Envmap)
    vk::raii::DescriptorSetLayout m_sceneSetLayout = nullptr;

    /// Fullscreen display sampler layout
    vk::raii::DescriptorSetLayout m_samplerSetLayout = nullptr;

    // ── Descriptor pools ────────────────────────────────────────
    vk::raii::DescriptorPool m_descriptorPool = nullptr;
    vk::raii::DescriptorPool m_imguiPool = nullptr;

    // ── Descriptor sets ─────────────────────────────────────────
    std::vector<vk::raii::DescriptorSet> m_tlasDescSets;     // per-frame TLAS
    std::vector<vk::raii::DescriptorSet> m_perFrameDescSets;  // per-frame data
    std::vector<vk::raii::DescriptorSet> m_sceneDescSets;     // scene data
    std::vector<vk::raii::DescriptorSet> m_fullscreenDescSets; // RT output display

    // ── Uniform buffers ────────────────────────────────────────
    std::vector<std::unique_ptr<Buffer>> m_cameraUniformBuffers;
    std::vector<void*> m_mappedCameraUniformData;
    std::vector<std::unique_ptr<Buffer>> m_lightBuffers;
    int m_lightsDirty = 0;
    bool isLightsDirty() { bool v = m_lightsDirty > 0; m_lightsDirty -= v; return v; }
    void setLightsDirty() { m_lightsDirty = static_cast<int>(m_framesInFlight); }

    // ── Shaders ────────────────────────────────────────────────
    std::unique_ptr<ShaderModule> m_rgenShader;
    std::unique_ptr<ShaderModule> m_rchitShader;
    std::unique_ptr<ShaderModule> m_rmissShader;
    std::unique_ptr<ShaderModule> m_smissShader;
    std::unique_ptr<ShaderModule> m_fullscreenVertShader;
    std::unique_ptr<ShaderModule> m_fullscreenFragShader;

    // ── Pipeline layouts ───────────────────────────────────────
    vk::raii::PipelineLayout m_rtPipelineLayout = nullptr;
    vk::raii::PipelineLayout m_fullscreenPipelineLayout = nullptr;
    vk::raii::Pipeline m_rtPipeline = nullptr;

    // ── SBT ────────────────────────────────────────────────────
    std::unique_ptr<Buffer> m_sbtBuffer;
    vk::DeviceSize m_sbtStride = 0;
    vk::DeviceSize m_sbtHitGroupOffset = 0;
    vk::DeviceSize m_sbtMissOffset = 0;
    vk::DeviceSize m_sbtShadowMissOffset = 0;

    // ── Ray-tracing output resources ───────────────────────────
    std::vector<vk::raii::Image> m_rtOutputImages;
    std::vector<vk::raii::DeviceMemory> m_rtOutputImagesMemory;
    std::vector<vk::raii::ImageView> m_rtOutputImageViews;
    vk::raii::Sampler m_rtOutputSampler = nullptr;
    vk::raii::Sampler m_textureSampler = nullptr;

    // ── Accumulation buffer for Monte Carlo (shared across frames) ──
    vk::raii::Image m_accumImage = nullptr;
    vk::raii::DeviceMemory m_accumImageMemory = nullptr;
    vk::raii::ImageView m_accumImageView = nullptr;
    uint32_t m_totalAccumSamples = 0;
    bool m_accumDirty = true; // reset accumulation when scene/camera changes
    vk::raii::Image m_dummyTextureImage = nullptr;
    vk::raii::DeviceMemory m_dummyTextureMemory = nullptr;
    vk::raii::ImageView m_dummyTextureView = nullptr;

    // ── Acceleration Structures ────────────────────────────────
    std::vector<BLAS> m_blases;          // per model source
    BLAS m_sphereUnitBLAS;               // unit sphere BLAS for all spheres
    TLAS m_tlas;                         // current TLAS (for latest frame)
    std::vector<TLAS> m_retiredTLAS;     // old TLAS handles pending cleanup
    bool m_tlasNeedsRebuild = true;      // rebuild on first frame
    std::unique_ptr<Buffer> m_instanceDataBuffer;
    std::vector<InstanceData> m_instanceData;
    int m_instanceDataDirty = 0;
    bool isInstanceDataDirty() { bool v = m_instanceDataDirty > 0; m_instanceDataDirty -= v; return v; }
    void setInstanceDataDirty() { m_instanceDataDirty = static_cast<int>(m_framesInFlight); }

    // Sphere geometry (tessellated once)
    std::vector<glm::vec3> m_sphereGeomVerts;
    std::vector<uint32_t> m_sphereGeomIndices;

    // ── Scene primitives ───────────────────────────────────────
    static constexpr size_t kMaxMaterials = 32;
    static constexpr size_t kMaxSpheres = 32;
    std::vector<std::unique_ptr<Buffer>> m_materialBuffers;
    std::vector<MaterialData> m_materials;
    int m_materialsDirty = 0;
    bool isMaterialsDirty() { bool v = m_materialsDirty > 0; m_materialsDirty -= v; return v; }
    void setMaterialsDirty() { m_materialsDirty = static_cast<int>(m_framesInFlight); }

    // ── Model data ─────────────────────────────────────────────
    static constexpr size_t kMaxVertices = 1'000'000;
    static constexpr size_t kMaxIndices  = 2'000'000;
    static constexpr size_t kMaxModelRefs = 64;
    static constexpr size_t kMaxTextures = 8;
    std::vector<ModelSource>           m_modelSources;
    std::unique_ptr<Texture> m_envmapTexture;
    std::vector<std::unique_ptr<Texture>> m_textures;
    std::vector<ModelRef>              m_modelRefs;
    std::vector<int>                   m_modelRefSourceIdx;
    std::vector<Transform>             m_modelRefTransforms;
    std::vector<std::unique_ptr<Buffer>> m_vertexBuffers;
    std::vector<std::unique_ptr<Buffer>> m_indexBuffers;
    std::vector<std::unique_ptr<Buffer>> m_modelRefBuffers;
    int m_modelRefsDirty = 0;
    bool isModelRefsDirty() { bool v = m_modelRefsDirty > 0; m_modelRefsDirty -= v; return v; }
    void setModelRefsDirty() { m_modelRefsDirty = static_cast<int>(m_framesInFlight); m_tlasNeedsRebuild = true; m_accumDirty = true; }
    void setAccumDirty() { m_accumDirty = true; }

    void createModelDataBuffers();
    void createGeometryBuffers();
    void updateModelRefBuffer(size_t currentFrame);
    void createTextures();
    void createEnvmap();
    int addModelSource(const std::string& objPath, const std::string& texturePath = "");
    void createDummyTexture();

    // Editor UI
    std::unique_ptr<EditorUI> m_editorUI;

    float m_ambientStrength = 0.1f;

    // ── Framebuffers & depth ───────────────────────────────────
    std::vector<vk::raii::Framebuffer> m_swapChainFramebuffers;
    vk::raii::Image m_depthImage = nullptr;
    vk::raii::DeviceMemory m_depthImageMemory = nullptr;
    vk::raii::ImageView m_depthImageView = nullptr;
    vk::Format m_depthFormat;

    // ── Command buffers ────────────────────────────────────────
    std::vector<vk::raii::CommandBuffer> m_commandBuffers;

    // ── Synchronization ────────────────────────────────────────
    std::vector<vk::raii::Semaphore> m_imageAvailableSemaphores;
    std::vector<vk::raii::Semaphore> m_renderFinishedSemaphores;
    std::vector<vk::raii::Fence> m_inFlightFences;
    std::vector<vk::Fence> m_imagesInFlight;

    size_t m_currentFrame;
    size_t m_framesInFlight;

    uint32_t m_windowWidth;
    uint32_t m_windowHeight;

    bool m_framebufferResized;
    bool m_imguiInitialized = false;

    // FPS tracking
    std::chrono::steady_clock::time_point m_fpsLastTime{};
    int m_fpsFrameCount = 0;
    float m_currentFps = 0.0f;

    Transform m_cameraTransform;
    SceneCamera m_camera;
    std::vector<LightData> m_lights;
    std::vector<SphereData> m_spheres;
    int m_spheresDirty = 0;
    bool isSpheresDirty() { bool v = m_spheresDirty > 0; m_spheresDirty -= v; return v; }
    void setSpheresDirty() { m_spheresDirty = static_cast<int>(m_framesInFlight); m_tlasNeedsRebuild = true; }

    // ── Spheres ────────────────────────────────────────────────
    std::vector<std::unique_ptr<Buffer>> m_sphereDataBuffers;

    void initVulkan();
    void initImGui();
    void initComponents();

    void cleanup();
    void recreateSwapChain();
    void cleanupSwapChain();
    void cleanupSyncObjects();
    void cleanupRtResources();
    void cleanupRtSwapChainResources();

    void createInstance();
    void createDevice();
    void createSwapChain();
    void createRenderPass();

    void createDescriptorSetLayouts();
    void createDescriptorPool();
    void createDescriptorSets();

    void createFramebuffers();
    void createCommandPool();

    void createUniformBuffers();
    void createUniformBuffersImpl(vk::DeviceSize bufferSize,
                                  std::vector<std::unique_ptr<Buffer>>& bufferOut,
                                  std::vector<void*>& mappedDataOut) const;
    void createLightBuffer();
    void createMaterialBuffer();

    void createDepthResources();
    void createCommandBuffers();
    void createSyncObjects();

    // RT methods
    void createRtStorageImage();
    void createAccumBuffer();
    void createRTPipeline();
    void createSBT();
    void createAccelerationStructures();
    void buildAccelerationStructures();
    void createFullscreenPipeline();
    void createFullscreenDescriptorSet();
    void createInstanceDataBuffer();

    void updateUniformBuffer(size_t currentFrame);
    void updateLightBuffer(size_t currentFrame);
    void updateMaterialBuffer(size_t currentFrame);

    void drawFrame();
    void mainLoop();

    vk::Format findDepthFormat(const std::vector<vk::Format>& candidates) const;
};

} // namespace RYRayTracing
