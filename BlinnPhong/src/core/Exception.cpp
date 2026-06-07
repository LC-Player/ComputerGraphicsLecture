#include "Exception.hpp"
#include <sstream>
#include <iomanip>

namespace RYBlinnPhong {

VulkanException::VulkanException(vk::Result result, const std::string& message,
                               const std::string& function, const std::string& file, int line)
    : std::runtime_error(message)
    , errorCode(result)
    , functionName(function)
    , fileName(file)
    , lineNumber(line) {
}
VulkanException::VulkanException(std::error_code code, const std::string& message,
                               const std::string& function, const std::string& file, int line)
    : std::runtime_error(message)
    , errorCode(static_cast<vk::Result>(code.value()))
    , functionName(function)
    , fileName(file)
    , lineNumber(line) {
}

std::string VulkanException::getLocation() const {
    std::ostringstream oss;
    oss << "at " << functionName << " in " << fileName << ":" << lineNumber;
    return oss.str();
}

std::string VulkanException::getErrorString(vk::Result result) {
    // vk::Result already has to_string support via vulkan.hpp
    return vk::to_string(result);
}

} // namespace RYBlinnPhong