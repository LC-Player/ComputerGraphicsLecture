// main.cpp
#include "Application.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <fstream>
#include <cassert>
#include <cstring>
#include <cstdlib>

std::vector<char> readFile(const std::string& filename) {
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

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

    return VK_FALSE;
}

void assertPhysicalDeviceSupportsVulkanVersion(const vk::PhysicalDevice& device, uint32_t vulkanApiVersion) {
    bool supportsVulkanVer = device.getProperties().apiVersion >= vulkanApiVersion;
    if (!supportsVulkanVer) {
        std::ostringstream out;
        out << "Selected physical device doesn't support vulkan "
            << vk::versionMajor(vulkanApiVersion) << "." << vk::versionMinor(vulkanApiVersion) << "!";
        throw std::runtime_error(out.str());
    }
}

void assertPhysicalDeviceSupportsGraphicsFamily(const vk::PhysicalDevice& device) {
    auto queueFamilies = device.getQueueFamilyProperties();
    bool supportsGraphics = std::any_of(queueFamilies.begin(), queueFamilies.end(), [](const vk::QueueFamilyProperties& qfp) {
        return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
        });
    if (!supportsGraphics) {
        throw std::runtime_error("Selected physical device doesn't support graphic family!");
    }
}

void assertPhysicalDeviceSupportsExtension(const vk::PhysicalDevice& device) {
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

void assertPhysicalDeviceSupportsFeatures(const vk::PhysicalDevice& device) {
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

vk::SurfaceFormatKHR chooseSwapSurfaceFormat(std::vector<vk::SurfaceFormatKHR> const& availableFormats) {
    assert(!availableFormats.empty());
    const auto formatIt = std::find_if(
        availableFormats.begin(), availableFormats.end(),
        [](const auto& format) { return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; });
    return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
}

vk::PresentModeKHR chooseSwapPresentMode(std::vector<vk::PresentModeKHR> const& availablePresentModes) {
    assert(std::any_of(availablePresentModes.begin(), availablePresentModes.end(), [](auto presentMode) { return presentMode == vk::PresentModeKHR::eFifo; }));
    return std::any_of(availablePresentModes.begin(), availablePresentModes.end(),
        [](const vk::PresentModeKHR value) { return vk::PresentModeKHR::eMailbox == value; }) ?
        vk::PresentModeKHR::eMailbox :
        vk::PresentModeKHR::eFifo;
}

uint32_t chooseSwapMinImageCount(vk::SurfaceCapabilitiesKHR const& surfaceCapabilities) {
    auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
    if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount < minImageCount)) {
        minImageCount = surfaceCapabilities.maxImageCount;
    }
    return minImageCount;
}

void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    HelloTriangleApplication* app = static_cast<HelloTriangleApplication*>(glfwGetWindowUserPointer(window));
    app->m_framebufferResized = true;
    app->m_camera.SetAspectRatio(static_cast<float>(width) / height);
}

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
