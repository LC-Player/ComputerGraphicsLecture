#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include "core/Exception.hpp"
#include "core/Logger.hpp"
#include "vulkan/Validation.hpp"

namespace RYRayTracing {

/**
 * @brief Vulkan instance creation parameters
 */
struct InstanceConfig {
    std::string applicationName = "Vulkan Application";
    uint32_t applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    std::string engineName = "No Engine";
    uint32_t engineVersion = VK_MAKE_VERSION(1, 0, 0);
    uint32_t apiVersion = VK_API_VERSION_1_3;
    bool enableValidation = true;
    std::vector<const char*> requiredExtensions;
};

/**
 * @brief Vulkan instance manager
 *
 * Manages the creation and destruction of the Vulkan instance,
 * which is the foundation of all Vulkan applications.
 */
class VulkanInstance {
public:
    /**
     * @brief Construct a new VulkanInstance object
     *
     * @param config Instance configuration
     */
    explicit VulkanInstance(const InstanceConfig& config = InstanceConfig());

    /**
     * @brief Destroy the VulkanInstance object
     */
    ~VulkanInstance();

    // Delete copy constructor and assignment operator
    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;

    /**
     * @brief Move constructor
     */
    VulkanInstance(VulkanInstance&& other) noexcept;

    /**
     * @brief Move assignment operator
     */
    VulkanInstance& operator=(VulkanInstance&& other) noexcept;

    /**
     * @brief Get the Vulkan instance handle
     *
     * @return VkInstance Vulkan instance
     */
    VkInstance get() const { return instance; }

    /**
     * @brief Check if validation layers are enabled
     *
     * @return true if validation layers are enabled, false otherwise
     */
    bool hasValidationLayers() const { return enableValidation; }

    /**
     * @brief Get the debug messenger handle
     *
     * @return VkDebugUtilsMessengerEXT Debug messenger
     */
    VkDebugUtilsMessengerEXT getDebugMessenger() const { return debugMessenger; }

    /**
     * @brief Get the list of enabled validation layers
     *
     * @return std::vector<const char*> List of validation layer names
     */
    const std::vector<const char*>& getValidationLayers() const { return validationLayers; }

    /**
     * @brief Get the list of enabled instance extensions
     *
     * @return std::vector<const char*> List of extension names
     */
    const std::vector<const char*>& getExtensions() const { return extensions; }

    /**
     * @brief Get the instance configuration
     *
     * @return const InstanceConfig& Instance configuration
     */
    const InstanceConfig& getConfig() const { return config; }

private:
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    InstanceConfig config;
    std::vector<const char*> validationLayers;
    std::vector<const char*> extensions;
    bool enableValidation;
    bool initialized;

    /**
     * @brief Create the Vulkan instance
     *
     * @throws VulkanException if instance creation fails
     */
    void createInstance();

    /**
     * @brief Setup validation layers
     */
    void setupValidationLayers();

    /**
     * @brief Get the required instance extensions
     *
     * @return std::vector<const char*> List of required extensions
     */
    std::vector<const char*> getRequiredExtensions();

    /**
     * @brief Check if all required extensions are available
     *
     * @throws VulkanException if an extension is not available
     */
    void checkExtensionSupport() const;

    /**
     * @brief Check if all required validation layers are available
     *
     * @throws VulkanException if a validation layer is not available
     */
    void checkValidationLayerSupport() const;
};

} // namespace RYRayTracing