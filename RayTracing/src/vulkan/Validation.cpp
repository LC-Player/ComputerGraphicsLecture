#include "Validation.hpp"
#include "core/Exception.hpp"
#include <algorithm>
#include <sstream>

namespace RYRayTracing {

const std::vector<const char*> Validation::defaultValidationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

bool Validation::checkSupport() {
    uint32_t layerCount;
    VK_CHECK_RESULT(vkEnumerateInstanceLayerProperties(&layerCount, nullptr));

    std::vector<VkLayerProperties> availableLayers(layerCount);
    VK_CHECK_RESULT(vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data()));

    LOG_DEBUG("Available Vulkan validation layers:");
    for (const auto& layer : availableLayers) {
        LOG_DEBUG("  - " + std::string(layer.layerName));
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

VkDebugUtilsMessengerCreateInfoEXT Validation::getDebugMessengerCreateInfo() {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
    createInfo.pUserData = nullptr;

    return createInfo;
}

VkResult Validation::createDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger) {

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkCreateDebugUtilsMessengerEXT");

    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void Validation::destroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* pAllocator) {

    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkDestroyDebugUtilsMessengerEXT");

    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

void Validation::setupDebugMessenger(
    VkInstance instance,
    VkDebugUtilsMessengerEXT& debugMessenger,
    bool enableValidation) {

    if (!enableValidation) {
        debugMessenger = VK_NULL_HANDLE;
        return;
    }

    LOG_INFO("Setting up debug messenger for validation layers");

    auto createInfo = getDebugMessengerCreateInfo();
    VkResult result = createDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger);

    if (result != VK_SUCCESS) {
        LOG_WARNING("Failed to create debug messenger: " + VulkanException::getErrorString(result));
        debugMessenger = VK_NULL_HANDLE;
    } else {
        LOG_INFO("Debug messenger created successfully");
    }
}

void Validation::destroyDebugMessenger(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    bool enableValidation) {

    if (enableValidation && debugMessenger != VK_NULL_HANDLE) {
        LOG_DEBUG("Destroying debug messenger");
        destroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    }
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
        << "[" << severityToString(messageSeverity) << "] "
        << "[" << typeToString(messageType) << "] "
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

std::string Validation::severityToString(VkDebugUtilsMessageSeverityFlagBitsEXT severity) {
    switch (severity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: return "VERBOSE";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:    return "INFO";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: return "WARNING";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:   return "ERROR";
        default:                                              return "UNKNOWN";
    }
}

std::string Validation::typeToString(VkDebugUtilsMessageTypeFlagsEXT type) {
    std::string result;

    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
        if (!result.empty()) result += "|";
        result += "GENERAL";
    }
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        if (!result.empty()) result += "|";
        result += "VALIDATION";
    }
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        if (!result.empty()) result += "|";
        result += "PERFORMANCE";
    }

    return result.empty() ? "UNKNOWN" : result;
}

} // namespace RYRayTracing