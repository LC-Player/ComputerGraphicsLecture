#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <algorithm>
#include "core/Exception.hpp"
#include "core/Logger.hpp"

namespace RYRayTracing {

// Forward declaration
class VulkanDevice;

/**
 * @brief Swap chain creation parameters
 */
struct SwapChainConfig {
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    VkSurfaceFormatKHR surfaceFormat = { VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    uint32_t minImageCount = 2; // Double buffering by default
    VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    bool clipped = VK_TRUE;
};

/**
 * @brief Swap chain support details query result
 */
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;

    /**
     * @brief Check if swap chain is adequately supported
     *
     * @return true if swap chain is supported, false otherwise
     */
    bool isAdequate() const {
        return !formats.empty() && !presentModes.empty();
    }
};

/**
 * @brief Swap chain manager
 *
 * Manages the creation, recreation, and destruction of swap chains.
 */
class SwapChainManager {
public:
    /**
     * @brief Construct a new SwapChainManager object
     *
     * @param device Vulkan device
     * @param surface Window surface
     * @param width Swap chain width
     * @param height Swap chain height
     * @param config Swap chain configuration
     */
    SwapChainManager(VulkanDevice* device, VkSurfaceKHR surface,
                     uint32_t width, uint32_t height,
                     const SwapChainConfig& config = SwapChainConfig());

    /**
     * @brief Destroy the SwapChainManager object
     */
    ~SwapChainManager();

    // Delete copy constructor and assignment operator
    SwapChainManager(const SwapChainManager&) = delete;
    SwapChainManager& operator=(const SwapChainManager&) = delete;

    /**
     * @brief Move constructor
     */
    SwapChainManager(SwapChainManager&& other) noexcept;

    /**
     * @brief Move assignment operator
     */
    SwapChainManager& operator=(SwapChainManager&& other) noexcept;

    /**
     * @brief Get the swap chain handle
     *
     * @return VkSwapchainKHR Swap chain handle
     */
    VkSwapchainKHR get() const { return swapChain; }

    /**
     * @brief Get the swap chain images
     *
     * @return const std::vector<VkImage>& Swap chain images
     */
    const std::vector<VkImage>& getImages() const { return images; }

    /**
     * @brief Get the swap chain image views
     *
     * @return const std::vector<VkImageView>& Swap chain image views
     */
    const std::vector<VkImageView>& getImageViews() const { return imageViews; }

    /**
     * @brief Get the swap chain image format
     *
     * @return VkFormat Image format
     */
    VkFormat getImageFormat() const { return imageFormat; }

    /**
     * @brief Get the swap chain extent
     *
     * @return VkExtent2D Swap chain extent
     */
    VkExtent2D getExtent() const { return extent; }

    /**
     * @brief Get the number of images in the swap chain
     *
     * @return uint32_t Number of images
     */
    uint32_t getImageCount() const { return static_cast<uint32_t>(images.size()); }

    /**
     * @brief Check if the swap chain needs to be recreated
     *
     * @param width New width
     * @param height New height
     * @return true if swap chain needs recreation, false otherwise
     */
    bool needsRecreation(uint32_t width, uint32_t height) const;

    /**
     * @brief Recreate the swap chain
     *
     * @param width New width
     * @param height New height
     */
    void recreate(uint32_t width, uint32_t height);

    /**
     * @brief Acquire the next image in the swap chain
     *
     * @param semaphore Semaphore to signal when image is available
     * @param fence Fence to signal when image is available
     * @return uint32_t Image index
     * @throws VulkanException if image acquisition fails
     */
    uint32_t acquireNextImage(VkSemaphore semaphore = VK_NULL_HANDLE,
                              VkFence fence = VK_NULL_HANDLE) const;

    /**
     * @brief Present an image to the swap chain
     *
     * @param imageIndex Image index to present
     * @param waitSemaphore Semaphore to wait on before presenting
     * @throws VulkanException if presentation fails
     */
    void presentImage(uint32_t imageIndex, VkSemaphore waitSemaphore = VK_NULL_HANDLE) const;

    /**
     * @brief Query swap chain support details
     *
     * @return SwapChainSupportDetails Support details
     */
    SwapChainSupportDetails querySupport() const;

    /**
     * @brief Cleanup swap chain resources (images and image views)
     */
    void cleanupResources();

private:
    VulkanDevice* device;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapChain;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    VkFormat imageFormat;
    VkExtent2D extent;
    SwapChainConfig config;
    bool initialized;

    /**
     * @brief Create the swap chain
     *
     * @throws VulkanException if swap chain creation fails
     */
    void createSwapChain();

    /**
     * @brief Create image views for swap chain images
     *
     * @throws VulkanException if image view creation fails
     */
    void createImageViews();

    /**
     * @brief Choose the best surface format
     *
     * @param availableFormats Available surface formats
     * @return VkSurfaceFormatKHR Chosen surface format
     */
    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const;

    /**
     * @brief Choose the best present mode
     *
     * @param availablePresentModes Available present modes
     * @return VkPresentModeKHR Chosen present mode
     */
    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;

    /**
     * @brief Choose the swap extent
     *
     * @param capabilities Surface capabilities
     * @param width Desired width
     * @param height Desired height
     * @return VkExtent2D Chosen extent
     */
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities,
                           uint32_t width, uint32_t height) const;

    /**
     * @brief Choose the number of images in the swap chain
     *
     * @param capabilities Surface capabilities
     * @return uint32_t Number of images
     */
    uint32_t chooseImageCount(const VkSurfaceCapabilitiesKHR& capabilities) const;
};

} // namespace RYRayTracing