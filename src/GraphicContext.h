#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <fstream>

namespace RYRayTracing {

    static std::vector<char> ReadFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("failed to open file!");
        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        return buffer;
    }

    const int WIDTH = 800;
    const int HEIGHT = 600;

    struct Vertex {
        float pos[2];
        float color[3];
    };

    const std::vector<Vertex> vertices = {
        {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, 0.5f},  {0.0f, 1.0f, 0.0f}},
        {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}
    };

    class VulkanApp {
    public:
        void Run();

    private:
        GLFWwindow* window;

        VkInstance instance;
        VkSurfaceKHR surface;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device;
        VkQueue graphicsQueue;
        VkQueue presentQueue;

        VkSwapchainKHR swapChain;
        std::vector<VkImage> swapChainImages;
        VkFormat swapChainImageFormat;
        VkExtent2D swapChainExtent;
        std::vector<VkImageView> swapChainImageViews;

        VkRenderPass renderPass;
        VkPipelineLayout pipelineLayout;
        VkPipeline graphicsPipeline;
        std::vector<VkFramebuffer> swapChainFramebuffers;

        VkCommandPool commandPool;
        std::vector<VkCommandBuffer> commandBuffers;

        VkBuffer vertexBuffer;
        VkDeviceMemory vertexBufferMemory;

        int queueFamilyIndex;

        void InitWindow();

        void InitVulkan();

        void MainLoop();

        void Cleanup();

        void DrawFrame();

        void CreateInstance();

        void CreateSurface();

        void PickPhysicalDevice();

        int FindQueueFamily(VkPhysicalDevice device);

        void CreateLogicalDevice();

        void CreateSwapChain();

        void CreateImageViews();

        void CreateRenderPass();

        VkShaderModule CreateShaderModule(const std::vector<char>& code);

        void CreateGraphicsPipeline();

        void CreateFramebuffers();

        void CreateCommandPool();

        void CreateVertexBuffer();

        void CreateCommandBuffers();
    };
}