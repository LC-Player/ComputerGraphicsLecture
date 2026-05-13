#include "Validation.hpp"
#include "core/Exception.hpp"
#include <algorithm>
#include <sstream>

namespace RYRayTracing {

const std::vector<const char*> Validation::defaultValidationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

bool Validation::checkSupport() {
    auto availableLayers = vk::enumerateInstanceLayerProperties();

    LOG_DEBUG("Available Vulkan validation layers:");
    for (const auto& layer : availableLayers) {
        LOG_DEBUG("  - " + std::string(layer.layerName.data()));
    }

    // Check if all required layers are available
    for (const char* layerName : defaultValidationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            LOG_WARNING("Validation layer not found: " + std::string(layerName));
            return false;
        }
    }

    LOG_INFO("All validation layers are available");
    return true;
}

std::vector<const char*> Validation::getRequiredExtensions() {
    std::vector<const char*> extensions;

    // Always add debug utils extension if validation is enabled
    if (shouldEnableValidation()) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        LOG_DEBUG("Added debug utils extension for validation layers");
    }

    return extensions;
}

vk::DebugUtilsMessengerCreateInfoEXT Validation::getDebugMessengerCreateInfo() {
    return vk::DebugUtilsMessengerCreateInfoEXT{
        {},
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(debugCallback),
        nullptr
    };
}

bool Validation::shouldEnableValidation() {
#ifdef ENABLE_VALIDATION_LAYERS
    return true;
#else
    return false;
#endif
}

VKAPI_ATTR VkBool32 VKAPI_CALL Validation::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    // Format the message
    std::ostringstream oss;
    oss << "[Vulkan Validation] "
        << "[" << severityToString(static_cast<vk::DebugUtilsMessageSeverityFlagBitsEXT>(messageSeverity)) << "] "
        << "[" << typeToString(static_cast<vk::DebugUtilsMessageTypeFlagsEXT>(messageType)) << "] "
        << pCallbackData->pMessage;

    std::string message = oss.str();

    // Route to appropriate log level
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        LOG_ERROR(message);
    } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOG_WARNING(message);
    } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        LOG_INFO(message);
    } else {
        LOG_DEBUG(message);
    }

    // Additional debug information
    if (pCallbackData->objectCount > 0) {
        LOG_DEBUG("  Objects involved:");
        for (uint32_t i = 0; i < pCallbackData->objectCount; ++i) {
            const auto& object = pCallbackData->pObjects[i];
            std::ostringstream objOss;
            objOss << "    - Object " << i << ": "
                   << "Type: " << object.objectType << ", "
                   << "Handle: " << reinterpret_cast<void*>(object.objectHandle);
            LOG_DEBUG(objOss.str());
        }
    }

    return VK_FALSE; // Don't abort the call
}

std::string Validation::severityToString(vk::DebugUtilsMessageSeverityFlagBitsEXT severity) {
    switch (severity) {
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose: return "VERBOSE";
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:    return "INFO";
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning: return "WARNING";
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:   return "ERROR";
        default:                                                 return "UNKNOWN";
    }
}

std::string Validation::typeToString(vk::DebugUtilsMessageTypeFlagsEXT type) {
    std::string result;

    if (type & vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral) {
        if (!result.empty()) result += "|";
        result += "GENERAL";
    }
    if (type & vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation) {
        if (!result.empty()) result += "|";
        result += "VALIDATION";
    }
    if (type & vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance) {
        if (!result.empty()) result += "|";
        result += "PERFORMANCE";
    }

    return result.empty() ? "UNKNOWN" : result;
}

} // namespace RYRayTracing
