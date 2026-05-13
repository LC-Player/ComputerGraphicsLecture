#include "VulkanDevice.hpp"
#include "SwapChainManager.hpp"
#include <set>
#include <algorithm>
#include <map>

namespace RYRayTracing {

VulkanDevice::VulkanDevice(vk::raii::Instance& instance, vk::raii::SurfaceKHR& surface,
                          const DeviceConfig& config)
    : config(config)
    , graphicsQueueFamily(UINT32_MAX)
    , presentQueueFamily(UINT32_MAX) {

    LOG_INFO("Creating Vulkan device");

    pickPhysicalDevice(instance, surface);
    createLogicalDevice(surface);

    LOG_INFO("Vulkan device created successfully");
}

SwapChainSupportDetails VulkanDevice::getSwapChainSupportDetails(vk::PhysicalDevice device) const {
    SwapChainSupportDetails details;

    LOG_DEBUG("Getting swap chain support details for physical device");

    // Get capabilities, formats, and present modes
    // Note: This will be called with the surface from SwapChainManager
    // We need to pass the surface separately or store it

    return details;
}

uint32_t VulkanDevice::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const {
    auto memProperties = physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw VulkanException(vk::Result::eErrorOutOfDeviceMemory,
                         "Failed to find suitable memory type",
                         __FUNCTION__, __FILE__, __LINE__);
}

void VulkanDevice::waitIdle() const {
    device.waitIdle();
}

void VulkanDevice::pickPhysicalDevice(vk::raii::Instance& instance, vk::raii::SurfaceKHR& surface) {
    LOG_DEBUG("Picking physical device");

    // Get available physical devices
    auto devices = instance.enumeratePhysicalDevices();

    if (devices.empty()) {
        throw VulkanException(vk::Result::eErrorInitializationFailed,
                            "Failed to find GPUs with Vulkan support",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    LOG_INFO("Found " + std::to_string(devices.size()) + " physical device(s)");

    // Rate each device and pick the best one
    std::multimap<int, vk::PhysicalDevice> candidates;

    for (const auto& dev : devices) {
        int score = rateDeviceSuitability(dev, *surface);
        candidates.insert(std::make_pair(score, dev));
    }

    // Check if the best candidate is suitable
    auto bestCandidate = candidates.rbegin();
    if (bestCandidate->first > 0) {
        physicalDevice = vk::raii::PhysicalDevice(instance, bestCandidate->second);
        deviceProperties = physicalDevice.getProperties();
        deviceFeatures = physicalDevice.getFeatures();

        LOG_INFO("Selected physical device: " + std::string(deviceProperties.deviceName.data()));
        LOG_INFO("  - Device type: " + std::to_string(static_cast<uint32_t>(deviceProperties.deviceType)));
        LOG_INFO("  - API version: " +
                 std::to_string(VK_VERSION_MAJOR(deviceProperties.apiVersion)) + "." +
                 std::to_string(VK_VERSION_MINOR(deviceProperties.apiVersion)) + "." +
                 std::to_string(VK_VERSION_PATCH(deviceProperties.apiVersion)));
        LOG_INFO("  - Driver version: " + std::to_string(deviceProperties.driverVersion));
    } else {
        throw VulkanException(vk::Result::eErrorInitializationFailed,
                            "Failed to find a suitable GPU",
                            __FUNCTION__, __FILE__, __LINE__);
    }
}

void VulkanDevice::createLogicalDevice(vk::raii::SurfaceKHR& surface) {
    LOG_DEBUG("Creating logical device");

    // Find queue families
    queueFamilyIndices = findQueueFamilies(*physicalDevice, *surface);

    if (!queueFamilyIndices.isComplete()) {
        throw VulkanException(vk::Result::eErrorInitializationFailed,
                            "Failed to find required queue families",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Create queue create infos
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        queueFamilyIndices.graphicsFamily.value(),
        queueFamilyIndices.presentFamily.value()
    };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        queueCreateInfos.push_back(vk::DeviceQueueCreateInfo{
            {},
            queueFamily,
            1,
            &queuePriority
        });
    }

    // Check device extension support
    if (!checkDeviceExtensionSupport(*physicalDevice)) {
        throw VulkanException(vk::Result::eErrorExtensionNotPresent,
                            "Device does not support required extensions",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Device create info
    vk::DeviceCreateInfo createInfo{
        {},
        queueCreateInfos,
        {},  // No validation layers at device level (deprecated)
        config.requiredExtensions,
        &config.requiredFeatures
    };

    try {
        device = physicalDevice.createDevice(createInfo);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to create logical device: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Get queue handles
    graphicsQueueFamily = queueFamilyIndices.graphicsFamily.value();
    presentQueueFamily = queueFamilyIndices.presentFamily.value();

    graphicsQueue = device.getQueue(graphicsQueueFamily, 0);
    presentQueue = device.getQueue(presentQueueFamily, 0);

    LOG_INFO("Logical device created successfully");
    LOG_INFO("  - Graphics queue family: " + std::to_string(graphicsQueueFamily));
    LOG_INFO("  - Present queue family: " + std::to_string(presentQueueFamily));
}

bool VulkanDevice::isDeviceSuitable(vk::PhysicalDevice device, vk::SurfaceKHR surface) const {
    // Find queue families
    QueueFamilyIndices indices = findQueueFamilies(device, surface);
    if (!indices.isComplete()) {
        return false;
    }

    // Check extension support
    if (!checkDeviceExtensionSupport(device)) {
        return false;
    }

    // Check swap chain support
    auto capabilities = device.getSurfaceCapabilitiesKHR(surface);
    auto formats = device.getSurfaceFormatsKHR(surface);
    auto presentModes = device.getSurfacePresentModesKHR(surface);

    if (formats.empty() || presentModes.empty()) {
        return false;
    }

    return true;
}

QueueFamilyIndices VulkanDevice::findQueueFamilies(vk::PhysicalDevice device, vk::SurfaceKHR surface) const {
    QueueFamilyIndices indices;

    // Get queue family properties
    auto queueFamilies = device.getQueueFamilyProperties();

    // Find suitable queue families
    for (uint32_t i = 0; i < queueFamilies.size(); i++) {
        const auto& queueFamily = queueFamilies[i];

        // Check for graphics support
        if (config.requireGraphicsQueue && queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphicsFamily = i;
        }

        // Check for present support
        if (config.requirePresentQueue) {
            auto presentSupport = device.getSurfaceSupportKHR(i, surface);
            if (presentSupport) {
                indices.presentFamily = i;
            }
        }

        // If we found all required queues, break early
        if (indices.isComplete()) {
            break;
        }
    }

    return indices;
}

bool VulkanDevice::checkDeviceExtensionSupport(vk::PhysicalDevice device) const {
    // Get available extensions
    auto availableExtensions = device.enumerateDeviceExtensionProperties();

    // Create a set of required extensions
    std::set<std::string> requiredExtensions(config.requiredExtensions.begin(),
                                            config.requiredExtensions.end());

    // Check if all required extensions are available
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

int VulkanDevice::rateDeviceSuitability(vk::PhysicalDevice device, vk::SurfaceKHR surface) const {
    int score = 0;

    // Get device properties and features
    auto deviceProperties = device.getProperties();
    auto deviceFeatures = device.getFeatures();

    // Discrete GPUs have a significant performance advantage
    if (deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
        score += 1000;
    }

    // Maximum possible size of textures affects graphics quality
    score += deviceProperties.limits.maxImageDimension2D;

    // Check if device is suitable
    if (!isDeviceSuitable(device, surface)) {
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

void VulkanDevice::logDeviceInfo(vk::PhysicalDevice device) const {
    auto properties = device.getProperties();
    auto features = device.getFeatures();

    LOG_DEBUG("Device: " + std::string(properties.deviceName.data()));
    LOG_DEBUG("  - Type: " + std::to_string(static_cast<uint32_t>(properties.deviceType)));
    LOG_DEBUG("  - API Version: " +
              std::to_string(VK_VERSION_MAJOR(properties.apiVersion)) + "." +
              std::to_string(VK_VERSION_MINOR(properties.apiVersion)) + "." +
              std::to_string(VK_VERSION_PATCH(properties.apiVersion)));
    LOG_DEBUG("  - Driver Version: " + std::to_string(properties.driverVersion));
    LOG_DEBUG("  - Vendor ID: " + std::to_string(properties.vendorID));
    LOG_DEBUG("  - Device ID: " + std::to_string(properties.deviceID));
}

} // namespace RYRayTracing
