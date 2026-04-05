#include "VulkanInstance.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <sstream>

namespace RYRayTracing {

VulkanInstance::VulkanInstance(const InstanceConfig& config)
    : instance(VK_NULL_HANDLE)
    , debugMessenger(VK_NULL_HANDLE)
    , config(config)
    , initialized(false) {

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

    // Setup debug messenger if validation is enabled
    Validation::setupDebugMessenger(instance, debugMessenger, enableValidation);

    initialized = true;
    LOG_INFO("Vulkan instance created successfully");
}

VulkanInstance::~VulkanInstance() {
    LOG_DEBUG("Destroying Vulkan instance");

    // Destroy debug messenger
    Validation::destroyDebugMessenger(instance, debugMessenger, enableValidation);

    // Destroy instance
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }

    LOG_DEBUG("Vulkan instance destroyed");
}

VulkanInstance::VulkanInstance(VulkanInstance&& other) noexcept
    : instance(other.instance)
    , debugMessenger(other.debugMessenger)
    , config(std::move(other.config))
    , validationLayers(std::move(other.validationLayers))
    , extensions(std::move(other.extensions))
    , initialized(other.initialized)
    , enableValidation(other.enableValidation) {
    other.instance = VK_NULL_HANDLE;
    other.debugMessenger = VK_NULL_HANDLE;
    other.initialized = false;
}

VulkanInstance& VulkanInstance::operator=(VulkanInstance&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        Validation::destroyDebugMessenger(instance, debugMessenger, enableValidation);
        if (instance != VK_NULL_HANDLE) {
            vkDestroyInstance(instance, nullptr);
        }

        // Move resources from other
        instance = other.instance;
        debugMessenger = other.debugMessenger;
        config = std::move(other.config);
        validationLayers = std::move(other.validationLayers);
        extensions = std::move(other.extensions);
        initialized = other.initialized;
        enableValidation = other.enableValidation;

        // Reset other
        other.instance = VK_NULL_HANDLE;
        other.debugMessenger = VK_NULL_HANDLE;
        other.initialized = false;
    }
    return *this;
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
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = config.applicationName.c_str();
    appInfo.applicationVersion = config.applicationVersion;
    appInfo.pEngineName = config.engineName.c_str();
    appInfo.engineVersion = config.engineVersion;
    appInfo.apiVersion = config.apiVersion;

    // Instance create info
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // Extensions
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    // Validation layers
    if (enableValidation) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        // Add debug messenger create info
        auto debugCreateInfo = Validation::getDebugMessengerCreateInfo();
        createInfo.pNext = &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    // Create the instance
    VK_CHECK_RESULT(vkCreateInstance(&createInfo, nullptr, &instance));
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
        throw VulkanException(VK_ERROR_EXTENSION_NOT_PRESENT,
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
    uint32_t extensionCount = 0;
    VK_CHECK_RESULT(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr));

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    VK_CHECK_RESULT(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data()));

    LOG_DEBUG("Available Vulkan extensions: " + std::to_string(extensionCount));
    for (const auto& extension : availableExtensions) {
        LOG_DEBUG("  - " + std::string(extension.extensionName) +
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
            throw VulkanException(VK_ERROR_EXTENSION_NOT_PRESENT,
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
        throw VulkanException(VK_ERROR_LAYER_NOT_PRESENT,
                            "Required validation layers not available",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    LOG_INFO("All validation layers are available");
}

} // namespace RYRayTracing