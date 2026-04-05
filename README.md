# Vulkan Triangle Rendering Project

This project demonstrates a simple Vulkan application that renders a colored triangle using modern C++ and a modular architecture.

## Project Structure

```
src/
├── core/                    # Core utilities
│   ├── Exception.hpp/cpp   # Vulkan exception handling
│   ├── Logger.hpp/cpp      # Logging system
│   └── VulkanMacros.hpp    # Vulkan helper macros
├── utils/                  # Utility functions
│   └── FileUtil.hpp/cpp    # File I/O utilities
├── window/                 # Window management
│   └── WindowManager.hpp/cpp # GLFW window wrapper
├── vulkan/                 # Vulkan core components
│   ├── Validation.hpp/cpp  # Validation layer management
│   ├── VulkanInstance.hpp/cpp # Vulkan instance
│   ├── VulkanDevice.hpp/cpp   # Logical device
│   ├── Buffer.hpp/cpp      # Buffer management
│   ├── ShaderModule.hpp/cpp # Shader loading
│   └── SwapChainManager.hpp/cpp # Swap chain (partial)
├── Application.hpp/cpp     # Main application class
└── main.cpp               # Application entry point

assets/shaders/            # Shader files
├── triangle.vert         # Vertex shader source
├── triangle.frag         # Fragment shader source
├── triangle.vert.spv     # Compiled vertex shader
└── triangle.frag.spv     # Compiled fragment shader
```

## Building the Project

### Prerequisites
- CMake 3.20 or higher
- C++20 compatible compiler
- Vulkan SDK
- GLFW3

### Build Steps

1. **Configure with CMake:**
   ```bash
   mkdir build
   cd build
   cmake ..
   ```

2. **Build the project:**
   ```bash
   cmake --build .
   ```

3. **Run the application:**
   ```bash
   ./RayTracing
   ```

### Windows-specific Notes
- Ensure Vulkan SDK is installed and `VULKAN_SDK` environment variable is set
- GLFW3 is managed via vcpkg (already configured in CMakeLists.txt)

## Features Implemented

### Core Infrastructure
- **Logging System**: Multi-level logging with file and console output
- **Exception Handling**: Custom Vulkan exception class with detailed error information
- **Vulkan Macros**: Helper macros for Vulkan error checking

### Window Management
- GLFW-based window creation
- Event handling (resize, key, mouse)
- Vulkan surface creation

### Vulkan Core
- Instance creation with validation layers
- Physical device selection
- Logical device creation
- Buffer management (vertex buffers)
- Shader module loading from SPIR-V

### Application Logic
- Modular application architecture
- Vertex buffer with colored triangle data
- Shader loading and pipeline setup (simplified)
- Main event loop

## Current Status

The project successfully:
- Creates a window using GLFW
- Initializes Vulkan with validation layers
- Creates a logical device
- Loads shaders from SPIR-V files
- Creates a vertex buffer with triangle data
- Runs a main event loop

**Note**: The graphics pipeline creation is simplified in the current implementation. A full pipeline setup with render passes, framebuffers, and command buffers would be needed for actual rendering.

## Architecture Design

The project follows a modular design:
1. **Core Module**: Logging, exceptions, and utilities
2. **Window Module**: Platform-agnostic window management
3. **Vulkan Module**: Vulkan object wrappers with RAII semantics
4. **Application Layer**: High-level application logic

This separation allows for easy testing, maintenance, and future extensions.

## Future Enhancements

1. Complete graphics pipeline setup
2. Swap chain management
3. Command buffer recording and submission
4. Synchronization objects (semaphores, fences)
5. Full triangle rendering
6. Texture support
7. 3D model loading
8. Camera system
9. Lighting and materials