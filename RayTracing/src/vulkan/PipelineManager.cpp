#include "PipelineManager.hpp"
#include <array>

namespace RYRayTracing {

PipelineManager::PipelineManager(VkDevice device)
    : device(device), initialized(true) {
    Logger::info("PipelineManager initialized successfully");
}

PipelineManager::~PipelineManager() {
    if (initialized) {
        destroyAllPipelines();
        Logger::info("PipelineManager destroyed");
    }
}

PipelineManager::PipelineManager(PipelineManager&& other) noexcept
    : device(other.device), pipelines(std::move(other.pipelines)),
      initialized(other.initialized) {
    other.pipelines.clear();
    other.initialized = false;
}

PipelineManager& PipelineManager::operator=(PipelineManager&& other) noexcept {
    if (this != &other) {
        if (initialized) {
            destroyAllPipelines();
        }

        device = other.device;
        pipelines = std::move(other.pipelines);
        initialized = other.initialized;

        other.pipelines.clear();
        other.initialized = false;
    }
    return *this;
}

VkPipeline PipelineManager::createPipeline(const std::string& name, const PipelineConfig& config) {
    // Check if pipeline already exists
    if (hasPipeline(name)) {
        Logger::warning("Pipeline '" + name + "' already exists, destroying old pipeline");
        destroyPipeline(name);
    }

    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    if (config.vertexShader != VK_NULL_HANDLE) {
        shaderStages.push_back(createShaderStageInfo(
            config.vertexShader, VK_SHADER_STAGE_VERTEX_BIT, config.vertexEntryPoint));
    }
    if (config.fragmentShader != VK_NULL_HANDLE) {
        shaderStages.push_back(createShaderStageInfo(
            config.fragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT, config.fragmentEntryPoint));
    }

    // Vertex input state
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &config.vertexBindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(config.vertexAttributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = config.vertexAttributeDescriptions.data();

    // Input assembly state
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = config.topology;
    inputAssembly.primitiveRestartEnable = config.primitiveRestartEnable;

    // Viewport and scissor state (dynamic)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = config.polygonMode;
    rasterizer.lineWidth = config.lineWidth;
    rasterizer.cullMode = config.cullMode;
    rasterizer.frontFace = config.frontFace;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling state
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = config.rasterizationSamples;

    // Color blending state
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = config.blendEnable;
    colorBlendAttachment.srcColorBlendFactor = config.srcColorBlendFactor;
    colorBlendAttachment.dstColorBlendFactor = config.dstColorBlendFactor;
    colorBlendAttachment.colorBlendOp = config.colorBlendOp;
    colorBlendAttachment.srcAlphaBlendFactor = config.srcAlphaBlendFactor;
    colorBlendAttachment.dstAlphaBlendFactor = config.dstAlphaBlendFactor;
    colorBlendAttachment.alphaBlendOp = config.alphaBlendOp;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state
    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Graphics pipeline create info
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = config.pipelineLayout;
    pipelineInfo.renderPass = config.renderPass;
    pipelineInfo.subpass = config.subpass;

    // Create pipeline
    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    if (result != VK_SUCCESS) {
        throw VulkanException(result, "Failed to create graphics pipeline", __FUNCTION__, __FILE__, __LINE__);
    }

    // Cache pipeline
    pipelines[name] = pipeline;
    Logger::debug("Graphics pipeline '" + name + "' created successfully");

    return pipeline;
}

VkPipeline PipelineManager::getPipeline(const std::string& name) const {
    auto it = pipelines.find(name);
    if (it != pipelines.end()) {
        return it->second;
    }
    return VK_NULL_HANDLE;
}

bool PipelineManager::hasPipeline(const std::string& name) const {
    return pipelines.find(name) != pipelines.end();
}

void PipelineManager::destroyPipeline(const std::string& name) {
    auto it = pipelines.find(name);
    if (it != pipelines.end()) {
        vkDestroyPipeline(device, it->second, nullptr);
        pipelines.erase(it);
        Logger::debug("Pipeline '" + name + "' destroyed");
    }
}

void PipelineManager::destroyAllPipelines() {
    for (auto& pair : pipelines) {
        vkDestroyPipeline(device, pair.second, nullptr);
    }
    pipelines.clear();
    Logger::debug("All pipelines destroyed");
}

VkPipelineShaderStageCreateInfo PipelineManager::createShaderStageInfo(
    VkShaderModule shader, VkShaderStageFlagBits stage, const std::string& entryPoint) const {

    VkPipelineShaderStageCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    createInfo.stage = stage;
    createInfo.module = shader;
    createInfo.pName = entryPoint.c_str();

    return createInfo;
}

} // namespace RYRayTracing
