#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include "core/Logger.hpp"

namespace RYRayTracing {

/**
 * @brief Vulkan validation layer support
 *
 * Provides functionality for enabling and managing Vulkan validation layers
 * for debugging and error checking.
 */
class Validation {
public:
    /**
     * @brief Default validation layers to enable
     */
    static const std::vector<const char*> defaultValidationLayers;

    /**
     * @brief Check if validation layers are supported
     *
     * @return true if validation layers are supported, false otherwise
     */
    static bool checkSupport();

    /**
     * @brief Get the required instance extensions for validation layers
     *
     * @return std::vector<const char*> List of required extensions
     */
    static std::vector<const char*> getRequiredExtensions();

    /**
     * @brief Get the debug messenger creation info
     *
     * @return VkDebugUtilsMessengerCreateInfoEXT Debug messenger creation info
     */
    static VkDebugUtilsMessengerCreateInfoEXT getDebugMessengerCreateInfo();

    /**
     * @brief Create a debug utils messenger
     *
     * @param instance Vulkan instance
     * @param pCreateInfo Creation info
     * @param pAllocator Allocation callbacks (optional)
     * @param pDebugMessenger Output debug messenger
     * @return VkResult Creation result
     */
    static VkResult createDebugUtilsMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDebugUtilsMessengerEXT* pDebugMessenger);

    /**
     * @brief Destroy a debug utils messenger
     *
     * @param instance Vulkan instance
     * @param debugMessenger Debug messenger to destroy
     * @param pAllocator Allocation callbacks (optional)
     */
    static void destroyDebugUtilsMessengerEXT(
        VkInstance instance,
        VkDebugUtilsMessengerEXT debugMessenger,
        const VkAllocationCallbacks* pAllocator);

    /**
     * @brief Setup debug messenger for an instance
     *
     * @param instance Vulkan instance
     * @param debugMessenger Output debug messenger
     * @param enableValidation Whether to enable validation layers
     */
    static void setupDebugMessenger(
        VkInstance instance,
        VkDebugUtilsMessengerEXT& debugMessenger,
        bool enableValidation);

    /**
     * @brief Destroy debug messenger
     *
     * @param instance Vulkan instance
     * @param debugMessenger Debug messenger to destroy
     * @param enableValidation Whether validation was enabled
     */
    static void destroyDebugMessenger(
        VkInstance instance,
        VkDebugUtilsMessengerEXT debugMessenger,
        bool enableValidation);

    /**
     * @brief Check if validation should be enabled based on build configuration
     *
     * @return true if validation should be enabled, false otherwise
     */
    static bool shouldEnableValidation();

private:
    /**
     * @brief Debug callback function for validation layers
     *
     * This function is called by Vulkan when validation layer messages are generated.
     * It routes the messages to the logging system.
     */
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

    /**
     * @brief Convert message severity to string
     *
     * @param severity Message severity
     * @return std::string String representation
     */
    static std::string severityToString(VkDebugUtilsMessageSeverityFlagBitsEXT severity);

    /**
     * @brief Convert message type to string
     *
     * @param type Message type
     * @return std::string String representation
     */
    static std::string typeToString(VkDebugUtilsMessageTypeFlagsEXT type);
};

} // namespace RYRayTracing