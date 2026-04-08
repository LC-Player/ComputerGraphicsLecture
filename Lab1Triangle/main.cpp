#include "type.h"
#include "Camera.h"
#include "Transform.h"

#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"

#include "imgui/imgui.h"

#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <map>
#include <sstream>
#include <fstream>
#include <optional>

#include <cstring>
#include <cstdlib>
#include <cassert>

static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }

    std::vector<char> buffer(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    file.close();

    return buffer;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

    return VK_FALSE;
}

static void assertPhysicalDeviceSupportsVulkanVersion(const vk::PhysicalDevice& device, uint32_t vulkanApiVersion) {
    bool supportsVulkanVer = device.getProperties().apiVersion >= vulkanApiVersion;
    if (!supportsVulkanVer) {
        std::ostringstream out;
        out << "Selected physical device doesn't support vulkan "
            << vk::versionMajor(vulkanApiVersion) << "." << vk::versionMinor(vulkanApiVersion) << "!";
        throw std::runtime_error(out.str());
    }
}

static void assertPhysicalDeviceSupportsGraphicsFamily(const vk::PhysicalDevice& device) {
    auto queueFamilies = device.getQueueFamilyProperties();
    bool supportsGraphics = std::any_of(queueFamilies.begin(), queueFamilies.end(), [](const vk::QueueFamilyProperties& qfp) {
        return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
        });
    if (!supportsGraphics) {
        throw std::runtime_error("Selected physical device doesn't support graphic family!");
    }
}

static void assertPhysicalDeviceSupportsExtension(const vk::PhysicalDevice& device) {
    std::vector<const char*> requiredDeviceExtension = { vk::KHRSwapchainExtensionName };

    auto availableDeviceExtensions = device.enumerateDeviceExtensionProperties();
    bool supportsAllRequiredExtensions = std::all_of(
        requiredDeviceExtension.begin(),
        requiredDeviceExtension.end(),
        [&availableDeviceExtensions](auto const& requiredDeviceExtension) {
            return std::any_of(
                availableDeviceExtensions.begin(),
                availableDeviceExtensions.end(),
                [requiredDeviceExtension](auto const& availableDeviceExtension) {
                    return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0;
                });
        });

    if (!supportsAllRequiredExtensions) {
        throw std::runtime_error("Selected physical device doesn't support extensions!");
    }
}

static void assertPhysicalDeviceSupportsFeatures(const vk::PhysicalDevice& device) {
    auto features = device.template getFeatures2<
        vk::PhysicalDeviceFeatures2,
        vk::PhysicalDeviceVulkan13Features,
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();

    bool supportsRequiredFeatures =
        features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
        features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;

    if (!supportsRequiredFeatures) {
        throw std::runtime_error("Selected physical device doesn't support features!");
    }
}

static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const& availableFormats) {
    assert(!availableFormats.empty());
    const auto formatIt = std::find_if(
        availableFormats.begin(), availableFormats.end(),
        [](const auto& format) { return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; });
    return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
}

static vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const& availablePresentModes) {
    assert(std::any_of(availablePresentModes.begin(), availablePresentModes.end(), [](auto presentMode) { return presentMode == vk::PresentModeKHR::eFifo; }));
    return std::any_of(availablePresentModes.begin(), availablePresentModes.end(),
        [](const vk::PresentModeKHR value) { return vk::PresentModeKHR::eMailbox == value; }) ?
        vk::PresentModeKHR::eMailbox :
        vk::PresentModeKHR::eFifo;
}

static uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& surfaceCapabilities) {
    auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
    if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount)) {
        minImageCount = surfaceCapabilities.maxImageCount;
    }
    return minImageCount;
}

static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
class HelloTriangleApplication {
public:
    HelloTriangleApplication() = default;
    ~HelloTriangleApplication() = default;

