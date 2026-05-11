// Application.h
#pragma once

#include "type.h"
#include "Camera.h"
#include "Transform.h"

#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

#include <optional>
#include <vector>
#include <array>
#include <string>
#include <cstdint>

std::vector<char> readFile(const std::string& filename);
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData);
void assertPhysicalDeviceSupportsVulkanVersion(const vk::PhysicalDevice& device, uint32_t vulkanApiVersion);
void assertPhysicalDeviceSupportsGraphicsFamily(const vk::PhysicalDevice& device);
void assertPhysicalDeviceSupportsExtension(const vk::PhysicalDevice& device);
void assertPhysicalDeviceSupportsFeatures(const vk::PhysicalDevice& device);
vk::SurfaceFormatKHR chooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const& availableFormats);
vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const& availablePresentModes);
uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& surfaceCapabilities);
void framebufferResizeCallback(GLFWwindow* window, int width, int height);

class Application {
public:
    Application() = default;
    ~Application() = default;

    void run();

private:
    friend void ::framebufferResizeCallback(GLFWwindow* window, int width, int height);

    void initWindow();
    void initImGui();
    void initComponents();
    void initVulkan();
    void mainLoop();
    void update();
    void updateUniformBuffer(int currentFrame);
    void updateVertexBuffer(int currentFrame);
    void drawFrame();
    vk::raii::CommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const;
    void cleanup();

    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions() const;
    static void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
    bool isDeviceSuitable(vk::raii::PhysicalDevice const& physicalDevice);
    void pickPhysicalDevice();
    void createInstance();
    void createSurface();
    void createLogicalDevice();
    void createSwapChain();
    void createImageViews();
    void createRenderPass();
    void createFramebuffers();
    void createDescriptorSetLayout();
    void createGraphicsPipeline();
    void createCommandPool();
    void createCommandBuffers();
    vk::Format findDepthFormat(const std::vector<vk::Format>& candidates) const;
    void createDepthResources();
    void createSyncObjects();
    void createVertexBuffer();
    void createIndexBuffer();
    void createUniformBuffers();
    void transitionImageLayout(const vk::raii::Image& image, vk::Format format, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) const;
    void copyBufferToImage(const vk::raii::Buffer& buffer, const vk::raii::Image& image, uint32_t width, uint32_t height) const;
    void createImage(uint32_t width, uint32_t height, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Image& image, vk::raii::DeviceMemory& imageMemory) const;
    void createTextureImage();
    void createTextureImageView();
    void createTextureSampler();
    void createDescriptorPool();
    void createDescriptorSets();
    void recreateSwapChain();

