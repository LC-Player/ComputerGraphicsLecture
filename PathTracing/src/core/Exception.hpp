#pragma once

#include <vulkan/vulkan.hpp>
#include <stdexcept>
#include <string>
#include <sstream>

namespace RYRayTracing {

/**
 * @brief Vulkan-specific exception class that provides detailed error information
 *
 * This exception class extends std::runtime_error to include Vulkan-specific
 * error information such as error codes, function names, and file locations.
 */
class VulkanException : public std::runtime_error {
public:
    /**
     * @brief Construct a new VulkanException object
     *
     * @param result Vulkan error code (vk::Result)
     * @param message Description of the error
     * @param function Name of the function where the error occurred
     * @param file Name of the file where the error occurred
     * @param line Line number where the error occurred
     */
    VulkanException(vk::Result result, const std::string& message,
                   const std::string& function, const std::string& file, int line);
    /**
     * @brief Construct a new VulkanException object
     *
     * @param result Vulkan error code
     * @param message Description of the error
     * @param function Name of the function where the error occurred
     * @param file Name of the file where the error occurred
     * @param line Line number where the error occurred
     */
    VulkanException(std::error_code, const std::string& message,
                   const std::string& function, const std::string& file, int line);

    /**
     * @brief Get the Vulkan error code
     *
     * @return vk::Result The Vulkan error code
     */
    vk::Result getErrorCode() const { return errorCode; }

    /**
     * @brief Get the error location (function, file, line)
     *
     * @return std::string Formatted location string
     */
    std::string getLocation() const;

    /**
     * @brief Get a human-readable description of the Vulkan error code
     *
     * @return std::string Description of the error code
     */
    static std::string getErrorString(vk::Result result);

private:
    vk::Result errorCode;
    std::string functionName;
    std::string fileName;
    int lineNumber;
};

/**
 * @brief Macro to simplify Vulkan error checking
 *
 * This macro checks a Vulkan function result and throws a VulkanException
 * if the result is not vk::Result::eSuccess.
 *
 * Example usage:
 * @code
 * VK_CHECK_RESULT(device.createBuffer(&createInfo, nullptr, &buffer));
 * @endcode
 */
#define VK_CHECK_RESULT(x) \
    do { \
        vk::Result result = (x); \
        if (result != vk::Result::eSuccess) { \
            throw VulkanException(result, #x, __FUNCTION__, __FILE__, __LINE__); \
        } \
    } while(0)

/**
 * @brief Macro for Vulkan assertions with detailed error messages
 *
 * This macro checks a condition and throws a VulkanException if the
 * condition is false.
 *
 * Example usage:
 * @code
 * VK_ASSERT(device, "Device must not be null");
 * @endcode
 */
#define VK_ASSERT(condition, message) \
    if (!(condition)) { \
        throw VulkanException(vk::Result::eErrorUnknown, message, __FUNCTION__, __FILE__, __LINE__); \
    }

} // namespace RYRayTracing