    void run() {
        initWindow();
        initVulkan();
        initImGui();
        mainLoop();
        cleanup();
    }

private:
    friend void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        m_window = glfwCreateWindow(m_windowSize.x, m_windowSize.y, "Vulkan", nullptr, nullptr);
        if (!m_window) {
            glfwTerminate();
            throw std::runtime_error("Failed to create GLFW window");
        }
        glfwSetWindowUserPointer(m_window, this);
        glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
    }

    void initImGui() {
        vk::DescriptorPoolSize pool_sizes[] = {
            { vk::DescriptorType::eCombinedImageSampler, 1000 },
        };

        vk::DescriptorPoolCreateInfo pool_info{};
        pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        pool_info.maxSets = 1000;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = pool_sizes;

        m_imguiPool = m_device.createDescriptorPool(pool_info);

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();

        ImGui_ImplGlfw_InitForVulkan(m_window, true);

        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.Instance = *m_instance;
        init_info.PhysicalDevice = *m_physicalDevice;
        init_info.Device = *m_device;
        init_info.Queue = *m_graphicsQueue;
        init_info.DescriptorPool = *m_imguiPool;
        init_info.MinImageCount = m_swapChainImages.size();
        init_info.ImageCount = m_swapChainImages.size();

        ImGui_ImplVulkan_Init(&init_info, *m_renderPass);

        auto& cmd = m_commandBuffers[0];
        cmd.reset();

        vk::CommandBufferBeginInfo beginInfo{};
        cmd.begin(beginInfo);

        ImGui_ImplVulkan_CreateFontsTexture(*cmd);

        cmd.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.setCommandBuffers(*cmd);

        m_graphicsQueue.submit(submitInfo);
        m_graphicsQueue.waitIdle();

        ImGui_ImplVulkan_DestroyFontUploadObjects();        
    }

    void createInstance() {
        vk::ApplicationInfo appInfo;
        appInfo.pApplicationName = "Hello Triangle";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = vk::ApiVersion13;
        vk::InstanceCreateInfo createInfo;

        createInfo.pApplicationInfo = &appInfo;

        auto extensions = getRequiredExtensions();
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        // Check if the required GLFW extensions are supported by the Vulkan implementation.
        auto extensionProperties = m_context.enumerateInstanceExtensionProperties();

        std::cout << "available extensions:\n";

        for (const auto& extension : extensionProperties) {
            std::cout << '\t' << extension.extensionName << '\n';
        }

        for (uint32_t i = 0; i < extensions.size(); ++i) {
            if (std::none_of(extensionProperties.begin(), extensionProperties.end(),
                [glfwExtension = extensions[i]](auto const& extensionProperty) {
                    return strcmp(extensionProperty.extensionName, glfwExtension) == 0;
                })) {
                throw std::runtime_error("Required GLFW extension not supported: " + std::string(extensions[i]));
            }
        }

        // validation layer

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};

        if (m_enableValidationLayers && !checkValidationLayerSupport()) {
            throw std::runtime_error("validation layers requested, but not available!");
        }
        if (m_enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
            createInfo.ppEnabledLayerNames = m_validationLayers.data();

            populateDebugMessengerCreateInfo(debugCreateInfo);
            createInfo.pNext = &debugCreateInfo;
        }
        else {
            createInfo.enabledLayerCount = 0;
            createInfo.pNext = nullptr; 
        }
        m_instance = m_context.createInstance(createInfo);

    }

    void createSurface() {
        VkSurfaceKHR       _surface;
        if (glfwCreateWindowSurface(*m_instance, m_window, nullptr, &_surface) != 0) {
            throw std::runtime_error("failed to create window surface!");
        }
        m_surface = vk::raii::SurfaceKHR(m_instance, _surface);
    }

    void initVulkan() {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createRenderPass();
        createFramebuffers();
        createDescriptorSetLayout();
        createGraphicsPipeline();
        createCommandPool();
        createCommandBuffers();
        createSyncObjects();

        createVertexBuffer();
        createUniformBuffers();
    }

    void mainLoop() {
        while (!glfwWindowShouldClose(m_window)) {

            // update logic
            m_transform.rotation.z += 0.01;

            glfwPollEvents();
            drawFrame();
        }
        m_device.waitIdle();
    }
    
    void drawFrame() {


        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Transform");

        ImGui::SetWindowFontScale(2);

        ImGui::DragFloat3("Translation", glm::value_ptr(m_transform.translation), 0.01f);
        ImGui::DragFloat3("Rotation", glm::value_ptr(m_transform.rotation), 0.01f);
        ImGui::DragFloat3("Scale", glm::value_ptr(m_transform.scale), 0.01f);

        ImGui::ColorEdit3("Color1", glm::value_ptr(m_vertices[0].color));
        ImGui::ColorEdit3("Color2", glm::value_ptr(m_vertices[1].color));
        ImGui::ColorEdit3("Color3", glm::value_ptr(m_vertices[2].color));

        ImGui::End();

        ImGui::Render();

        auto result = m_device.waitForFences(*m_inFlightFences[m_currentFrame], true, UINT64_MAX);

        if (result != vk::Result::eSuccess) {
            throw std::runtime_error{ "waitForFences in drawFrame was failed" };
        }

        auto [nxtRes, imageIndex] = m_swapChain.acquireNextImage(
            std::numeric_limits<uint64_t>::max(), 
            m_imageAvailableSemaphores[m_currentFrame]
        );
        if (nxtRes == vk::Result::eErrorOutOfDateKHR) {
            recreateSwapChain();
            return;
        }

        if (nxtRes != vk::Result::eSuccess && nxtRes != vk::Result::eSuboptimalKHR) {
            throw std::runtime_error("Failed to acquire swap chain image!");
        }

        // update vertex buffers
        auto transformedVertices = getTransformedVertices();

        memcpy(m_mappedVertexData, transformedVertices.data(),
            sizeof(Vertex) * transformedVertices.size());

        // Only reset the fence if we are submitting work
        m_device.resetFences(*m_inFlightFences[m_currentFrame]);

        m_commandBuffers[m_currentFrame].reset();
        recordCommandBuffer(m_commandBuffers[m_currentFrame], imageIndex);

        vk::SubmitInfo submitInfo;

        submitInfo.setWaitSemaphores(*m_imageAvailableSemaphores[m_currentFrame]);
        std::array<vk::PipelineStageFlags, 1> waitStages = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
        submitInfo.setWaitDstStageMask(waitStages);

        submitInfo.setCommandBuffers(*m_commandBuffers[m_currentFrame]);

        submitInfo.setSignalSemaphores(*m_renderFinishedSemaphores[imageIndex]);

        m_graphicsQueue.submit(submitInfo, m_inFlightFences[m_currentFrame]);

        vk::PresentInfoKHR presentInfo;
        presentInfo.setWaitSemaphores(*m_renderFinishedSemaphores[imageIndex]);

        presentInfo.setSwapchains(*m_swapChain);
        presentInfo.pImageIndices = &imageIndex;

        auto presentRes = m_graphicsQueue.presentKHR(presentInfo);
        if (presentRes == vk::Result::eErrorOutOfDateKHR || presentRes == vk::Result::eSuboptimalKHR || m_framebufferResized) {
            recreateSwapChain();
            return;
        }
        else if (presentRes != vk::Result::eSuccess) {
            throw std::runtime_error("presentKHR failed with unexpected error");
        }

        if (m_framebufferResized) {
            recreateSwapChain();
        }

        m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void cleanup() {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        if (m_mappedVertexData) {
            m_vertexBufferMemory.unmapMemory();
            m_mappedVertexData = nullptr;
        }
        if (m_window) {
            glfwDestroyWindow(m_window);
            m_window = nullptr;
        }
        glfwTerminate();
    }

    bool checkValidationLayerSupport() {

        auto availableLayers = m_context.enumerateInstanceLayerProperties();

        std::cout << "Available validation layers:\n";
        for (const auto& layer : availableLayers) {
            std::cout << "  " << layer.layerName << std::endl;
        }

        for (const char* layerName : m_validationLayers) {
            bool layerFound = false;

            for (const auto& layerProperties : availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }

            if (!layerFound) {
                return false;
            }
        }

        return true;
    }

    std::vector<const char*> getRequiredExtensions() {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        if (m_enableValidationLayers) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }

    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = 
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | 
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | 
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = 
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | 
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;
        createInfo.pUserData = nullptr;
    }

    bool isDeviceSuitable(vk::raii::PhysicalDevice const& physicalDevice) {
        auto deviceProperties = physicalDevice.getProperties();
        auto deviceFeatures = physicalDevice.getFeatures();

        if (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu && deviceFeatures.geometryShader) {
            return true;
        }

        return false;
    }

    void pickPhysicalDevice() {
        auto physicalDevices = vk::raii::PhysicalDevices(m_instance);
        if (physicalDevices.empty()) {
            throw std::runtime_error("failed to find GPUs with Vulkan support!");
        }

        // Use an ordered map to automatically sort candidates by increasing score
        std::multimap<int, vk::raii::PhysicalDevice> candidates;

        for (const auto& pd : physicalDevices) {
            auto deviceProperties = pd.getProperties();
            auto deviceFeatures = pd.getFeatures();
            uint32_t score = 0;

            // Discrete GPUs have a significant performance advantage
            if (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
                score += 1000;
            }

            // Maximum possible size of textures affects graphics quality
            score += deviceProperties.limits.maxImageDimension2D;

            // Application can't function without geometry shaders
            if (!deviceFeatures.geometryShader) {
                continue;
            }
            candidates.insert(std::make_pair(score, pd));
        }

        // Check if the best candidate is suitable at all
        if (!candidates.empty() && candidates.rbegin()->first > 0) {
            m_physicalDevice = candidates.rbegin()->second;
        }
        else {
            throw std::runtime_error("failed to find a suitable GPU!");
        }

        std::cout << "Selected Physical Device: " << m_physicalDevice.getProperties().deviceName << std::endl;

        assertPhysicalDeviceSupportsVulkanVersion(m_physicalDevice, vk::ApiVersion13);
        assertPhysicalDeviceSupportsGraphicsFamily(m_physicalDevice);
        assertPhysicalDeviceSupportsExtension(m_physicalDevice);
        assertPhysicalDeviceSupportsFeatures(m_physicalDevice);
    }



    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() const {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };

    QueueFamilyIndices findQueueFamilies(const vk::raii::PhysicalDevice& device) const {

        QueueFamilyIndices indices;
        auto queueFamilies = device.getQueueFamilyProperties();
        uint32_t i = 0;

        for (const auto& qfp : queueFamilies) {
            if (qfp.queueFlags & vk::QueueFlagBits::eGraphics) {
                indices.graphicsFamily = i;
            }

            if (device.getSurfaceSupportKHR(i, *m_surface)) {
                indices.presentFamily = i;
            }

            if (indices.graphicsFamily.has_value() && indices.presentFamily.has_value()) {
                break;
            }
            ++i;
        }

        return indices;
    }

    void createLogicalDevice() {
        QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);

        if (!indices.isComplete()) {
            throw std::runtime_error("Could not find suitable queue families!");
        }
        
        vk::DeviceQueueCreateInfo deviceQueueCreateInfo{};
        deviceQueueCreateInfo.queueFamilyIndex = indices.graphicsFamily.value();
        deviceQueueCreateInfo.queueCount = 1;
        float queuePriority = 0.5f;
        deviceQueueCreateInfo.pQueuePriorities = &queuePriority;

        vk::StructureChain<
            vk::PhysicalDeviceFeatures2,
            vk::PhysicalDeviceVulkan11Features,
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
        > featureChain = {
            vk::PhysicalDeviceFeatures2{},
            vk::PhysicalDeviceVulkan11Features{}.setShaderDrawParameters(true),
            vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT{}.setExtendedDynamicState(true)
        };

        std::vector<const char*> requiredDeviceExtension = {
            vk::KHRSwapchainExtensionName 
        };

        vk::DeviceCreateInfo deviceCreateInfo;
        deviceCreateInfo.pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>();
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &deviceQueueCreateInfo;
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size());
        deviceCreateInfo.ppEnabledExtensionNames = requiredDeviceExtension.data();
        
        m_device = m_physicalDevice.createDevice(deviceCreateInfo);
        m_graphicsQueue = m_device.getQueue(indices.graphicsFamily.value(), 0);
    }

    vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities) {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);

        return {
            std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
        };
    }

    void createSwapChain() {
        vk::SurfaceCapabilitiesKHR surfaceCapabilities = m_physicalDevice.getSurfaceCapabilitiesKHR(*m_surface);
        m_swapChainExtent = chooseSwapExtent(surfaceCapabilities);
        uint32_t minImageCount = chooseSwapMinImageCount(surfaceCapabilities);

        std::vector<vk::SurfaceFormatKHR> availableFormats = m_physicalDevice.getSurfaceFormatsKHR(*m_surface);
        m_swapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);

        std::vector<vk::PresentModeKHR> availablePresentModes =
            m_physicalDevice.getSurfacePresentModesKHR(*m_surface);

        vk::SwapchainCreateInfoKHR swapChainCreateInfo;
        swapChainCreateInfo.surface = *m_surface;
        swapChainCreateInfo.minImageCount = minImageCount;
        swapChainCreateInfo.imageFormat = m_swapChainSurfaceFormat.format;
        swapChainCreateInfo.imageColorSpace = m_swapChainSurfaceFormat.colorSpace;
        swapChainCreateInfo.imageExtent = m_swapChainExtent;
        swapChainCreateInfo.imageArrayLayers = 1;
        swapChainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
        swapChainCreateInfo.imageSharingMode = vk::SharingMode::eExclusive;
        swapChainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
        swapChainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        swapChainCreateInfo.presentMode = chooseSwapPresentMode(availablePresentModes);
        swapChainCreateInfo.clipped = true;
        swapChainCreateInfo.oldSwapchain = nullptr; 

        m_swapChain = m_device.createSwapchainKHR(swapChainCreateInfo);
        m_swapChainImages = m_swapChain.getImages();

        std::cout << "Swapchain image count: " << m_swapChainImages.size() << std::endl;
    }

    void createImageViews() {
        assert(m_swapChainImageViews.empty());

        vk::ImageViewCreateInfo imageViewCreateInfo;
        imageViewCreateInfo.viewType = vk::ImageViewType::e2D;
        imageViewCreateInfo.format = m_swapChainSurfaceFormat.format;
        imageViewCreateInfo.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
        imageViewCreateInfo.components = {
            vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity };
        for (auto& image : m_swapChainImages) {
            imageViewCreateInfo.image = image;
            m_swapChainImageViews.emplace_back(m_device, imageViewCreateInfo);
        }
    }

    void createRenderPass() {
        vk::AttachmentDescription colorAttachment;
        colorAttachment.format = m_swapChainSurfaceFormat.format;
        colorAttachment.samples = vk::SampleCountFlagBits::e1;
        colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
        colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

        vk::AttachmentReference colorAttachmentRef;
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subpass;
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.setColorAttachments(colorAttachmentRef);

        vk::RenderPassCreateInfo renderPassInfo;
        renderPassInfo.setAttachments(colorAttachment);
        renderPassInfo.setSubpasses(subpass);

        vk::SubpassDependency dependency;
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.srcAccessMask = {};
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        renderPassInfo.setDependencies(dependency);

        m_renderPass = m_device.createRenderPass(renderPassInfo);
    }

    void createFramebuffers() {
        m_swapChainFramebuffers.reserve(m_swapChainImageViews.size());
        vk::FramebufferCreateInfo framebufferInfo;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.width = m_swapChainExtent.width;
        framebufferInfo.height = m_swapChainExtent.height;
        framebufferInfo.layers = 1;
        for (const auto& imageView : m_swapChainImageViews) {
            framebufferInfo.setAttachments(*imageView);
            m_swapChainFramebuffers.emplace_back(m_device.createFramebuffer(framebufferInfo));
        }
    }

    [[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char>& code) const {
        vk::ShaderModuleCreateInfo createInfo;
        createInfo.codeSize = code.size() * sizeof(char);
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        vk::raii::ShaderModule shaderModule{ m_device, createInfo };
        return shaderModule;
    }

    void createDescriptorSetLayout() {
        vk::DescriptorSetLayoutBinding uboLayoutBinding;
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;

        vk::DescriptorSetLayoutCreateInfo layoutInfo;
        layoutInfo.setBindings(uboLayoutBinding);
        m_descriptorSetLayout = m_device.createDescriptorSetLayout(layoutInfo);
    }

    void createGraphicsPipeline() {
        auto shaderCode = readFile("shaders/shader.spv");
        vk::raii::ShaderModule shaderModule = createShaderModule(shaderCode);

        vk::PipelineShaderStageCreateInfo vertShaderStageInfo;
        vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
        vertShaderStageInfo.module = *shaderModule;
        vertShaderStageInfo.pName = "vertMain";
        vk::PipelineShaderStageCreateInfo fragShaderStageInfo;
        fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
        fragShaderStageInfo.module = *shaderModule;
        fragShaderStageInfo.pName = "fragMain";

        vk::PipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

        std::vector<vk::DynamicState> dynamicStates = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
        };
        vk::PipelineDynamicStateCreateInfo dynamicState;
        dynamicState.setDynamicStates(dynamicStates);

        vk::PipelineVertexInputStateCreateInfo vertexInputInfo;

        const auto bindingDescription = Vertex::getBindingDescription();
        const auto attributeDescriptions = Vertex::getAttributeDescriptions();
        vertexInputInfo.setVertexBindingDescriptions(bindingDescription);
        vertexInputInfo.setVertexAttributeDescriptions(attributeDescriptions);

        vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
        inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
        inputAssembly.primitiveRestartEnable = false;

        vk::Viewport viewport(
            0.0f, 0.0f,                                     // x y
            static_cast<float>(m_swapChainExtent.width),    // width
            static_cast<float>(m_swapChainExtent.height),   // height
            0.0f, 1.0f                                      // minDepth maxDepth
        );

        vk::Rect2D scissor(
            { 0, 0 },             // offset
            m_swapChainExtent   // Extent2D
        );

        vk::PipelineViewportStateCreateInfo viewportState;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        vk::PipelineRasterizationStateCreateInfo rasterizer;
        rasterizer.depthClampEnable = false;
        rasterizer.rasterizerDiscardEnable = false;
        rasterizer.polygonMode = vk::PolygonMode::eFill;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = vk::CullModeFlagBits::eBack;
        rasterizer.frontFace = vk::FrontFace::eClockwise;
        rasterizer.depthBiasEnable = false;

        vk::PipelineMultisampleStateCreateInfo multisampling;
        multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
        multisampling.sampleShadingEnable = false;

        vk::PipelineColorBlendAttachmentState colorBlendAttachment;
        colorBlendAttachment.blendEnable = true;
        colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
        colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
        colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
        colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR |
            vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB |
            vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo colorBlending;
        colorBlending.logicOpEnable = false;
        colorBlending.logicOp = vk::LogicOp::eCopy;
        colorBlending.setAttachments(colorBlendAttachment);

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
        pipelineLayoutInfo.setSetLayouts(*m_descriptorSetLayout);
        m_pipelineLayout = m_device.createPipelineLayout(pipelineLayoutInfo);

        vk::GraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;              
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;     
        pipelineInfo.pRasterizationState = &rasterizer;   
        pipelineInfo.pMultisampleState = &multisampling;  
        pipelineInfo.pColorBlendState = &colorBlending;   
        pipelineInfo.pDynamicState = &dynamicState;       
        pipelineInfo.layout = m_pipelineLayout;           
        pipelineInfo.renderPass = m_renderPass;
        pipelineInfo.subpass = 0;

        m_graphicsPipeline = m_device.createGraphicsPipeline(nullptr, pipelineInfo);

    };

    void createCommandPool() {
        const auto [graphicsFamily, presentFamily] = findQueueFamilies(m_physicalDevice);
        
        vk::CommandPoolCreateInfo poolInfo;
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        poolInfo.queueFamilyIndex = graphicsFamily.value();

        m_commandPool = m_device.createCommandPool(poolInfo);
    }

    void recordCommandBuffer(const vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex) const {
        constexpr vk::CommandBufferBeginInfo beginInfo;
        commandBuffer.begin(beginInfo);

        vk::RenderPassBeginInfo renderPassInfo;
        renderPassInfo.renderPass = m_renderPass;
        renderPassInfo.framebuffer = m_swapChainFramebuffers[imageIndex];

        renderPassInfo.renderArea.offset = vk::Offset2D{ 0, 0 };
        renderPassInfo.renderArea.extent = m_swapChainExtent;
        vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
        renderPassInfo.setClearValues(clearColor);

        commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline);

        const vk::Viewport viewport(
            0.0f, 0.0f, // x, y
            static_cast<float>(m_swapChainExtent.width),    // width
            static_cast<float>(m_swapChainExtent.height),   // height
            0.0f, 1.0f  // minDepth maxDepth
        );
        commandBuffer.setViewport(0, viewport);

        const vk::Rect2D scissor(
            vk::Offset2D{ 0, 0 }, // offset
            m_swapChainExtent   // extent
        );
        commandBuffer.setScissor(0, scissor);

        const std::array<vk::Buffer, 1> vertexBuffers{ m_vertexBuffer };
        constexpr std::array<vk::DeviceSize, 1> offsets{ 0 };
        commandBuffer.bindVertexBuffers(0, vertexBuffers, offsets);

        commandBuffer.draw(static_cast<uint32_t>(m_vertices.size()), 1, 0, 0);

        ImGui_ImplVulkan_RenderDrawData(
            ImGui::GetDrawData(),
            *commandBuffer
        );

        commandBuffer.endRenderPass();
        commandBuffer.end();
    }

    void createCommandBuffers() {
        vk::CommandBufferAllocateInfo allocInfo;
        allocInfo.commandPool = m_commandPool;
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

        m_commandBuffers = m_device.allocateCommandBuffers(allocInfo);
    }

    void createSyncObjects() {
        constexpr vk::SemaphoreCreateInfo semaphoreInfo;
        constexpr vk::FenceCreateInfo fenceInfo(
            vk::FenceCreateFlagBits::eSignaled  // flags
        );
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            m_imageAvailableSemaphores.emplace_back(m_device, semaphoreInfo);
            m_inFlightFences.emplace_back(m_device, fenceInfo);
        }
        for (size_t i = 0; i < m_swapChainImages.size(); ++i) {
            m_renderFinishedSemaphores.emplace_back(m_device, semaphoreInfo);
        }
    }

    void recreateSwapChain() {
        int width = 0, height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(m_window, &width, &height);
            glfwWaitEvents();
        }

        m_device.waitIdle();

        m_swapChainFramebuffers.clear();
        m_swapChainImageViews.clear();
        m_swapChainImages.clear();
        m_swapChain = nullptr;

        createSwapChain();
        createImageViews();
        createFramebuffers();

        m_framebufferResized = false;
    }

    uint32_t findMemoryType(const uint32_t typeFilter, const vk::MemoryPropertyFlags properties) const {
        const auto memProperties = m_physicalDevice.getMemoryProperties();
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
            if ((typeFilter & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties
                ) return i;
        }
        throw std::runtime_error("failed to find suitable memory type!");
        return 0; // optional
    }

    void createVertexBuffer() {
        vk::BufferCreateInfo bufferInfo;
        bufferInfo.size = sizeof(Vertex) * m_vertices.size();
        bufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer;
        bufferInfo.sharingMode = vk::SharingMode::eExclusive;

        m_vertexBuffer = m_device.createBuffer(bufferInfo);

        const vk::MemoryRequirements memRequirements = m_vertexBuffer.getMemoryRequirements();

        vk::MemoryAllocateInfo allocInfo;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(
            memRequirements.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent
        );
        m_vertexBufferMemory = m_device.allocateMemory(allocInfo);
        m_vertexBuffer.bindMemory(m_vertexBufferMemory, 0);

        m_mappedVertexData = m_vertexBufferMemory.mapMemory(0, bufferInfo.size);
    }

    void createUniformBuffers() {
#if 0
        constexpr vk::DeviceSize bufferSize = sizeof(UniformBufferObject);

        m_uniformBuffers.reserve(MAX_FRAMES_IN_FLIGHT);
        m_uniformBuffersMemory.reserve(MAX_FRAMES_IN_FLIGHT);
        m_uniformBuffersMapped.reserve(MAX_FRAMES_IN_FLIGHT);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            m_uniformBuffers.emplace_back(nullptr);
            m_uniformBuffersMemory.emplace_back(nullptr);
            m_uniformBuffersMapped.emplace_back(nullptr);
            createBuffer(bufferSize,
                vk::BufferUsageFlagBits::eUniformBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent,
                m_uniformBuffers[i],
                m_uniformBuffersMemory[i]
            );

            m_uniformBuffersMapped[i] = m_uniformBuffersMemory[i].mapMemory(0, bufferSize);
        }
