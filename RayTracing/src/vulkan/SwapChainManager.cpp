#include "SwapChainManager.hpp"
#include "VulkanDevice.hpp"
#include <algorithm>
#include <stdexcept>

namespace RYRayTracing {

SwapChainManager::SwapChainManager(VulkanDevice* device, VkSurfaceKHR surface,
                                   uint32_t width, uint32_t height,
                                   const SwapChainConfig& config)
    : device(device)
    , surface(surface)
    , swapChain(VK_NULL_HANDLE)
    , imageFormat(VK_FORMAT_UNDEFINED)
    , extent{width, height}
    , config(config)
    , initialized(false) {

    LOG_INFO("Creating swap chain: " + std::to_string(width) + "x" + std::to_string(height));

    try {
        createSwapChain();
        createImageViews();

        initialized = true;
        LOG_INFO("Swap chain created successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create swap chain: " + std::string(e.what()));
        cleanupResources();
        throw;
    }
}

SwapChainManager::~SwapChainManager() {
    cleanupResources();
}

SwapChainManager::SwapChainManager(SwapChainManager&& other) noexcept
    : device(other.device)
    , surface(other.surface)
    , swapChain(other.swapChain)
    , images(std::move(other.images))
    , imageViews(std::move(other.imageViews))
    , imageFormat(other.imageFormat)
    , extent(other.extent)
    , config(other.config)
    , initialized(other.initialized) {
    other.device = nullptr;
    other.surface = VK_NULL_HANDLE;
    other.swapChain = VK_NULL_HANDLE;
    other.imageFormat = VK_FORMAT_UNDEFINED;
    other.extent = {0, 0};
    other.initialized = false;
}

SwapChainManager& SwapChainManager::operator=(SwapChainManager&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        cleanupResources();

        // Move resources from other
        device = other.device;
        surface = other.surface;
        swapChain = other.swapChain;
        images = std::move(other.images);
        imageViews = std::move(other.imageViews);
        imageFormat = other.imageFormat;
        extent = other.extent;
        config = other.config;
        initialized = other.initialized;

        // Reset other
        other.device = nullptr;
        other.surface = VK_NULL_HANDLE;
        other.swapChain = VK_NULL_HANDLE;
        other.imageFormat = VK_FORMAT_UNDEFINED;
        other.extent = {0, 0};
        other.initialized = false;
    }
    return *this;
}

void SwapChainManager::cleanupResources() {
    VkDevice vkDevice = device ? device->get() : VK_NULL_HANDLE;

    if (vkDevice == VK_NULL_HANDLE) {
        return;
    }

    // Destroy image views
    for (auto imageView : imageViews) {
        vkDestroyImageView(vkDevice, imageView, nullptr);
    }
    imageViews.clear();

    // Destroy swap chain
    if (swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(vkDevice, swapChain, nullptr);
        swapChain = VK_NULL_HANDLE;
    }

    // Clear images
    images.clear();

    initialized = false;
    LOG_DEBUG("Swap chain resources cleaned up");
}

void SwapChainManager::createSwapChain() {
    SwapChainSupportDetails swapChainSupport = device->getSwapChainSupportDetails(device->getPhysical());

    if (!swapChainSupport.isAdequate()) {
        throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                            "Swap chain is not adequately supported",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = choosePresentMode(swapChainSupport.presentModes);
    VkExtent2D actualExtent = chooseExtent(swapChainSupport.capabilities, extent.width, extent.height);

    // Determine the number of images in the swap chain
    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = actualExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = config.imageUsage;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = config.compositeAlpha;
    createInfo.presentMode = presentMode;
    createInfo.clipped = config.clipped ? VK_TRUE : VK_FALSE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    // Handle queue family indices
    QueueFamilyIndices indices = device->getQueueFamilyIndices();
    uint32_t queueFamilyIndices[] = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }

    VkResult result = vkCreateSwapchainKHR(device->get(), &createInfo, nullptr, &swapChain);
    if (result != VK_SUCCESS) {
        throw VulkanException(result, "vkCreateSwapchainKHR", __FUNCTION__, __FILE__, __LINE__);
    }

    // Get swap chain images
    vkGetSwapchainImagesKHR(device->get(), swapChain, &imageCount, nullptr);
    images.resize(imageCount);
    vkGetSwapchainImagesKHR(device->get(), swapChain, &imageCount, images.data());

    // Store format and extent
    imageFormat = surfaceFormat.format;
    extent = actualExtent;

    LOG_DEBUG("Swap chain created: " + std::to_string(extent.width) + "x" +
              std::to_string(extent.height) + ", format: " +
              std::to_string(imageFormat) + ", images: " +
              std::to_string(images.size()));
}