    vk::raii::ImageView createImageView(vk::Image image, vk::Format format, const vk::ImageAspectFlags aspectFlags) const;

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;
        bool isComplete() const {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };
    QueueFamilyIndices findQueueFamilies(const vk::raii::PhysicalDevice& device) const;
    vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities);
    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;
    void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& bufferMemory);
    void copyBuffer(const vk::raii::Buffer& srcBuffer, const vk::raii::Buffer& dstBuffer, vk::DeviceSize size) const;
    [[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char>& code) const;
    void recordCommandBuffer(const vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex) const;

    constexpr static int MAX_FRAMES_IN_FLIGHT = 2;

    vk::raii::Context m_context;
    vk::raii::Instance m_instance = nullptr;
    vk::raii::PhysicalDevice m_physicalDevice = nullptr;
    vk::raii::Device m_device = nullptr;
    vk::raii::Queue m_graphicsQueue = nullptr;
    vk::raii::SurfaceKHR m_surface = nullptr;
    vk::raii::RenderPass m_renderPass = nullptr;
    std::vector<vk::raii::DescriptorSetLayout> m_descriptorSetLayouts = {};
    vk::raii::PipelineLayout m_pipelineLayout = nullptr;
    vk::raii::Pipeline m_graphicsPipeline = nullptr;
    vk::raii::CommandPool m_commandPool = nullptr;
    vk::raii::SwapchainKHR m_swapChain = nullptr;
    vk::raii::DescriptorPool m_imguiPool = nullptr;
    vk::raii::DescriptorPool m_descriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> m_descriptorSets;
    vk::raii::DescriptorSet m_combinedDescriptorSet{ nullptr };
    std::vector<vk::raii::ImageView> m_swapChainImageViews;
    std::vector<vk::raii::Framebuffer> m_swapChainFramebuffers;
    std::vector<vk::raii::CommandBuffer> m_commandBuffers;
    std::vector<vk::raii::Semaphore> m_imageAvailableSemaphores;
    std::vector<vk::raii::Semaphore> m_renderFinishedSemaphores;
    std::vector<vk::raii::Fence> m_inFlightFences;

    vk::raii::DeviceMemory m_vertexBufferMemory = nullptr;
    vk::raii::Buffer m_vertexBuffer = nullptr;
    void* m_vertexBufferMapped = nullptr;
    vk::raii::DeviceMemory m_indexBufferMemory = nullptr;
    vk::raii::Buffer m_indexBuffer = nullptr;
    std::vector<vk::raii::DeviceMemory> m_uniformBuffersMemory;
    std::vector<vk::raii::Buffer> m_uniformBuffers;
    std::vector<void*> m_uniformBuffersMapped;

    vk::raii::DeviceMemory m_textureImageMemory{ nullptr };
    vk::raii::Image m_textureImage{ nullptr };
    vk::raii::ImageView m_textureImageView{ nullptr };
    vk::raii::Sampler m_textureSampler{ nullptr };

    vk::raii::Image m_depthImage = nullptr;
    vk::raii::DeviceMemory m_depthImageMemory = nullptr;
    vk::raii::ImageView m_depthImageView = nullptr;


    vk::Extent2D m_swapChainExtent;
    vk::SurfaceFormatKHR m_swapChainSurfaceFormat;
    std::vector<vk::Image> m_swapChainImages = {};

    std::array<Vertex, 8> m_vertices = std::array {
        // Quad 1
        Vertex{{-0.5f, -0.5f, 0.0f}, {1, 0, 0, 1}, {0, 0}, glm::mat4(1.0f)},
        Vertex{{ 0.5f, -0.5f, 0.0f}, {0, 1, 0, 1}, {1, 0}, glm::mat4(1.0f)},
        Vertex{{ 0.5f,  0.5f, 0.0f}, {0, 0, 1, 1}, {1, 1}, glm::mat4(1.0f)},
        Vertex{{-0.5f,  0.5f, 0.0f}, {1, 1, 1, 1}, {0, 1}, glm::mat4(1.0f)},
        // Quad 2
        Vertex{{-0.5f, -0.5f, 0.0f}, {1, 0, 0, 1}, {0, 0}, glm::mat4(1.0f)},
        Vertex{{ 0.5f, -0.5f, 0.0f}, {0, 1, 0, 1}, {1, 0}, glm::mat4(1.0f)},
        Vertex{{ 0.5f,  0.5f, 0.0f}, {0, 0, 1, 1}, {1, 1}, glm::mat4(1.0f)},
        Vertex{{-0.5f,  0.5f, 0.0f}, {1, 1, 1, 1}, {0, 1}, glm::mat4(1.0f)}
    };

    const std::vector<uint16_t> m_indices = {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4
    };

    Transform m_transform1, m_transform2, m_cameraTransform;
    SceneCamera m_camera;

    int m_currentFrame = 0;
    bool m_framebufferResized = false;

    GLFWwindow* m_window = nullptr;
    glm::vec2 m_windowSize = { 1440 , 1080 };
    const std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };
#ifdef NDEBUG
    const bool m_enableValidationLayers = false;
#else
    const bool m_enableValidationLayers = true;
#endif
};