#endif
    }

    const std::vector<Vertex> getTransformedVertices() {
        std::vector<Vertex> vert = {};
        for (const auto& in : m_vertices) {
            vert.emplace_back(Vertex{ 
                m_transform() * glm::vec4{ in.pos, 1.0f }, 
                in.color 
            });
        }
        return vert;
    }

private:
    constexpr static int MAX_FRAMES_IN_FLIGHT = 2;

    vk::raii::Context m_context;
    vk::raii::Instance m_instance = nullptr;
    vk::raii::PhysicalDevice m_physicalDevice = nullptr;
    vk::raii::Device m_device = nullptr;
    vk::raii::Queue m_graphicsQueue = nullptr;
    vk::raii::SurfaceKHR m_surface = nullptr;
    vk::raii::RenderPass m_renderPass = nullptr;
    vk::raii::DescriptorSetLayout m_descriptorSetLayout = nullptr;
    vk::raii::PipelineLayout m_pipelineLayout = nullptr;
    vk::raii::Pipeline m_graphicsPipeline = nullptr;
    vk::raii::CommandPool m_commandPool = nullptr;
    vk::raii::SwapchainKHR m_swapChain = nullptr;
    vk::raii::DeviceMemory m_vertexBufferMemory = nullptr;
    vk::raii::Buffer m_vertexBuffer = nullptr;
    vk::raii::DescriptorPool m_imguiPool = nullptr;
    std::vector<vk::raii::ImageView> m_swapChainImageViews;
    std::vector<vk::raii::Framebuffer> m_swapChainFramebuffers;
    std::vector<vk::raii::CommandBuffer> m_commandBuffers;
    std::vector<vk::raii::Semaphore> m_imageAvailableSemaphores;
    std::vector<vk::raii::Semaphore> m_renderFinishedSemaphores;
    std::vector<vk::raii::Fence> m_inFlightFences;

    vk::raii::DeviceMemory m_indexBufferMemory{ nullptr };
    vk::raii::Buffer m_indexBuffer{ nullptr };
    std::vector<vk::raii::DeviceMemory> m_uniformBuffersMemory;
    std::vector<vk::raii::Buffer> m_uniformBuffers;
    std::vector<void*> m_uniformBuffersMapped;

    void* m_mappedVertexData = nullptr;

    vk::Extent2D m_swapChainExtent;
    vk::SurfaceFormatKHR m_swapChainSurfaceFormat;
    std::vector<vk::Image> m_swapChainImages = {};

    std::vector<Vertex> m_vertices = {
            {{0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
            {{0.5f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
            {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}}

    };

    Transform m_transform;
    SceneCamera m_Camera;

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

int main() {
    try {
        HelloTriangleApplication app;
        app.run();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    HelloTriangleApplication* app = static_cast<HelloTriangleApplication*>(glfwGetWindowUserPointer(window));
    app->m_framebufferResized = true;
}