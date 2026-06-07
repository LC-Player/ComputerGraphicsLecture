#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <string>
#include "core/Logger.hpp"

namespace RYBlinnPhong {

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
     * @return vk::DebugUtilsMessengerCreateInfoEXT Debug messenger creation info
     */
    static vk::DebugUtilsMessengerCreateInfoEXT getDebugMessengerCreateInfo();

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
    static std::string severityToString(vk::DebugUtilsMessageSeverityFlagBitsEXT severity);

    /**
     * @brief Convert message type to string
     *
     * @param type Message type
     * @return std::string String representation
     */
    static std::string typeToString(vk::DebugUtilsMessageTypeFlagsEXT type);
};

} // namespace RYBlinnPhong