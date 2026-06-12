#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <string>
#include "core/Exception.hpp"
#include "core/Logger.hpp"
#include "utils/FileUtil.hpp"
#include "VulkanDevice.hpp"

namespace RYRayTracing {

/**
 * @brief Shader stage type
 */
enum class ShaderStage {
    VERTEX,
    FRAGMENT,
    GEOMETRY,
    TESSELLATION_CONTROL,
    TESSELLATION_EVALUATION,
    COMPUTE,
    RAY_GEN,
    CLOSEST_HIT,
    MISS
};

/**
 * @brief Shader module creation parameters
 */
struct ShaderModuleConfig {
    std::string filename;           // SPIR-V file path
    ShaderStage stage;              // Shader stage
    std::string entryPoint = "main"; // Entry point name
};

/**
 * @brief Vulkan shader module wrapper using vk::raii
 *
 * Manages the lifecycle of Vulkan shader modules.
 */
class ShaderModule {
public:
    /**
     * @brief Construct a new ShaderModule object from SPIR-V file
     *
     * @param device Vulkan device
     * @param config Shader module configuration
     */
    ShaderModule(VulkanDevice* device, const ShaderModuleConfig& config);

    /**
     * @brief Construct a new ShaderModule object from SPIR-V code
     *
     * @param device Vulkan device
     * @param code SPIR-V bytecode
     * @param stage Shader stage
     * @param entryPoint Entry point name (default: "main")
     */
    ShaderModule(VulkanDevice* device, const std::vector<char>& code,
                 ShaderStage stage, const std::string& entryPoint = "main");

    /**
     * @brief Destroy the ShaderModule object
     */
    ~ShaderModule() = default;

    // Delete copy constructor and assignment operator
    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    /**
     * @brief Move constructor
     */
    ShaderModule(ShaderModule&& other) noexcept = default;

    /**
     * @brief Move assignment operator
     */
    ShaderModule& operator=(ShaderModule&& other) noexcept = default;

    /**
     * @brief Get the shader module handle
     *
     * @return vk::raii::ShaderModule& Shader module handle
     */
    vk::raii::ShaderModule& get() { return shaderModule; }
    const vk::raii::ShaderModule& get() const { return shaderModule; }

    /**
     * @brief Get the shader stage
     *
     * @return ShaderStage Shader stage
     */
    ShaderStage getStage() const { return stage; }

    /**
     * @brief Get the entry point name
     *
     * @return const std::string& Entry point name
     */
    const std::string& getEntryPoint() const { return entryPoint; }

    /**
     * @brief Get the Vulkan shader stage flag
     *
     * @return vk::ShaderStageFlagBits Vulkan shader stage flag
     */
    vk::ShaderStageFlagBits getVulkanStage() const;

    /**
     * @brief Get the pipeline shader stage create info
     *
     * @return vk::PipelineShaderStageCreateInfo Pipeline shader stage create info
     */
    vk::PipelineShaderStageCreateInfo getStageCreateInfo() const;

    /**
     * @brief Load SPIR-V code from file
     *
     * @param filename SPIR-V file path
     * @return std::vector<char> SPIR-V bytecode
     * @throws VulkanException if file cannot be read
     */
    static std::vector<char> loadFromFile(const std::string& filename);

    /**
     * @brief Create a vertex shader module
     *
     * @param device Vulkan device
     * @param filename SPIR-V file path
     * @param entryPoint Entry point name (default: "main")
     * @return ShaderModule Vertex shader module
     */
    static ShaderModule createVertexShader(VulkanDevice* device,
                                           const std::string& filename,
                                           const std::string& entryPoint = "main");

    /**
     * @brief Create a fragment shader module
     *
     * @param device Vulkan device
     * @param filename SPIR-V file path
     * @param entryPoint Entry point name (default: "main")
     * @return ShaderModule Fragment shader module
     */
    static ShaderModule createFragmentShader(VulkanDevice* device,
                                            const std::string& filename,
                                            const std::string& entryPoint = "main");

    /**
     * @brief Create a geometry shader module
     *
     * @param device Vulkan device
     * @param filename SPIR-V file path
     * @param entryPoint Entry point name (default: "main")
     * @return ShaderModule Geometry shader module
     */
    static ShaderModule createGeometryShader(VulkanDevice* device,
                                            const std::string& filename,
                                            const std::string& entryPoint = "main");

    /**
     * @brief Create a compute shader module
     *
     * @param device Vulkan device
     * @param filename SPIR-V file path
     * @param entryPoint Entry point name (default: "main")
     * @return ShaderModule Compute shader module
     */
    static ShaderModule createComputeShader(VulkanDevice* device,
                                           const std::string& filename,
                                           const std::string& entryPoint = "main");

    static ShaderModule createRayGenShader(VulkanDevice* device,
                                            const std::string& filename,
                                            const std::string& entryPoint = "rayGen");
    static ShaderModule createClosestHitShader(VulkanDevice* device,
                                                const std::string& filename,
                                                const std::string& entryPoint = "closestHit");
    static ShaderModule createMissShader(VulkanDevice* device,
                                          const std::string& filename,
                                          const std::string& entryPoint = "miss");

private:
    VulkanDevice* device;  // Non-owning pointer, lifetime managed 
    vk::raii::ShaderModule shaderModule = nullptr;
    ShaderStage stage;
    std::string entryPoint;
    std::vector<char> code;

    /**
     * @brief Create the Vulkan shader module
     *
     * @throws VulkanException if shader module creation fails
     */
    void createShaderModule();

    /**
     * @brief Convert ShaderStage to Vulkan stage flag
     *
     * @param stage Shader stage
     * @return vk::ShaderStageFlagBits Vulkan stage flag
     */
    static vk::ShaderStageFlagBits stageToVulkan(ShaderStage stage);

    /**
     * @brief Convert Vulkan stage flag to string
     *
     * @param stage Vulkan stage flag
     * @return std::string Stage name
     */
    static std::string stageToString(vk::ShaderStageFlagBits stage);

    /**
     * @brief Convert ShaderStage to string
     *
     * @param stage Shader stage
     * @return std::string Stage name
     */
    static std::string stageToString(ShaderStage stage);
};

} // namespace RYRayTracing
