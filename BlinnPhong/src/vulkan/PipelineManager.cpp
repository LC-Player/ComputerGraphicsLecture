#include "PipelineManager.hpp"
#include <array>

namespace RYBlinnPhong {

PipelineManager::PipelineManager(vk::raii::Device& device)
    : device(device) {
    LOG_INFO("PipelineManager initialized successfully");
}

vk::raii::Pipeline& PipelineManager::createPipeline(const std::string& name, const PipelineConfig& config) {
    // Check if pipeline already exists
    if (hasPipeline(name)) {
        LOG_WARNING("Pipeline '" + name + "' already exists, destroying old pipeline");
        destroyPipeline(name);
    }

    // Shader stages
    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
    if (config.vertexShader) {
        shaderStages.push_back(createShaderStageInfo(
            config.vertexShader, vk::ShaderStageFlagBits::eVertex, config.vertexEntryPoint));
    }
    if (config.fragmentShader) {
        shaderStages.push_back(createShaderStageInfo(
            config.fragmentShader, vk::ShaderStageFlagBits::eFragment, config.fragmentEntryPoint));
    }

    // Vertex input state
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
    if (!config.vertexBindingDescriptions.empty()) {
        vertexInputInfo.setVertexBindingDescriptions(config.vertexBindingDescriptions);
    } else {
        vertexInputInfo.setVertexBindingDescriptions({ config.vertexBindingDescription });
    }
    vertexInputInfo.setVertexAttributeDescriptions(config.vertexAttributeDescriptions);

    // Input assembly state
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
    inputAssembly.setTopology(config.topology);
    inputAssembly.setPrimitiveRestartEnable(config.primitiveRestartEnable);

    // Viewport and scissor state (dynamic)
    vk::PipelineViewportStateCreateInfo viewportState;
    viewportState.setViewportCount(1);
    viewportState.setScissorCount(1);

    // Rasterization state
    vk::PipelineRasterizationStateCreateInfo rasterizer;
    rasterizer.setDepthClampEnable(false);
    rasterizer.setRasterizerDiscardEnable(false);
    rasterizer.setPolygonMode(config.polygonMode);
    rasterizer.setCullMode(config.cullMode);
    rasterizer.setFrontFace(config.frontFace);
    rasterizer.setDepthBiasEnable(false);
    rasterizer.setDepthBiasConstantFactor(0.0f);
    rasterizer.setDepthBiasClamp(0.0f);
    rasterizer.setDepthBiasSlopeFactor(0.0f);
    rasterizer.setLineWidth(1.0f);

    // Multisampling state
    vk::PipelineMultisampleStateCreateInfo multisampling;
    multisampling.setRasterizationSamples(config.rasterizationSamples);
    multisampling.setSampleShadingEnable(false);

    // Color blending state
    vk::PipelineColorBlendAttachmentState colorBlendAttachment;
    colorBlendAttachment.setBlendEnable(config.blendEnable);
    colorBlendAttachment.setSrcColorBlendFactor(config.srcColorBlendFactor);
    colorBlendAttachment.setDstColorBlendFactor(config.dstColorBlendFactor);
    colorBlendAttachment.setColorBlendOp(config.colorBlendOp);
    colorBlendAttachment.setSrcAlphaBlendFactor(config.srcAlphaBlendFactor);
    colorBlendAttachment.setDstAlphaBlendFactor(config.dstAlphaBlendFactor);
    colorBlendAttachment.setAlphaBlendOp(config.alphaBlendOp);
    colorBlendAttachment.setColorWriteMask(
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    );

    vk::PipelineColorBlendStateCreateInfo colorBlending;
    colorBlending.setLogicOpEnable(false);
    colorBlending.setAttachments(colorBlendAttachment);

    vk::PipelineDepthStencilStateCreateInfo depthStencil;
    depthStencil.setDepthTestEnable(config.depthTestEnable);
    depthStencil.setDepthWriteEnable(config.depthWriteEnable);
    depthStencil.setDepthCompareOp(config.depthCompareOp);
    depthStencil.setDepthBoundsTestEnable(false);
    depthStencil.setStencilTestEnable(false);

    std::array<vk::DynamicState, 2> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamicState;
    dynamicState.setDynamicStates(dynamicStates);

    vk::GraphicsPipelineCreateInfo pipelineInfo;
    pipelineInfo.setStages(shaderStages);
    pipelineInfo.setPVertexInputState(&vertexInputInfo);
    pipelineInfo.setPInputAssemblyState(&inputAssembly);
    pipelineInfo.setPViewportState(&viewportState);
    pipelineInfo.setPRasterizationState(&rasterizer);
    pipelineInfo.setPMultisampleState(&multisampling);
    pipelineInfo.setPDepthStencilState(&depthStencil);
    pipelineInfo.setPColorBlendState(&colorBlending);
    pipelineInfo.setPDynamicState(&dynamicState);
    pipelineInfo.setLayout(config.pipelineLayout);
    pipelineInfo.setRenderPass(config.renderPass);
    pipelineInfo.setSubpass(config.subpass);

    // Create pipeline
    try {
        auto pipelineResult = device.createGraphicsPipelines(nullptr, pipelineInfo);
        auto [it, success] = pipelines.emplace(name, std::move(pipelineResult[0]));
        LOG_DEBUG("Graphics pipeline '" + name + "' created successfully");
        return it->second;
    } catch (const vk::SystemError& e) {
        throw VulkanException(e.code(),
                            std::string("Failed to create graphics pipeline: ") + e.what(),
                            __FUNCTION__, __FILE__, __LINE__);
    }
}

vk::raii::Pipeline* PipelineManager::getPipeline(const std::string& name) {
    auto it = pipelines.find(name);
    if (it != pipelines.end()) {
        return &it->second;
    }
    return nullptr;
}

bool PipelineManager::hasPipeline(const std::string& name) const {
    return pipelines.find(name) != pipelines.end();
}

void PipelineManager::destroyPipeline(const std::string& name) {
    auto it = pipelines.find(name);
    if (it != pipelines.end()) {
        pipelines.erase(it);
        LOG_DEBUG("Pipeline '" + name + "' destroyed");
    }
}

void PipelineManager::destroyAllPipelines() {
    pipelines.clear();
    LOG_DEBUG("All pipelines destroyed");
}

vk::PipelineShaderStageCreateInfo PipelineManager::createShaderStageInfo(
    vk::ShaderModule shader, vk::ShaderStageFlagBits stage, const std::string& entryPoint) const {

    return vk::PipelineShaderStageCreateInfo{
        {},
        stage,
        shader,
        entryPoint.c_str()
    };
}

} // namespace RYBlinnPhong
