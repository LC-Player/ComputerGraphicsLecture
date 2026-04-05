#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <optional>
#include "core/Exception.hpp"
#include "core/Logger.hpp"

namespace RYRayTracing {

/**
 * @brief Queue family indices
 */
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    /**
     * @brief Check if all required queue families are found
     *
     * @return true if all queue families are found, false otherwise
     */
    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

// Forward declaration - defined in SwapChainManager.hpp
struct SwapChainSupportDetails;

/**
 * @brief Device creation parameters
 */
struct DeviceConfig {
    std::vector<const char*> requiredExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkPhysicalDeviceFeatures requiredFeatures = {};
    bool requireGraphicsQueue = true;
    bool requirePresentQueue = true;
    bool requireTransferQueue = false;
    bool requireComputeQueue = false;
};

/**
 * @brief Vulkan device manager
 *
 * Manages physical device selection and logical device creation.
 */
class VulkanDevice {
public:
    /**
     * @brief Construct a new VulkanDevice object
     *
     * @param instance Vulkan instance
     * @param surface Window surface
     * @param config Device configuration
     */
    VulkanDevice(VkInstance instance, VkSurfaceKHR surface, const DeviceConfig& config = DeviceConfig());

    /**
     * @brief Destroy the VulkanDevice object
     */
    ~VulkanDevice();

    // Delete copy constructor and assignment operator
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    /**
     * @brief Move constructor
     */
    VulkanDevice(VulkanDevice&& other) noexcept;

    /**
     * @brief Move assignment operator
     */
    VulkanDevice& operator=(VulkanDevice&& other) noexcept;

    /**
     * @brief Get the logical device handle
     *
     * @return VkDevice Logical device
     */
    VkDevice get() const { return device; }

    /**
     * @brief Get the physical device handle
     *
     * @return VkPhysicalDevice Physical device
     */
    VkPhysicalDevice getPhysical() const { return physicalDevice; }

    /**
     * @brief Get the graphics queue
     *
     * @return VkQueue Graphics queue
     */
    VkQueue getGraphicsQueue() const { return graphicsQueue; }

    /**
     * @brief Get the present queue
     *
     * @return VkQueue Present queue
     */
    VkQueue getPresentQueue() const { return presentQueue; }

    /**
     * @brief Get the graphics queue family index
     *
     * @return uint32_t Graphics queue family index
     */
    uint32_t getGraphicsQueueFamily() const { return graphicsQueueFamily; }

    /**
     * @brief Get the present queue family index
     *
     * @return uint32_t Present queue family index
     */
    uint32_t getPresentQueueFamily() const { return presentQueueFamily; }

    /**
     * @brief Get the queue family indices
     *
     * @return const QueueFamilyIndices& Queue family indices
     */
    const QueueFamilyIndices& getQueueFamilyIndices() const { return queueFamilyIndices; }

    /**
     * @brief Get the device properties
     *
     * @return const VkPhysicalDeviceProperties& Device properties
     */
    const VkPhysicalDeviceProperties& getProperties() const { return deviceProperties; }

    /**
     * @brief Get the device features
     *
     * @return const VkPhysicalDeviceFeatures& Device features
     */
    const VkPhysicalDeviceFeatures& getFeatures() const { return deviceFeatures; }

    /**
     * @brief Get the swap chain support details for a physical device
     *
     * @param device Physical device to query
     * @return SwapChainSupportDetails Swap chain support details
     */
    SwapChainSupportDetails getSwapChainSupportDetails(VkPhysicalDevice device) const;

    /**
     * @brief Find a suitable memory type
     *
     * @param typeFilter Memory type filter
     * @param properties Memory properties
     * @return uint32_t Memory type index
     * @throws VulkanException if no suitable memory type is found
     */
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

    /**
     * @brief Wait for the device to become idle
     */
    void waitIdle() const;

    /**
     * @brief Get the device configuration
     *
     * @return const DeviceConfig& Device configuration
     */
    const DeviceConfig& getConfig() const { return config; }

private:
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    QueueFamilyIndices queueFamilyIndices;
    VkPhysicalDeviceProperties deviceProperties;
    VkPhysicalDeviceFeatures deviceFeatures;
    DeviceConfig config;
    std::vector<const char*> enabledExtensions;
    bool initialized;

    uint32_t graphicsQueueFamily;
    uint32_t presentQueueFamily;

    /**
     * @brief Pick a suitable physical device
     *
     * @throws VulkanException if no suitable device is found
     */
    void pickPhysicalDevice();

    /**
     * @brief Create the logical device
     *
     * @throws VulkanException if device creation fails
     */
    void createLogicalDevice();

    /**
     * @brief Check if a physical device is suitable
     *
     * @param device Physical device to check
     * @return true if device is suitable, false otherwise
     */
    bool isDeviceSuitable(VkPhysicalDevice device) const;

    /**
     * @brief Find queue families for a device
     *
     * @param device Physical device
     * @return QueueFamilyIndices Queue family indices
     */
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;

    /**
     * @brief Check if a device supports required extensions
     *
     * @param device Physical device
     * @return true if device supports all required extensions, false otherwise
     */
    bool checkDeviceExtensionSupport(VkPhysicalDevice device) const;

    /**
     * @brief Rate a physical device based on suitability
     *
     * @param device Physical device to rate
     * @return int Device score (higher is better)
     */
    int rateDeviceSuitability(VkPhysicalDevice device) const;

    /**
     * @brief Log device information
     *
     * @param device Physical device
     */
    void logDeviceInfo(VkPhysicalDevice device) const;
};

} // namespace RYRayTracing