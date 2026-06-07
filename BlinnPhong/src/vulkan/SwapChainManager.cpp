#include "SwapChainManager.hpp"
#include "VulkanDevice.hpp"
#include <algorithm>
#include <stdexcept>

namespace RYBlinnPhong {

SwapChainManager::SwapChainManager(VulkanDevice& device, vk::raii::SurfaceKHR& surface,
                                   uint32_t width, uint32_t height,
                                   const SwapChainConfig& config)
    : device(device)
    , surface(surface)
    , imageFormat(vk::Format::eUndefined)
    , extent{width, height}
    , config(config) {

    LOG_INFO("Creating swap chain: " + std::to_string(width) + "x" + std::to_string(height));

    try {
        createSwapChain();
        createImageViews();

        LOG_INFO("Swap chain created successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create swap chain: " + std::string(e.what()));
        throw;
    }
}

void SwapChainManager::createSwapChain() {
    auto physicalDevice = device.getPhysical();

    // Get surface capabilities
    auto capabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
    auto formats = physicalDevice.getSurfaceFormatsKHR(*surface);
    auto presentModes = physicalDevice.getSurfacePresentModesKHR(*surface);

    SwapChainSupportDetails swapChainSupport;
    swapChainSupport.capabilities = capabilities;
    swapChainSupport.formats = formats;
    swapChainSupport.presentModes = presentModes;

    if (!swapChainSupport.isAdequate()) {
        throw VulkanException(vk::Result::eErrorInitializationFailed,
                            "Swap chain is not adequately supported",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    vk::SurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(swapChainSupport.formats);
    vk::PresentModeKHR presentMode = choosePresentMode(swapChainSupport.presentModes);
    vk::Extent2D actualExtent = chooseExtent(swapChainSupport.capabilities, extent.width, extent.height);
    uint32_t imageCount = chooseImageCount(swapChainSupport.capabilities);

    vk::SwapchainCreateInfoKHR createInfo;
    createInfo.setSurface(*surface);
    createInfo.setMinImageCount(imageCount);
    createInfo.setImageFormat(surfaceFormat.format);
    createInfo.setImageColorSpace(surfaceFormat.colorSpace);
    createInfo.setImageExtent(actualExtent);
    createInfo.setImageArrayLayers(1);
    createInfo.setImageUsage(config.imageUsage);
    createInfo.setImageSharingMode(vk::SharingMode::eExclusive);
    createInfo.setPreTransform(swapChainSupport.capabilities.currentTransform);
    createInfo.setCompositeAlpha(config.compositeAlpha);
    createInfo.setPresentMode(presentMode);
    createInfo.setClipped(config.clipped);
    createInfo.setOldSwapchain(nullptr);

    // Handle queue family indices
    QueueFamilyIndices indices = device.getQueueFamilyIndices();
    std::vector<uint32_t> queueFamilyIndicesVec;

    if (indices.graphicsFamily != indices.presentFamily) {
        queueFamilyIndicesVec = {
            indices.graphicsFamily.value(),
            indices.presentFamily.value()
        };
        createInfo.setImageSharingMode(vk::SharingMode::eConcurrent);
        createInfo.setQueueFamilyIndices(queueFamilyIndicesVec);
    }

    try {
        swapChain = device.get().createSwapchainKHR(createInfo);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(),
                            std::string("Failed to create swap chain: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Get swap chain images
    images = swapChain.getImages();

    // Store format and extent
    imageFormat = surfaceFormat.format;
    extent = actualExtent;

    LOG_DEBUG("Swap chain created: " + std::to_string(extent.width) + "x" +
              std::to_string(extent.height) + ", format: " +
              std::to_string(static_cast<uint32_t>(imageFormat)) + ", images: " +
              std::to_string(images.size()));
}

void SwapChainManager::createImageViews() {
    imageViews.clear();

    for (size_t i = 0; i < images.size(); i++) {
        vk::ImageViewCreateInfo createInfo;
        createInfo.setImage(images[i]);
        createInfo.setViewType(vk::ImageViewType::e2D);
        createInfo.setFormat(imageFormat);
        createInfo.setComponents(vk::ComponentMapping{
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity
        });
        createInfo.setSubresourceRange(vk::ImageSubresourceRange{
            vk::ImageAspectFlagBits::eColor,
            0,
            1,
            0,
            1
        });

        try {
            imageViews.emplace_back(device.get().createImageView(createInfo));
        } catch (const vk::SystemError& e) {
            throw VulkanException(e.code(),
                                std::string("Failed to create image view: ") + e.what(),
                                __FUNCTION__, __FILE__, __LINE__);
        }
    }

    LOG_DEBUG("Created " + std::to_string(imageViews.size()) + " image views");
}

vk::SurfaceFormatKHR SwapChainManager::chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats) const {
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

    throw VulkanException(vk::Result::eErrorInitializationFailed,
                         "No suitable surface format found",
                         __FUNCTION__, __FILE__, __LINE__);
}

vk::PresentModeKHR SwapChainManager::choosePresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes) const {
    // Prefer mailbox (triple buffering) if available
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == config.presentMode) {
            return availablePresentMode;
        }
    }

    // Fallback to FIFO (guaranteed to be available)
    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D SwapChainManager::chooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities,
                                         uint32_t width, uint32_t height) const {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    vk::Extent2D actualExtent = {width, height};
    actualExtent.width = std::clamp(actualExtent.width,
                                   capabilities.minImageExtent.width,
                                   capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height,
                                    capabilities.minImageExtent.height,
                                    capabilities.maxImageExtent.height);

    return actualExtent;
}

uint32_t SwapChainManager::chooseImageCount(const vk::SurfaceCapabilitiesKHR& capabilities) const {
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    return imageCount;
}

uint32_t SwapChainManager::acquireNextImage(vk::Semaphore semaphore, vk::Fence fence) const {
    try {
        auto result = swapChain.acquireNextImage(UINT64_MAX, semaphore, fence);
        return result.value;
    } catch (const vk::OutOfDateKHRError& e) {
        // Swap chain needs to be recreated
        return UINT32_MAX;
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(),
                            std::string("Failed to acquire next image: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }
}

void SwapChainManager::presentImage(uint32_t imageIndex, vk::Semaphore waitSemaphore) const {
    vk::PresentInfoKHR presentInfo{};
    if (waitSemaphore) {
        presentInfo.setWaitSemaphores(waitSemaphore);
    }
    presentInfo.setSwapchains(*swapChain);
    presentInfo.setImageIndices(imageIndex);

    try {
        vk::Result result = device.getPresentQueue().presentKHR(presentInfo);
        if (result == vk::Result::eSuboptimalKHR) {
            // Swap chain is suboptimal, but presentation will still succeed
            LOG_DEBUG("Swap chain is suboptimal");
        }
    } catch (const vk::OutOfDateKHRError& e) {
        throw VulkanException(vk::Result::eErrorOutOfDateKHR, "Swap chain out of date",
                            __FUNCTION__, __FILE__, __LINE__);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(),
                            std::string("Failed to present image: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }
}

void SwapChainManager::recreate(uint32_t width, uint32_t height) {
    LOG_INFO("Recreating swap chain: " + std::to_string(width) + "x" + std::to_string(height));

    // Wait for device to be idle
    device.waitIdle();

    // Clear old resources
    imageViews.clear();
    images.clear();

    // Update extent
    extent = vk::Extent2D{width, height};

    // Recreate swap chain
    createSwapChain();
    createImageViews();

    LOG_INFO("Swap chain recreated successfully");
}

SwapChainSupportDetails SwapChainManager::querySupport() const {
    auto physicalDevice = device.getPhysical();

    SwapChainSupportDetails details;
    details.capabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
    details.formats = physicalDevice.getSurfaceFormatsKHR(*surface);
    details.presentModes = physicalDevice.getSurfacePresentModesKHR(*surface);

    return details;
}

bool SwapChainManager::needsRecreation(uint32_t width, uint32_t height) const {
    return extent.width != width || extent.height != height;
}

} // namespace RYBlinnPhong
