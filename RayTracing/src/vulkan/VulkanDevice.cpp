#include "VulkanDevice.hpp"
#include "SwapChainManager.hpp"
#include <set>
#include <algorithm>
#include <map>

namespace RYRayTracing {

VulkanDevice::VulkanDevice(VkInstance instance, VkSurfaceKHR surface, const DeviceConfig& config)
    : instance(instance)
    , surface(surface)
    , physicalDevice(VK_NULL_HANDLE)
    , device(VK_NULL_HANDLE)
    , graphicsQueue(VK_NULL_HANDLE)
    , presentQueue(VK_NULL_HANDLE)
    , config(config)
    , initialized(false)
    , graphicsQueueFamily(UINT32_MAX)
    , presentQueueFamily(UINT32_MAX) {

    LOG_INFO("Creating Vulkan device");

    // Validate inputs
    if (instance == VK_NULL_HANDLE) {
        throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                            "Vulkan instance is null",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    if (surface == VK_NULL_HANDLE) {
        throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                            "Surface is null",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    pickPhysicalDevice();
    createLogicalDevice();

    initialized = true;
    LOG_INFO("Vulkan device created successfully");
}

VulkanDevice::~VulkanDevice() {
    LOG_DEBUG("Destroying Vulkan device");

    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }

    // Destroy surface
    if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }

    // Note: We don't destroy physicalDevice as it's owned by the instance
    LOG_DEBUG("Vulkan device destroyed");
}

VulkanDevice::VulkanDevice(VulkanDevice&& other) noexcept
    : instance(other.instance)
    , surface(other.surface)
    , physicalDevice(other.physicalDevice)
    , device(other.device)
    , graphicsQueue(other.graphicsQueue)
    , presentQueue(other.presentQueue)
    , queueFamilyIndices(other.queueFamilyIndices)
    , deviceProperties(other.deviceProperties)
    , deviceFeatures(other.deviceFeatures)
    , config(std::move(other.config))
    , enabledExtensions(std::move(other.enabledExtensions))
    , initialized(other.initialized)
    , graphicsQueueFamily(other.graphicsQueueFamily)
    , presentQueueFamily(other.presentQueueFamily) {
    other.instance = VK_NULL_HANDLE;
    other.surface = VK_NULL_HANDLE;
    other.instance = VK_NULL_HANDLE;
    other.surface = VK_NULL_HANDLE;
    other.device = VK_NULL_HANDLE;
    other.physicalDevice = VK_NULL_HANDLE;
    other.graphicsQueue = VK_NULL_HANDLE;
    other.presentQueue = VK_NULL_HANDLE;
    other.initialized = false;
}

VulkanDevice& VulkanDevice::operator=(VulkanDevice&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        if (device != VK_NULL_HANDLE) {
            vkDestroyDevice(device, nullptr);
        }

        // Destroy current surface if it exists
        if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
        }

        // Move resources from other
        instance = other.instance;
        surface = other.surface;
        physicalDevice = other.physicalDevice;
        device = other.device;
        graphicsQueue = other.graphicsQueue;
        presentQueue = other.presentQueue;
        queueFamilyIndices = other.queueFamilyIndices;
        deviceProperties = other.deviceProperties;
        deviceFeatures = other.deviceFeatures;
        config = std::move(other.config);
        enabledExtensions = std::move(other.enabledExtensions);
        initialized = other.initialized;
        graphicsQueueFamily = other.graphicsQueueFamily;
        presentQueueFamily = other.presentQueueFamily;

        // Reset other
        other.instance = VK_NULL_HANDLE;
        other.surface = VK_NULL_HANDLE;
        other.device = VK_NULL_HANDLE;
        other.physicalDevice = VK_NULL_HANDLE;
        other.graphicsQueue = VK_NULL_HANDLE;
        other.presentQueue = VK_NULL_HANDLE;
        other.initialized = false;
    }
    return *this;
}

