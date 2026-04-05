#include "WindowManager.hpp"
#include "core/Exception.hpp"
#include <stdexcept>
#include <GLFW/glfw3.h>

namespace RYRayTracing {

WindowManager::WindowManager(const WindowConfig& config)
    : window(nullptr)
    , width(config.width)
    , height(config.height)
    , title(config.title)
    , initialized(false)
    , framebufferResized(false) {

    LOG_DEBUG("WindowManager created with config: " +
              std::to_string(width) + "x" + std::to_string(height) +
              ", title: " + title);
}

WindowManager::~WindowManager() {
    if (window) {
        LOG_DEBUG("Destroying window: " + title);
        glfwDestroyWindow(window);
        window = nullptr;
    }

    // Note: We don't call glfwTerminate() here because there might be
    // other windows. GLFW termination should be handled at application level.
}

WindowManager::WindowManager(WindowManager&& other) noexcept
    : window(other.window)
    , width(other.width)
    , height(other.height)
    , title(std::move(other.title))
    , initialized(other.initialized)
    , framebufferResized(other.framebufferResized)
    , callbacks(std::move(other.callbacks)) {
    other.window = nullptr;
    other.initialized = false;
}

WindowManager& WindowManager::operator=(WindowManager&& other) noexcept {
    if (this != &other) {
        if (window) {
            glfwDestroyWindow(window);
        }

        window = other.window;
        width = other.width;
        height = other.height;
        title = std::move(other.title);
        initialized = other.initialized;
        framebufferResized = other.framebufferResized;
        callbacks = std::move(other.callbacks);

        other.window = nullptr;
        other.initialized = false;
    }
    return *this;
}

void WindowManager::init() {
    if (initialized) {
        LOG_WARNING("WindowManager already initialized");
        return;
    }

    LOG_INFO("Initializing GLFW");

    // Initialize GLFW
    if (!glfwInit()) {
        throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                            "Failed to initialize GLFW",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // GLFW doesn't create OpenGL context for Vulkan
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // Setup window hints
    setupWindowHints();

    // Create window
    createWindow();

    // Setup callbacks
    setupCallbacks();

    // Show the window
    glfwShowWindow(window);

    initialized = true;
    LOG_INFO("Window created successfully: " + title);
}

bool WindowManager::shouldClose() const {
    if (!window) {
        return true;
    }
    return glfwWindowShouldClose(window);
}

void WindowManager::pollEvents() {
    if (!initialized) {
        return;
    }
    glfwPollEvents();
}

std::pair<int, int> WindowManager::getFramebufferSize() const {
    if (!window) {
        return {0, 0};
    }

    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    return {fbWidth, fbHeight};
}

VkSurfaceKHR WindowManager::createSurface(VkInstance instance) const {
    if (!window) {
        throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                            "Cannot create surface: window not created",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    VkSurfaceKHR surface;
    VkResult result = glfwCreateWindowSurface(instance, window, nullptr, &surface);

    if (result != VK_SUCCESS) {
        throw VulkanException(result,
                            "Failed to create window surface",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    LOG_INFO("Vulkan surface created successfully");
    return surface;
}

void WindowManager::setCallbacks(const WindowCallbacks& callbacks) {
    this->callbacks = callbacks;
}

void WindowManager::setTitle(const std::string& title) {
    this->title = title;
    if (window) {
        glfwSetWindowTitle(window, title.c_str());
    }
}

void WindowManager::show() {
    if (window) {
        glfwShowWindow(window);
    }
}

void WindowManager::hide() {
    if (window) {
        glfwHideWindow(window);
    }
}

bool WindowManager::isVisible() const {
    if (!window) {
        return false;
    }
    return glfwGetWindowAttrib(window, GLFW_VISIBLE) != 0;
}

void WindowManager::setupWindowHints() const {
    // Basic window hints
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_TRUE);
    glfwWindowHint(GLFW_CENTER_CURSOR, GLFW_TRUE);

    // OpenGL context hints (not used for Vulkan but set anyway)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 0);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);

    LOG_DEBUG("GLFW window hints configured");
}

void WindowManager::createWindow() {
    LOG_DEBUG("Creating GLFW window: " + title);

    window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);

    if (!window) {
        throw VulkanException(VK_ERROR_INITIALIZATION_FAILED,
                            "Failed to create GLFW window",
                            __FUNCTION__, __FILE__, __LINE__);
    }

    // Store this instance in GLFW window user pointer
    glfwSetWindowUserPointer(window, this);

    LOG_DEBUG("GLFW window created successfully");
}

void WindowManager::setupCallbacks() {
    if (!window) {
        return;
    }

    // Framebuffer resize callback
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

    // Window close callback
    glfwSetWindowCloseCallback(window, windowCloseCallback);

    // Input callbacks
    glfwSetKeyCallback(window, keyCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);

    LOG_DEBUG("GLFW callbacks configured");
}

// Static callback functions
void WindowManager::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto instance = getInstance(window);
    if (instance) {
        instance->handleFramebufferResize(width, height);
    }
}

void WindowManager::windowCloseCallback(GLFWwindow* window) {
    auto instance = getInstance(window);
    if (instance) {
        instance->handleWindowClose();
    }
}

void WindowManager::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto instance = getInstance(window);
    if (instance) {
        instance->handleKey(key, action);
    }
}

void WindowManager::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    auto instance = getInstance(window);
    if (instance) {
        instance->handleMouseMove(xpos, ypos);
    }
}

void WindowManager::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    auto instance = getInstance(window);
    if (instance) {
        instance->handleMouseButton(button, action);
    }
}

// Instance callback handlers
void WindowManager::handleFramebufferResize(int width, int height) {
    LOG_DEBUG("Framebuffer resized: " + std::to_string(width) + "x" + std::to_string(height));
    framebufferResized = true;

    // Update internal size
    this->width = width;
    this->height = height;

    // Call user callback if set
    if (callbacks.onResize) {
        callbacks.onResize(width, height);
    }
}

void WindowManager::handleWindowClose() {
    LOG_DEBUG("Window close requested");

    // Call user callback if set
    if (callbacks.onClose) {
        callbacks.onClose();
    }
}

void WindowManager::handleKey(int key, int action) {
    // Call user callback if set
    if (callbacks.onKey) {
        callbacks.onKey(key, action);
    }
}

void WindowManager::handleMouseMove(double x, double y) {
    // Call user callback if set
    if (callbacks.onMouseMove) {
        callbacks.onMouseMove(x, y);
    }
}

void WindowManager::handleMouseButton(int button, int action) {
    // Call user callback if set
    if (callbacks.onMouseButton) {
        callbacks.onMouseButton(button, action);
    }
}

WindowManager* WindowManager::getInstance(GLFWwindow* window) {
    return static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
}

} // namespace RYRayTracing