#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>
#include <functional>
#include "core/Logger.hpp"

namespace RYRayTracing {

/**
 * @brief Window event callbacks
 */
struct WindowCallbacks {
    std::function<void(int width, int height)> onResize;
    std::function<void()> onClose;
    std::function<void(int key, int action)> onKey;
    std::function<void(double x, double y)> onMouseMove;
    std::function<void(int button, int action)> onMouseButton;
};

/**
 * @brief Window creation parameters
 */
struct WindowConfig {
    int width = 800;
    int height = 600;
    std::string title = "Vulkan Application";
    bool resizable = true;
    bool fullscreen = false;
    bool vsync = false;
};

/**
 * @brief GLFW window manager
 *
 * Manages window creation, event handling, and Vulkan surface creation.
 */
class WindowManager {
public:
    /**
     * @brief Construct a new WindowManager object
     *
     * @param config Window configuration
     */
    explicit WindowManager(const WindowConfig& config = WindowConfig());

    /**
     * @brief Destroy the WindowManager object
     */
    ~WindowManager();

    // Delete copy constructor and assignment operator
    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    /**
     * @brief Move constructor
     */
    WindowManager(WindowManager&& other) noexcept;

    /**
     * @brief Move assignment operator
     */
    WindowManager& operator=(WindowManager&& other) noexcept;

    /**
     * @brief Initialize GLFW and create the window
     *
     * @throws VulkanException if initialization fails
     */
    void init();

    /**
     * @brief Check if the window should close
     *
     * @return true if window should close, false otherwise
     */
    bool shouldClose() const;

    /**
     * @brief Poll window events
     */
    void pollEvents();

    /**
     * @brief Get the window handle
     *
     * @return GLFWwindow* Window handle
     */
    GLFWwindow* getHandle() const { return window; }

    /**
     * @brief Get the window width
     *
     * @return int Window width
     */
    int getWidth() const { return width; }

    /**
     * @brief Get the window height
     *
     * @return int Window height
     */
    int getHeight() const { return height; }

    /**
     * @brief Get the framebuffer size (actual rendering size)
     *
     * @return std::pair<int, int> Framebuffer width and height
     */
    std::pair<int, int> getFramebufferSize() const;

    /**
     * @brief Create a Vulkan surface for the window
     *
     * @param instance Vulkan instance
     * @return VkSurfaceKHR Created surface
     * @throws VulkanException if surface creation fails
     */
    VkSurfaceKHR createSurface(VkInstance instance) const;

    /**
     * @brief Check if window was resized
     *
     * @return true if window was resized, false otherwise
     */
    bool wasResized() const { return framebufferResized; }

    /**
     * @brief Reset the resize flag
     */
    void resetResizeFlag() { framebufferResized = false; }

    /**
     * @brief Set window event callbacks
     *
     * @param callbacks Callback functions
     */
    void setCallbacks(const WindowCallbacks& callbacks);

    /**
     * @brief Set window title
     *
     * @param title New window title
     */
    void setTitle(const std::string& title);

    /**
     * @brief Show the window
     */
    void show();

    /**
     * @brief Hide the window
     */
    void hide();

    /**
     * @brief Check if window is visible
     *
     * @return true if window is visible, false otherwise
     */
    bool isVisible() const;

private:
    GLFWwindow* window;
    int width;
    int height;
    std::string title;
    bool initialized;
    mutable bool framebufferResized;

    // Callbacks
    WindowCallbacks callbacks;

    /**
     * @brief Setup GLFW window hints based on configuration
     */
    void setupWindowHints() const;

    /**
     * @brief Create the GLFW window
     *
     * @throws VulkanException if window creation fails
     */
    void createWindow();

    /**
     * @brief Setup GLFW callbacks
     */
    void setupCallbacks();

    // Static callback functions (forward to instance methods)
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    static void windowCloseCallback(GLFWwindow* window);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);

    // Instance callback handlers
    void handleFramebufferResize(int width, int height);
    void handleWindowClose();
    void handleKey(int key, int action);
    void handleMouseMove(double x, double y);
    void handleMouseButton(int button, int action);

    /**
     * @brief Get the WindowManager instance from GLFW window user pointer
     *
     * @param window GLFW window
     * @return WindowManager* Associated WindowManager instance
     */
    static WindowManager* getInstance(GLFWwindow* window);
};

} // namespace RYRayTracing