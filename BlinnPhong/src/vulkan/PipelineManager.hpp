#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <string>
#include <unordered_map>
#include "core/Exception.hpp"
#include "core/Logger.hpp"

namespace RYBlinnPhong {

/**
 * @brief Pipeline configuration
 */
struct PipelineConfig {
    vk::ShaderModule vertexShader = nullptr;
    vk::ShaderModule fragmentShader = nullptr;
    std::string vertexEntryPoint = "main";
    std::string fragmentEntryPoint = "main";

    vk::VertexInputBindingDescription vertexBindingDescription = {};
    std::vector<vk::VertexInputBindingDescription> vertexBindingDescriptions;
    std::vector<vk::VertexInputAttributeDescription> vertexAttributeDescriptions;

    vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
    bool primitiveRestartEnable = false;

    bool dynamicViewportAndScissor = true;

    vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
    vk::CullModeFlags cullMode = vk::CullModeFlagBits::eNone;
    vk::FrontFace frontFace = vk::FrontFace::eClockwise;
    float lineWidth = 1.0f;

    vk::SampleCountFlagBits rasterizationSamples = vk::SampleCountFlagBits::e1;

    bool blendEnable = false;
    vk::BlendFactor srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    vk::BlendFactor dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    vk::BlendOp colorBlendOp = vk::BlendOp::eAdd;
    vk::BlendFactor srcAlphaBlendFactor = vk::BlendFactor::eOne;
    vk::BlendFactor dstAlphaBlendFactor = vk::BlendFactor::eZero;
    vk::BlendOp alphaBlendOp = vk::BlendOp::eAdd;

    bool depthTestEnable = false;
    bool depthWriteEnable = true;
    vk::CompareOp depthCompareOp = vk::CompareOp::eLess;

    vk::PipelineLayout pipelineLayout = nullptr;

    vk::RenderPass renderPass = nullptr;
    uint32_t subpass = 0;
};

/**
 * @brief Pipeline manager using vk::raii
 *
 * Manages Vulkan graphics pipeline creation and caching.
 */
class PipelineManager {
public:
    /**
     * @brief Construct a new PipelineManager object
     *
     * @param device Vulkan logical device
     */
    explicit PipelineManager(vk::raii::Device& device);

    /**
     * @brief Destroy the PipelineManager object
     */
    ~PipelineManager() = default;

    // Delete copy constructor and assignment operator
    PipelineManager(const PipelineManager&) = delete;
    PipelineManager& operator=(const PipelineManager&) = delete;

    /**
     * @brief Move constructor
     */
    PipelineManager(PipelineManager&& other) noexcept = default;

    /**
     * @brief Move assignment operator
     */
    PipelineManager& operator=(PipelineManager&& other) noexcept = default;

    /**
     * @brief Create a graphics pipeline
     *
     * @param name Pipeline name for caching
     * @param config Pipeline configuration
     * @return vk::raii::Pipeline& Graphics pipeline
     */
    vk::raii::Pipeline& createPipeline(const std::string& name, const PipelineConfig& config);

    /**
     * @brief Get a cached pipeline by name
     *
     * @param name Pipeline name
     * @return vk::raii::Pipeline* Graphics pipeline (nullptr if not found)
     */
    vk::raii::Pipeline* getPipeline(const std::string& name);

    /**
     * @brief Check if a pipeline exists
     *
     * @param name Pipeline name
     * @return true if pipeline exists, false otherwise
     */
    bool hasPipeline(const std::string& name) const;

    /**
     * @brief Destroy a cached pipeline
     *
     * @param name Pipeline name
     */
    void destroyPipeline(const std::string& name);

    /**
     * @brief Destroy all cached pipelines
     */
    void destroyAllPipelines();

private:
    vk::raii::Device& device;
    std::unordered_map<std::string, vk::raii::Pipeline> pipelines;

    /**
     * @brief Create shader stage info
     *
     * @param shader Shader module
     * @param stage Shader stage
     * @param entryPoint Entry point name
     * @return vk::PipelineShaderStageCreateInfo Shader stage create info
     */
    vk::PipelineShaderStageCreateInfo createShaderStageInfo(
        vk::ShaderModule shader, vk::ShaderStageFlagBits stage, const std::string& entryPoint) const;
};

} // namespace RYBlinnPhong