SwapChainSupportDetails VulkanDevice::getSwapChainSupportDetails(VkPhysicalDevice device) const {
    SwapChainSupportDetails details;

    LOG_DEBUG("Getting swap chain support details for physical device");

    if (device == VK_NULL_HANDLE) {
        throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                            "Physical device is null",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    if (surface == VK_NULL_HANDLE) {
        throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                            "Surface is null",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Get capabilities
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);
    if (result != VK_SUCCESS) {
        throw VulkanException(result,
                            "Failed to get surface capabilities",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Get formats
    uint32_t formatCount;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (result != VK_SUCCESS) {
        throw VulkanException(result,
                            "Failed to get surface format count",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
        if (result != VK_SUCCESS) {
            throw VulkanException(result,
                                "Failed to get surface formats",
                                __FUNCTION__, __FILE__, __LINE__);
        }
    }

    // Get present modes
    uint32_t presentModeCount;
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (result != VK_SUCCESS) {
        throw VulkanException(result,
                            "Failed to get present mode count",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface,
                                                          &presentModeCount, details.presentModes.data());
        if (result != VK_SUCCESS) {
            throw VulkanException(result,
                                "Failed to get present modes",
                                __FUNCTION__, __FILE__, __LINE__);
        }
    }

    return details;
}

uint32_t VulkanDevice::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw VulkanException(VK_ERROR_OUT_OF_DEVICE_MEMORY,
                         "Failed to find suitable memory type",
                         __FUNCTION__, __FILE__, __LINE__);
}

void VulkanDevice::waitIdle() const {
    if (device != VK_NULL_HANDLE) {
        VK_CHECK_RESULT(vkDeviceWaitIdle(device));
    }
}

void VulkanDevice::pickPhysicalDevice() {
    LOG_DEBUG("Picking physical device");

    // Get available physical devices
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                            "Failed to find GPUs with Vulkan support",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    LOG_INFO("Found " + std::to_string(deviceCount) + " physical device(s)");

    // Rate each device and pick the best one
    std::multimap<int, VkPhysicalDevice> candidates;

    for (const auto& device : devices) {
        int score = rateDeviceSuitability(device);
        candidates.insert(std::make_pair(score, device));
    }

    // Check if the best candidate is suitable
    auto bestCandidate = candidates.rbegin();
    if (bestCandidate->first > 0) {
        physicalDevice = bestCandidate->second;
        vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
        vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);

        LOG_INFO("Selected physical device: " + std::string(deviceProperties.deviceName));
        LOG_INFO("  - Device type: " + std::to_string(deviceProperties.deviceType));
        LOG_INFO("  - API version: " +
                 std::to_string(VK_VERSION_MAJOR(deviceProperties.apiVersion)) + "." +
                 std::to_string(VK_VERSION_MINOR(deviceProperties.apiVersion)) + "." +
                 std::to_string(VK_VERSION_PATCH(deviceProperties.apiVersion)));
        LOG_INFO("  - Driver version: " + std::to_string(deviceProperties.driverVersion));
    } else {
        throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                            "Failed to find a suitable GPU",
                            __FUNCTION__, __FILE__, __LINE__);
    }
}

void VulkanDevice::createLogicalDevice() {
    LOG_DEBUG("Creating logical device");

    // Find queue families
    queueFamilyIndices = findQueueFamilies(physicalDevice);

    if (!queueFamilyIndices.isComplete()) {
        throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                            "Failed to find required queue families",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Create queue create infos
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        queueFamilyIndices.graphicsFamily.value(),
        queueFamilyIndices.presentFamily.value()
    };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Check device extension support
    if (!checkDeviceExtensionSupport(physicalDevice)) {
        throw VulkanException(VK_ERROR_EXTENSION_NOT_PRESENT,
                            "Device does not support required extensions",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Device features
    VkPhysicalDeviceFeatures deviceFeatures = {};
    // Enable features as needed
    // deviceFeatures.samplerAnisotropy = VK_TRUE;

    // Device create info
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(config.requiredExtensions.size());
    createInfo.ppEnabledExtensionNames = config.requiredExtensions.data();

    // Validation layers (deprecated on newer Vulkan, but kept for compatibility)
    // Note: Device-specific validation layers are deprecated, but we still need to
    // enable them for compatibility with older Vulkan implementations.
    // In modern Vulkan, validation layers are enabled at instance level only.

    VK_CHECK_RESULT(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device));

    // Get queue handles
    graphicsQueueFamily = queueFamilyIndices.graphicsFamily.value();
    presentQueueFamily = queueFamilyIndices.presentFamily.value();

    vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(device, presentQueueFamily, 0, &presentQueue);

    LOG_INFO("Logical device created successfully");
    LOG_INFO("  - Graphics queue family: " + std::to_string(graphicsQueueFamily));
    LOG_INFO("  - Present queue family: " + std::to_string(presentQueueFamily));
}

bool VulkanDevice::isDeviceSuitable(VkPhysicalDevice device) const {
    // Find queue families
    QueueFamilyIndices indices = findQueueFamilies(device);
    if (!indices.isComplete()) {
        return false;
    }

    // Check extension support
    if (!checkDeviceExtensionSupport(device)) {
        return false;
    }

    // Check swap chain support
    SwapChainSupportDetails swapChainSupport = getSwapChainSupportDetails(device);
    if (swapChainSupport.formats.empty() || swapChainSupport.presentModes.empty()) {
        return false;
    }

    // Check required features
    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

    // Check each required feature
    // Example: if (config.requiredFeatures.samplerAnisotropy && !supportedFeatures.samplerAnisotropy) return false;

    return true;
}

QueueFamilyIndices VulkanDevice::findQueueFamilies(VkPhysicalDevice device) const {
    QueueFamilyIndices indices;

    // Get queue family properties
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    // Find suitable queue families
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        const auto& queueFamily = queueFamilies[i];

        // Check for graphics support
        if (config.requireGraphicsQueue && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        // Check for present support
        if (config.requirePresentQueue) {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
            if (presentSupport) {
                indices.presentFamily = i;
            }
        }

        // Check for transfer support (optional)
        if (config.requireTransferQueue && queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) {
            // Could store this if needed
        }

        // Check for compute support (optional)
        if (config.requireComputeQueue && queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            // Could store this if needed
        }

        // If we found all required queues, break early
        if (indices.isComplete()) {
            break;
        }
    }

    return indices;
}

bool VulkanDevice::checkDeviceExtensionSupport(VkPhysicalDevice device) const {
    // Get available extensions
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    // Create a set of required extensions
    std::set<std::string> requiredExtensions(config.requiredExtensions.begin(),
                                            config.requiredExtensions.end());

    // Check if all required extensions are available
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

int VulkanDevice::rateDeviceSuitability(VkPhysicalDevice device) const {
    int score = 0;

    // Get device properties and features
    VkPhysicalDeviceProperties deviceProperties;
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    // Discrete GPUs have a significant performance advantage
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    }

    // Maximum possible size of textures affects graphics quality
    score += deviceProperties.limits.maxImageDimension2D;

    // Application can't function without geometry shaders
    if (!deviceFeatures.geometryShader) {
        return 0;
    }

    // Check if device is suitable
    if (!isDeviceSuitable(device)) {
        return 0;
    }

    // Additional scoring based on features
    if (deviceFeatures.samplerAnisotropy) {
        score += 100;
    }

    if (deviceFeatures.textureCompressionBC) {
        score += 50;
    }

    if (deviceFeatures.fillModeNonSolid) {
        score += 25;
    }

    return score;
}

void VulkanDevice::logDeviceInfo(VkPhysicalDevice device) const {
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceProperties(device, &properties);
    vkGetPhysicalDeviceFeatures(device, &features);

    LOG_DEBUG("Device: " + std::string(properties.deviceName));
    LOG_DEBUG("  - Type: " + std::to_string(properties.deviceType));
    LOG_DEBUG("  - API Version: " +
              std::to_string(VK_VERSION_MAJOR(properties.apiVersion)) + "." +
              std::to_string(VK_VERSION_MINOR(properties.apiVersion)) + "." +
              std::to_string(VK_VERSION_PATCH(properties.apiVersion)));
    LOG_DEBUG("  - Driver Version: " + std::to_string(properties.driverVersion));
    LOG_DEBUG("  - Vendor ID: " + std::to_string(properties.vendorID));
    LOG_DEBUG("  - Device ID: " + std::to_string(properties.deviceID));
}

} // namespace RYRayTracing