#pragma once

#include <vulkan/vulkan.hpp>
#include <string>

namespace RYRayTracing {

// Vulkan result to string - now using vk::to_string from vulkan.hpp
inline std::string vkResultToString(vk::Result result) {
    return vk::to_string(result);
}

} // namespace RYRayTracing
