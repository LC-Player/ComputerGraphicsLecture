#include "ShaderModule.hpp"
#include "VulkanDevice.hpp"
#include <fstream>

namespace RYRayTracing {

ShaderModule::ShaderModule(VulkanDevice* device, const ShaderModuleConfig& config)
    : device(device)
    , shaderModule(VK_NULL_HANDLE)
    , stage(config.stage)
    , entryPoint(config.entryPoint) {

    LOG_INFO("Creating shader module from file: " + config.filename);

    // Load SPIR-V code from file
    code = loadFromFile(config.filename);

    // Create shader module
    createShaderModule();

    LOG_INFO("Shader module created: " + stageToString(stage) +
             ", entryPoint=" + entryPoint +
             ", codeSize=" + std::to_string(code.size()) + " bytes");
}

ShaderModule::ShaderModule(VulkanDevice* device, const std::vector<char>& code,
                         ShaderStage stage, const std::string& entryPoint)
    : device(device)
    , shaderModule(VK_NULL_HANDLE)
    , stage(stage)
    , entryPoint(entryPoint)
    , code(code) {

    LOG_INFO("Creating shader module from memory: " + stageToString(stage));

    // Create shader module
    createShaderModule();

    LOG_INFO("Shader module created: " + stageToString(stage) +
             ", entryPoint=" + entryPoint +
             ", codeSize=" + std::to_string(code.size()) + " bytes");
}

ShaderModule::~ShaderModule() {
    LOG_DEBUG("Destroying shader module: " + stageToString(stage));

    if (shaderModule != VK_NULL_HANDLE) {
        VkDevice vkDevice = device->get();
        vkDestroyShaderModule(vkDevice, shaderModule, nullptr);
        shaderModule = VK_NULL_HANDLE;
    }

    LOG_DEBUG("Shader module destroyed");
}

ShaderModule::ShaderModule(ShaderModule&& other) noexcept
    : device(other.device)
    , shaderModule(other.shaderModule)
    , stage(other.stage)
    , entryPoint(std::move(other.entryPoint))
    , code(std::move(other.code)) {
    other.shaderModule = VK_NULL_HANDLE;
}

ShaderModule& ShaderModule::operator=(ShaderModule&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        if (shaderModule != VK_NULL_HANDLE) {
            VkDevice vkDevice = device->get();
            vkDestroyShaderModule(vkDevice, shaderModule, nullptr);
        }

        // Move resources from other
        device = other.device;
        shaderModule = other.shaderModule;
        stage = other.stage;
        entryPoint = std::move(other.entryPoint);
        code = std::move(other.code);

        // Reset other
        other.shaderModule = VK_NULL_HANDLE;
    }
    return *this;
}

VkShaderStageFlagBits ShaderModule::getVulkanStage() const {
    return stageToVulkan(stage);
}

VkPipelineShaderStageCreateInfo ShaderModule::getStageCreateInfo() const {
    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = getVulkanStage();
    stageInfo.module = shaderModule;
    stageInfo.pName = entryPoint.c_str();
    stageInfo.pSpecializationInfo = nullptr; // No specialization constants for now

    return stageInfo;
}

std::vector<char> ShaderModule::loadFromFile(const std::string& filename) {
    LOG_DEBUG("Loading shader from file: " + filename);

    try {
        std::vector<char> code = FileUtil::readBinaryFile(filename);

        if (code.empty()) {
            throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                                "Shader file is empty: " + filename,
                                __FUNCTION__, __FILE__, __LINE__);
        }

        // SPIR-V files should be a multiple of 4 bytes
        if (code.size() % 4 != 0) {
            LOG_WARNING("Shader file size is not a multiple of 4: " + filename);
        }

        LOG_DEBUG("Shader loaded successfully: " + std::to_string(code.size()) + " bytes");
        return code;

    } catch (const std::exception& e) {
        throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                            "Failed to load shader file: " + std::string(e.what()),
                            __FUNCTION__, __FILE__, __LINE__);
    }
}

ShaderModule ShaderModule::createVertexShader(VulkanDevice* device,
                                             const std::string& filename,
                                             const std::string& entryPoint) {
    ShaderModuleConfig config;
    config.filename = filename;
    config.stage = ShaderStage::VERTEX;
    config.entryPoint = entryPoint;

    return ShaderModule(device, config);
}

ShaderModule ShaderModule::createFragmentShader(VulkanDevice* device,
                                               const std::string& filename,
                                               const std::string& entryPoint) {
    ShaderModuleConfig config;
    config.filename = filename;
    config.stage = ShaderStage::FRAGMENT;
    config.entryPoint = entryPoint;

    return ShaderModule(device, config);
}

ShaderModule ShaderModule::createGeometryShader(VulkanDevice* device,
                                               const std::string& filename,
                                               const std::string& entryPoint) {
    ShaderModuleConfig config;
    config.filename = filename;
    config.stage = ShaderStage::GEOMETRY;
    config.entryPoint = entryPoint;

    return ShaderModule(device, config);
}

ShaderModule ShaderModule::createComputeShader(VulkanDevice* device,
                                              const std::string& filename,
                                              const std::string& entryPoint) {
    ShaderModuleConfig config;
    config.filename = filename;
    config.stage = ShaderStage::COMPUTE;
    config.entryPoint = entryPoint;

    return ShaderModule(device, config);
}

void ShaderModule::createShaderModule() {
    if (code.empty()) {
        throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                            "Shader code is empty",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkDevice vkDevice = device->get();
    VK_CHECK_RESULT(vkCreateShaderModule(vkDevice, &createInfo, nullptr, &shaderModule));
}

VkShaderStageFlagBits ShaderModule::stageToVulkan(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::VERTEX:
            return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderStage::FRAGMENT:
            return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderStage::GEOMETRY:
            return VK_SHADER_STAGE_GEOMETRY_BIT;
        case ShaderStage::TESSELLATION_CONTROL:
            return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case ShaderStage::TESSELLATION_EVALUATION:
            return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        case ShaderStage::COMPUTE:
            return VK_SHADER_STAGE_COMPUTE_BIT;
        default:
            throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                                "Unknown shader stage",
                                __FUNCTION__, __FILE__, __LINE__);
    }
}

std::string ShaderModule::stageToString(VkShaderStageFlagBits stage) {
    switch (stage) {
        case VK_SHADER_STAGE_VERTEX_BIT:
            return "VERTEX";
        case VK_SHADER_STAGE_FRAGMENT_BIT:
            return "FRAGMENT";
        case VK_SHADER_STAGE_GEOMETRY_BIT:
            return "GEOMETRY";
        case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
            return "TESSELLATION_CONTROL";
        case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
            return "TESSELLATION_EVALUATION";
        case VK_SHADER_STAGE_COMPUTE_BIT:
            return "COMPUTE";
        case VK_SHADER_STAGE_ALL_GRAPHICS:
            return "ALL_GRAPHICS";
        case VK_SHADER_STAGE_ALL:
            return "ALL";
        default:
            return "UNKNOWN";
    }
}

std::string ShaderModule::stageToString(ShaderStage stage) {
    return stageToString(stageToVulkan(stage));
}

} // namespace RYRayTracing