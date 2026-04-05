# RayTracing executable
add_executable(RayTracing)

# Core module sources
target_sources(RayTracing PRIVATE
        src/core/Exception.cpp
        src/core/Logger.cpp
)

# Utils module sources
target_sources(RayTracing PRIVATE
        src/utils/FileUtil.cpp
)

# Window module sources
target_sources(RayTracing PRIVATE
        src/window/WindowManager.cpp
)

# Vulkan module sources
target_sources(RayTracing PRIVATE
        src/vulkan/Validation.cpp
        src/vulkan/VulkanInstance.cpp
        src/vulkan/VulkanDevice.cpp
        src/vulkan/Buffer.cpp
        src/vulkan/ShaderModule.cpp
        src/vulkan/SwapChainManager.cpp
        src/vulkan/RenderPassManager.cpp
        src/vulkan/PipelineManager.cpp
        src/vulkan/CommandManager.cpp
)

# Renderer module sources
target_sources(RayTracing PRIVATE
        src/renderer/VulkanRenderer.cpp
)

# Main application sources
target_sources(RayTracing PRIVATE
        src/main.cpp
        src/Application.cpp
)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

# Set include directories (include all module directories)
target_include_directories(RayTracing PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
        ${CMAKE_CURRENT_SOURCE_DIR}/src/window
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vulkan
        ${CMAKE_CURRENT_SOURCE_DIR}/src/renderer
        ${Vulkan_INCLUDE_DIRS}
)

# Link dependencies
target_link_libraries(RayTracing PRIVATE
        glfw
        ${Vulkan_LIBRARIES}
)

# Validation layers configuration
option(ENABLE_VALIDATION_LAYERS "Enable Vulkan validation layers for debugging" ON)

if(ENABLE_VALIDATION_LAYERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Vulkan validation layers enabled for Debug build")
    target_compile_definitions(RayTracing PRIVATE
        VULKAN_DEBUG=1
        ENABLE_VALIDATION_LAYERS=1
    )
else()
    message(STATUS "Vulkan validation layers disabled")
    target_compile_definitions(RayTracing PRIVATE
        VULKAN_DEBUG=0
        ENABLE_VALIDATION_LAYERS=0
    )
endif()

target_compile_definitions(RayTracing PUBLIC
    $<$<CONFIG:Debug>:RT_DEBUG>
    $<$<CONFIG:RelWithDebInfo>:RT_REL_WITH_DEB_INFO>
    $<$<CONFIG:Release>:RT_RELEASE>
)

if(WIN32)
    target_compile_definitions(RayTracing PUBLIC
            RT_PLATFORM_WINDOWS
            GLFW_INCLUDE_NONE
    )
    if (RayTracing_LIB_LINK_TYPE STREQUAL "SHARED")
        target_compile_definitions(RayTracing PRIVATE RY_BUILD_DLL)
    endif()
endif()
if (MSVC)
    target_compile_options(RayTracing PRIVATE /utf-8)
endif()

# Copy assets to build directory
add_custom_target(RayTracingAssets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:RayTracing>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:RayTracing>/assets
)

add_dependencies(RayTracing RayTracingAssets)
