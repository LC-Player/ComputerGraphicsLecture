#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <algorithm>
#include "core/Exception.hpp"
#include "core/Logger.hpp"

namespace RYBlinnPhong {

// Forward declaration
class VulkanDevice;

/**
 * @brief Swap chain creation parameters
 */
struct SwapChainConfig {
    vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;
    vk::SurfaceFormatKHR surfaceFormat = { vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear };
    uint32_t minImageCount = 2; // Double buffering by default
    vk::ImageUsageFlags imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    vk::CompositeAlphaFlagBitsKHR compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    bool clipped = true;
};

/**
 * @brief Swap chain support details query result
 */
struct SwapChainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;

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
 * @brief Swap chain manager using vk::raii
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
    SwapChainManager(VulkanDevice& device, vk::raii::SurfaceKHR& surface,
                     uint32_t width, uint32_t height,
                     const SwapChainConfig& config = SwapChainConfig());

    /**
     * @brief Destroy the SwapChainManager object
     */
    ~SwapChainManager() = default;

    // Delete copy constructor and assignment operator
    SwapChainManager(const SwapChainManager&) = delete;
    SwapChainManager& operator=(const SwapChainManager&) = delete;

    /**
     * @brief Move constructor
     */
    SwapChainManager(SwapChainManager&& other) noexcept = default;

    /**
     * @brief Move assignment operator
     */
    SwapChainManager& operator=(SwapChainManager&& other) noexcept = default;

    /**
     * @brief Get the swap chain handle
     *
     * @return vk::raii::SwapchainKHR& Swap chain handle
     */
    vk::raii::SwapchainKHR& get() { return swapChain; }
    const vk::raii::SwapchainKHR& get() const { return swapChain; }

    /**
     * @brief Get the swap chain images
     *
     * @return const std::vector<vk::Image>& Swap chain images
     */
    const std::vector<vk::Image>& getImages() const { return images; }

    /**
     * @brief Get the swap chain image views
     *
     * @return std::vector<vk::raii::ImageView>& Swap chain image views
     */
    std::vector<vk::raii::ImageView>& getImageViews() { return imageViews; }
    const std::vector<vk::raii::ImageView>& getImageViews() const { return imageViews; }

    /**
     * @brief Get the swap chain image format
     *
     * @return vk::Format Image format
     */
    vk::Format getImageFormat() const { return imageFormat; }

    /**
     * @brief Get the swap chain extent
     *
     * @return vk::Extent2D Swap chain extent
     */
    vk::Extent2D getExtent() const { return extent; }

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
    uint32_t acquireNextImage(vk::Semaphore semaphore = nullptr,
                              vk::Fence fence = nullptr) const;

    /**
     * @brief Present an image to the swap chain
     *
     * @param imageIndex Image index to present
     * @param waitSemaphore Semaphore to wait on before presenting
     * @throws VulkanException if presentation fails
     */
    void presentImage(uint32_t imageIndex, vk::Semaphore waitSemaphore = nullptr) const;

    /**
     * @brief Query swap chain support details
     *
     * @return SwapChainSupportDetails Support details
     */
    SwapChainSupportDetails querySupport() const;

private:
    VulkanDevice& device;
    vk::raii::SurfaceKHR& surface;
    vk::raii::SwapchainKHR swapChain = nullptr;
    std::vector<vk::Image> images;
    std::vector<vk::raii::ImageView> imageViews;
    vk::Format imageFormat;
    vk::Extent2D extent;
    SwapChainConfig config;

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
     * @return vk::SurfaceFormatKHR Chosen surface format
     */
    vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats) const;

    /**
     * @brief Choose the best present mode
     *
     * @param availablePresentModes Available present modes
     * @return vk::PresentModeKHR Chosen present mode
     */
    vk::PresentModeKHR choosePresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes) const;

    /**
     * @brief Choose the swap extent
     *
     * @param capabilities Surface capabilities
     * @param width Desired width
     * @param height Desired height
     * @return vk::Extent2D Chosen extent
     */
    vk::Extent2D chooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities,
                           uint32_t width, uint32_t height) const;

    /**
     * @brief Choose the number of images in the swap chain
     *
     * @param capabilities Surface capabilities
     * @return uint32_t Number of images
     */
    uint32_t chooseImageCount(const vk::SurfaceCapabilitiesKHR& capabilities) const;
};

} // namespace RYBlinnPhong