void SwapChainManager::createImageViews() {
    imageViews.resize(images.size());

    for (size_t i = 0; i < images.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = images[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = imageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        VkResult result = vkCreateImageView(device->get(), &createInfo, nullptr, &imageViews[i]);
        if (result != VK_SUCCESS) {
            // Clean up any image views created so far
            for (size_t j = 0; j < i; j++) {
                vkDestroyImageView(device->get(), imageViews[j], nullptr);
            }
            imageViews.clear();
            throw VulkanException(result, "vkCreateImageView", __FUNCTION__, __FILE__, __LINE__);
        }
    }

    LOG_DEBUG("Created " + std::to_string(imageViews.size()) + " image views");
}

VkSurfaceFormatKHR SwapChainManager::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const {
    // Prefer SRGB format if available
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == config.surfaceFormat.format &&
            availableFormat.colorSpace == config.surfaceFormat.colorSpace) {
            return availableFormat;
        }
    }

    // Fallback to first available format
    if (!availableFormats.empty()) {
        return availableFormats[0];
    }

    throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                         "No suitable surface format found",
                         __FUNCTION__, __FILE__, __LINE__);
}

VkPresentModeKHR SwapChainManager::choosePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const {
    // Prefer mailbox (triple buffering) if available
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == config.presentMode) {
            return availablePresentMode;
        }
    }

    // Fallback to FIFO (guaranteed to be available)
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D SwapChainManager::chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                                         uint32_t width, uint32_t height) const {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    VkExtent2D actualExtent = {width, height};
    actualExtent.width = std::clamp(actualExtent.width,
                                   capabilities.minImageExtent.width,
                                   capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height,
                                    capabilities.minImageExtent.height,
                                    capabilities.maxImageExtent.height);

    return actualExtent;
}

uint32_t SwapChainManager::acquireNextImage(VkSemaphore semaphore, VkFence fence) const {
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device->get(), swapChain, UINT64_MAX,
                                           semaphore, fence, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // Swap chain needs to be recreated
        return UINT32_MAX;
    } else if (result != VK_SUCCESS) {
        throw VulkanException(result, "vkAcquireNextImageKHR", __FUNCTION__, __FILE__, __LINE__);
    }

    return imageIndex;
}

void SwapChainManager::presentImage(uint32_t imageIndex, VkSemaphore waitSemaphore) const {
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = waitSemaphore != VK_NULL_HANDLE ? 1 : 0;
    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = nullptr;

    VkResult result = vkQueuePresentKHR(device->getPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // Swap chain needs to be recreated
        throw VulkanException(result, "Swap chain out of date or suboptimal", __FUNCTION__, __FILE__, __LINE__);
    } else if (result != VK_SUCCESS) {
        throw VulkanException(result, "vkQueuePresentKHR", __FUNCTION__, __FILE__, __LINE__);
    }
}

void SwapChainManager::recreate(uint32_t width, uint32_t height) {
    LOG_INFO("Recreating swap chain: " + std::to_string(width) + "x" + std::to_string(height));

    // Wait for device to be idle
    device->waitIdle();

    // Clean up old resources
    cleanupResources();

    // Update extent
    extent = {width, height};

    // Recreate swap chain
    createSwapChain();
    createImageViews();

    LOG_INFO("Swap chain recreated successfully");
}

SwapChainSupportDetails SwapChainManager::querySupport() const {
    return device->getSwapChainSupportDetails(device->getPhysical());
}

} // namespace RYRayTracing