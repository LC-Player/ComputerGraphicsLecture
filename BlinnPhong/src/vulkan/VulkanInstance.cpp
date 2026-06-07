#include "VulkanInstance.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <sstream>

namespace RYBlinnPhong {

VulkanInstance::VulkanInstance(const InstanceConfig& config)
    : config(config) {

    LOG_INFO("Creating Vulkan instance: " + config.applicationName);

    // Determine if validation should be enabled
    enableValidation = config.enableValidation && Validation::shouldEnableValidation();

    // Setup validation layers if enabled
    if (enableValidation) {
        setupValidationLayers();
    }

    // Get required extensions
    extensions = getRequiredExtensions();

    // Create the instance
    createInstance();

    LOG_INFO("Vulkan instance created successfully");
}

void VulkanInstance::createInstance() {
    LOG_DEBUG("Creating Vulkan instance");

    // Check extension support
    checkExtensionSupport();

    // Check validation layer support if enabled
    if (enableValidation) {
        checkValidationLayerSupport();
    }

    // Application info
    vk::ApplicationInfo appInfo{
        config.applicationName.c_str(),
        config.applicationVersion,
        config.engineName.c_str(),
        config.engineVersion,
        config.apiVersion
    };

    // Instance create info
    vk::InstanceCreateInfo createInfo{
        {},
        &appInfo,
        validationLayers,
        extensions
    };

    // Add debug messenger create info if validation is enabled
    vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo;
    if (enableValidation) {
        debugCreateInfo = Validation::getDebugMessengerCreateInfo();
        createInfo.setPNext(&debugCreateInfo);
    }

    // Create the instance
    try {
        instance = vk::raii::Instance(context, createInfo);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to create Vulkan instance: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Setup debug messenger if validation is enabled
    if (enableValidation) {
        try {
            debugMessenger = instance.createDebugUtilsMessengerEXT(debugCreateInfo);
            LOG_INFO("Debug messenger created successfully");
        } catch (const vk::SystemError& e) {
            LOG_WARNING(std::string("Failed to create debug messenger: ") + e.what());
        }
    }
}

void VulkanInstance::setupValidationLayers() {
    LOG_DEBUG("Setting up validation layers");

    // Use default validation layers
    validationLayers = Validation::defaultValidationLayers;

    LOG_DEBUG("Validation layers configured: " + std::to_string(validationLayers.size()) + " layers");
    for (const auto& layer : validationLayers) {
        LOG_DEBUG("  - " + std::string(layer));
    }
}

std::vector<const char*> VulkanInstance::getRequiredExtensions() {
    std::vector<const char*> requiredExtensions;

    // Get GLFW required extensions
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    if (glfwExtensions == nullptr || glfwExtensionCount == 0) {
        throw VulkanException(vk::Result::eErrorExtensionNotPresent,
                            "GLFW failed to get required instance extensions",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    LOG_DEBUG("GLFW required extensions: " + std::to_string(glfwExtensionCount));
    for (uint32_t i = 0; i < glfwExtensionCount; i++) {
        requiredExtensions.push_back(glfwExtensions[i]);
        LOG_DEBUG("  - " + std::string(glfwExtensions[i]));
    }

    // Add validation layer extensions if enabled
    if (enableValidation) {
        auto validationExtensions = Validation::getRequiredExtensions();
        requiredExtensions.insert(requiredExtensions.end(),
                                 validationExtensions.begin(),
                                 validationExtensions.end());
    }

    // Add user-required extensions
    for (const auto& ext : config.requiredExtensions) {
        requiredExtensions.push_back(ext);
        LOG_DEBUG("  - " + std::string(ext) + " (user-required)");
    }

    // Remove duplicates
    std::sort(requiredExtensions.begin(), requiredExtensions.end());
    auto last = std::unique(requiredExtensions.begin(), requiredExtensions.end());
    requiredExtensions.erase(last, requiredExtensions.end());

    LOG_INFO("Total required extensions: " + std::to_string(requiredExtensions.size()));
    return requiredExtensions;
}

void VulkanInstance::checkExtensionSupport() const {
    LOG_DEBUG("Checking extension support");

    // Get available extensions
    auto availableExtensions = vk::enumerateInstanceExtensionProperties();

    LOG_DEBUG("Available Vulkan extensions: " + std::to_string(availableExtensions.size()));
    for (const auto& extension : availableExtensions) {
        LOG_DEBUG("  - " + std::string(extension.extensionName.data()) +
                 " (version: " + std::to_string(extension.specVersion) + ")");
    }

    // Check if all required extensions are available
    for (const auto& requiredExt : extensions) {
        bool found = false;
        for (const auto& availableExt : availableExtensions) {
            if (strcmp(requiredExt, availableExt.extensionName) == 0) {
                found = true;
                break;
            }
        }

        if (!found) {
            std::string errorMsg = "Required extension not found: " + std::string(requiredExt);
            LOG_ERROR(errorMsg);
            throw VulkanException(vk::Result::eErrorExtensionNotPresent,
                                errorMsg,
                                __FUNCTION__, __FILE__, __LINE__);
        }
    }

    LOG_INFO("All required extensions are available");
}

void VulkanInstance::checkValidationLayerSupport() const {
    LOG_DEBUG("Checking validation layer support");

    // Use Validation class to check support
    bool supported = Validation::checkSupport();
    if (!supported) {
        throw VulkanException(vk::Result::eErrorLayerNotPresent,
                            "Required validation layers not available",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    LOG_INFO("All validation layers are available");
}

} // namespace RYBlinnPhong
