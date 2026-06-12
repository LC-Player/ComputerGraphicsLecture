#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
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

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

// Forward declaration
struct SwapChainSupportDetails;

/**
 * @brief Device creation parameters
 */
struct DeviceConfig {
    std::vector<const char*> requiredExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    vk::PhysicalDeviceFeatures requiredFeatures = {};
    bool requireGraphicsQueue = true;
    bool requirePresentQueue = true;
    bool requireRayTracing = false;
};

/**
 * @brief Vulkan device manager using vk::raii
 */
class VulkanDevice {
public:
    VulkanDevice(vk::raii::Instance& instance, vk::raii::SurfaceKHR& surface,
                const DeviceConfig& config = DeviceConfig());
    ~VulkanDevice() = default;

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;
    VulkanDevice(VulkanDevice&& other) noexcept = default;
    VulkanDevice& operator=(VulkanDevice&& other) noexcept = default;

    vk::raii::Device& get() { return device; }
    const vk::raii::Device& get() const { return device; }

    vk::raii::PhysicalDevice& getPhysical() { return physicalDevice; }
    const vk::raii::PhysicalDevice& getPhysical() const { return physicalDevice; }

    vk::Queue getGraphicsQueue() const { return graphicsQueue; }
    vk::Queue getPresentQueue() const { return presentQueue; }

    uint32_t getGraphicsQueueFamily() const { return graphicsQueueFamily; }
    uint32_t getPresentQueueFamily() const { return presentQueueFamily; }

    const QueueFamilyIndices& getQueueFamilyIndices() const { return queueFamilyIndices; }
    const vk::PhysicalDeviceProperties& getProperties() const { return deviceProperties; }

    /// Ray tracing pipeline properties
    const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR& getRTPipelineProps() const {
        return m_rtPipelineProps;
    }
    const vk::PhysicalDeviceAccelerationStructurePropertiesKHR& getASProps() const {
        return m_asProps;
    }

    /// Get buffer device address
    vk::DeviceAddress getBufferDeviceAddress(vk::Buffer buffer) const {
        return device.getBufferAddress({buffer});
    }

    SwapChainSupportDetails getSwapChainSupportDetails(vk::PhysicalDevice device) const;

    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;
    void waitIdle() const;

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

    vk::PhysicalDeviceRayTracingPipelinePropertiesKHR m_rtPipelineProps{};
    vk::PhysicalDeviceAccelerationStructurePropertiesKHR m_asProps{};

    uint32_t graphicsQueueFamily;
    uint32_t presentQueueFamily;

    void pickPhysicalDevice(vk::raii::Instance& instance, vk::raii::SurfaceKHR& surface);
    void createLogicalDevice(vk::raii::SurfaceKHR& surface);
    bool isDeviceSuitable(vk::PhysicalDevice device, vk::SurfaceKHR surface) const;
    QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device, vk::SurfaceKHR surface) const;
    bool checkDeviceExtensionSupport(vk::PhysicalDevice device) const;
    int rateDeviceSuitability(vk::PhysicalDevice device, vk::SurfaceKHR surface) const;
    void logDeviceInfo(vk::PhysicalDevice device) const;
};

} // namespace RYRayTracing
