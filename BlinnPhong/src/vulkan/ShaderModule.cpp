#include "ShaderModule.hpp"
#include "VulkanDevice.hpp"
#include <fstream>

namespace RYBlinnPhong {

ShaderModule::ShaderModule(VulkanDevice* device, const ShaderModuleConfig& config)
    : device(device)
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

vk::ShaderStageFlagBits ShaderModule::getVulkanStage() const {
    return stageToVulkan(stage);
}

vk::PipelineShaderStageCreateInfo ShaderModule::getStageCreateInfo() const {
    return vk::PipelineShaderStageCreateInfo{
        {},
        getVulkanStage(),
        *shaderModule,
        entryPoint.c_str()
    };
}

std::vector<char> ShaderModule::loadFromFile(const std::string& filename) {
    LOG_DEBUG("Loading shader from file: " + filename);

    try {
        std::vector<char> code = FileUtil::readBinaryFile(filename);

        if (code.empty()) {
            throw VulkanException(vk::Result::eErrorInitializationFailed,
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
        throw VulkanException(vk::Result::eErrorInitializationFailed,
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
        throw VulkanException(vk::Result::eErrorInitializationFailed,
                            "Shader code is empty",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    vk::ShaderModuleCreateInfo createInfo{
        {},
        code.size(),
        reinterpret_cast<const uint32_t*>(code.data())
    };

    try {
        shaderModule = device->get().createShaderModule(createInfo);
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(), std::string("Failed to create shader module: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }
}

vk::ShaderStageFlagBits ShaderModule::stageToVulkan(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::VERTEX:
            return vk::ShaderStageFlagBits::eVertex;
        case ShaderStage::FRAGMENT:
            return vk::ShaderStageFlagBits::eFragment;
        case ShaderStage::GEOMETRY:
            return vk::ShaderStageFlagBits::eGeometry;
        case ShaderStage::TESSELLATION_CONTROL:
            return vk::ShaderStageFlagBits::eTessellationControl;
        case ShaderStage::TESSELLATION_EVALUATION:
            return vk::ShaderStageFlagBits::eTessellationEvaluation;
        case ShaderStage::COMPUTE:
            return vk::ShaderStageFlagBits::eCompute;
        default:
            throw VulkanException(vk::Result::eErrorInitializationFailed,
                                "Unknown shader stage",
                                __FUNCTION__, __FILE__, __LINE__);
    }
}

std::string ShaderModule::stageToString(vk::ShaderStageFlagBits stage) {
    return vk::to_string(stage);
}

std::string ShaderModule::stageToString(ShaderStage stage) {
    return stageToString(stageToVulkan(stage));
}

} // namespace RYBlinnPhong
