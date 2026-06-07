#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <string>
#include <optional>
#include "core/Exception.hpp"
#include "core/Logger.hpp"

namespace RYBlinnPhong {

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
    vk::PhysicalDeviceFeatures requiredFeatures = {};
    bool requireGraphicsQueue = true;
    bool requirePresentQueue = true;
    bool requireTransferQueue = false;
    bool requireComputeQueue = false;
};

/**
 * @brief Vulkan device manager using vk::raii
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
    VulkanDevice(vk::raii::Instance& instance, vk::raii::SurfaceKHR& surface,
                const DeviceConfig& config = DeviceConfig());

    /**
     * @brief Destroy the VulkanDevice object
     */
    ~VulkanDevice() = default;

    // Delete copy constructor and assignment operator
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    /**
     * @brief Move constructor
     */
    VulkanDevice(VulkanDevice&& other) noexcept = default;

    /**
     * @brief Move assignment operator
     */
    VulkanDevice& operator=(VulkanDevice&& other) noexcept = default;

    /**
     * @brief Get the logical device handle
     *
     * @return vk::raii::Device& Logical device
     */
    vk::raii::Device& get() { return device; }
    const vk::raii::Device& get() const { return device; }

    /**
     * @brief Get the physical device handle
     *
     * @return vk::raii::PhysicalDevice& Physical device
     */
    vk::raii::PhysicalDevice& getPhysical() { return physicalDevice; }
    const vk::raii::PhysicalDevice& getPhysical() const { return physicalDevice; }

    /**
     * @brief Get the graphics queue
     *
     * @return vk::Queue Graphics queue
     */
    vk::Queue getGraphicsQueue() const { return graphicsQueue; }

    /**
     * @brief Get the present queue
     *
     * @return vk::Queue Present queue
     */
    vk::Queue getPresentQueue() const { return presentQueue; }

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
     * @return const vk::PhysicalDeviceProperties& Device properties
     */
    const vk::PhysicalDeviceProperties& getProperties() const { return deviceProperties; }

    /**
     * @brief Get the device features
     *
     * @return const vk::PhysicalDeviceFeatures& Device features
     */
    const vk::PhysicalDeviceFeatures& getFeatures() const { return deviceFeatures; }

    /**
     * @brief Get the swap chain support details for a physical device
     *
     * @param device Physical device to query
     * @return SwapChainSupportDetails Swap chain support details
     */
    SwapChainSupportDetails getSwapChainSupportDetails(vk::PhysicalDevice device) const;

    /**
     * @brief Find a suitable memory type
     *
     * @param typeFilter Memory type filter
     * @param properties Memory properties
     * @return uint32_t Memory type index
     * @throws VulkanException if no suitable memory type is found
     */
    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;

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
    vk::raii::PhysicalDevice physicalDevice = nullptr;
    vk::raii::Device device = nullptr;
    vk::Queue graphicsQueue;
    vk::Queue presentQueue;
    QueueFamilyIndices queueFamilyIndices;
    vk::PhysicalDeviceProperties deviceProperties;
    vk::PhysicalDeviceFeatures deviceFeatures;
    DeviceConfig config;
    std::vector<const char*> enabledExtensions;

    uint32_t graphicsQueueFamily;
    uint32_t presentQueueFamily;

    /**
     * @brief Pick a suitable physical device
     *
     * @param instance Vulkan instance
     * @param surface Window surface
     * @throws VulkanException if no suitable device is found
     */
    void pickPhysicalDevice(vk::raii::Instance& instance, vk::raii::SurfaceKHR& surface);

    /**
     * @brief Create the logical device
     *
     * @param surface Window surface
     * @throws VulkanException if device creation fails
     */
    void createLogicalDevice(vk::raii::SurfaceKHR& surface);

    /**
     * @brief Check if a physical device is suitable
     *
     * @param device Physical device to check
     * @param surface Window surface
     * @return true if device is suitable, false otherwise
     */
    bool isDeviceSuitable(vk::PhysicalDevice device, vk::SurfaceKHR surface) const;

    /**
     * @brief Find queue families for a device
     *
     * @param device Physical device
     * @param surface Window surface
     * @return QueueFamilyIndices Queue family indices
     */
    QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device, vk::SurfaceKHR surface) const;

    /**
     * @brief Check if a device supports required extensions
     *
     * @param device Physical device
     * @return true if device supports all required extensions, false otherwise
     */
    bool checkDeviceExtensionSupport(vk::PhysicalDevice device) const;

    /**
     * @brief Rate a physical device based on suitability
     *
     * @param device Physical device to rate
     * @param surface Window surface
     * @return int Device score (higher is better)
     */
    int rateDeviceSuitability(vk::PhysicalDevice device, vk::SurfaceKHR surface) const;

    /**
     * @brief Log device information
     *
     * @param device Physical device
     */
    void logDeviceInfo(vk::PhysicalDevice device) const;
};

} // namespace RYBlinnPhong
