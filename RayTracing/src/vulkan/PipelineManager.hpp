#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <unordered_map>
#include "core/Exception.hpp"
#include "core/Logger.hpp"

namespace RYRayTracing {

/**
 * @brief Pipeline configuration
 */
struct PipelineConfig {
    // Shader stages
    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    std::string vertexEntryPoint = "main";
    std::string fragmentEntryPoint = "main";

    // Vertex input state
    VkVertexInputBindingDescription vertexBindingDescription = {};
    std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;

    // Input assembly
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool primitiveRestartEnable = false;

    // Viewport and scissor (dynamic)
    bool dynamicViewportAndScissor = true;

    // Rasterization
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE;
    float lineWidth = 1.0f;

    // Multisampling
    VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Color blending
    bool blendEnable = false;
    VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkBlendOp colorBlendOp = VK_BLEND_OP_ADD;
    VkBlendFactor srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp alphaBlendOp = VK_BLEND_OP_ADD;

    // Pipeline layout
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    // Render pass
    VkRenderPass renderPass = VK_NULL_HANDLE;
    uint32_t subpass = 0;
};

/**
 * @brief Pipeline manager
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
    explicit PipelineManager(VkDevice device);

    /**
     * @brief Destroy the PipelineManager object
     */
    ~PipelineManager();

    // Delete copy constructor and assignment operator
    PipelineManager(const PipelineManager&) = delete;
    PipelineManager& operator=(const PipelineManager&) = delete;

    /**
     * @brief Move constructor
     */
    PipelineManager(PipelineManager&& other) noexcept;

    /**
     * @brief Move assignment operator
     */
    PipelineManager& operator=(PipelineManager&& other) noexcept;

    /**
     * @brief Create a graphics pipeline
     *
     * @param name Pipeline name for caching
     * @param config Pipeline configuration
     * @return VkPipeline Graphics pipeline
     */
    VkPipeline createPipeline(const std::string& name, const PipelineConfig& config);

    /**
     * @brief Get a cached pipeline by name
     *
     * @param name Pipeline name
     * @return VkPipeline Graphics pipeline (VK_NULL_HANDLE if not found)
     */
    VkPipeline getPipeline(const std::string& name) const;

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
    VkDevice device;
    std::unordered_map<std::string, VkPipeline> pipelines;
    bool initialized;

    /**
     * @brief Create shader stage info
     *
     * @param shader Shader module
     * @param stage Shader stage
     * @param entryPoint Entry point name
     * @return VkPipelineShaderStageCreateInfo Shader stage create info
     */
    VkPipelineShaderStageCreateInfo createShaderStageInfo(
        VkShaderModule shader, VkShaderStageFlagBits stage, const std::string& entryPoint) const;
};

} // namespace RYRayTracing
