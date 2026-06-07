# BlinnPhong executable
add_executable(BlinnPhong)

# Core module sources
target_sources(BlinnPhong PRIVATE
        src/core/Exception.cpp
        src/core/Logger.cpp
)

# Utils module sources
target_sources(BlinnPhong PRIVATE
        src/utils/FileUtil.cpp
)

# Window module sources
target_sources(BlinnPhong PRIVATE
        src/window/WindowManager.cpp
)

# Vulkan module sources
target_sources(BlinnPhong PRIVATE
        src/vulkan/Validation.cpp
        src/vulkan/VulkanInstance.cpp
        src/vulkan/VulkanDevice.cpp
        src/vulkan/Buffer.cpp
        src/vulkan/ShaderModule.cpp
        src/vulkan/SwapChainManager.cpp
        src/vulkan/RenderPassManager.cpp
        src/vulkan/PipelineManager.cpp
        src/vulkan/CommandManager.cpp
        src/vulkan/Texture.cpp
        src/vulkan/stb_image.cpp
)

# Main application sources
target_sources(BlinnPhong PRIVATE
        src/main.cpp
        src/Application.cpp
        src/Model.cpp
        src/Instance.cpp
        src/Camera.cpp
        src/imgui_impl_glfw.cpp
        src/imgui_impl_vulkan.cpp
        src/imgui_impl_glfw.h
        src/imgui_impl_vulkan.h
        src/tiny_obj_loader_impl.cpp
)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)
find_package(Stb REQUIRED)

# Set include directories (include all module directories)
target_include_directories(BlinnPhong PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
        ${CMAKE_CURRENT_SOURCE_DIR}/src/window
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vulkan
        ${Vulkan_INCLUDE_DIRS}
        ${Stb_INCLUDE_DIR}
)

# Link dependencies
target_link_libraries(BlinnPhong PRIVATE
        glfw
        glm::glm
        ImGui
        Vulkan::Vulkan
        tinyobjloader::tinyobjloader
)

set_target_properties(BlinnPhong PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
)

# Validation layers configuration
option(ENABLE_VALIDATION_LAYERS "Enable Vulkan validation layers for debugging" ON)

if(ENABLE_VALIDATION_LAYERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Vulkan validation layers enabled for Debug build")
    target_compile_definitions(BlinnPhong PRIVATE
        VULKAN_DEBUG=1
        ENABLE_VALIDATION_LAYERS=1
    )
else()
    message(STATUS "Vulkan validation layers disabled")
    target_compile_definitions(BlinnPhong PRIVATE
        VULKAN_DEBUG=0
        ENABLE_VALIDATION_LAYERS=0
    )
endif()

target_compile_definitions(BlinnPhong PUBLIC
    $<$<CONFIG:Debug>:BP_DEBUG>
    $<$<CONFIG:RelWithDebInfo>:BP_REL_WITH_DEB_INFO>
    $<$<CONFIG:Release>:BP_RELEASE>
)

if(WIN32)
    target_compile_definitions(BlinnPhong PUBLIC
            BP_PLATFORM_WINDOWS
            VK_USE_PLATFORM_WIN32_KHR
            GLFW_INCLUDE_VULKAN
    )
endif()
if (MSVC)
    target_compile_options(BlinnPhong PRIVATE /utf-8)
endif()

# Copy assets to build directory
add_custom_target(BlinnPhongAssets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:BlinnPhong>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:BlinnPhong>/assets
)

add_dependencies(BlinnPhong BlinnPhongAssets)

find_program(SLANGC_EXECUTABLE slangc HINTS "$ENV{VULKAN_SDK}/Bin")
if (NOT EXISTS ${SLANGC_EXECUTABLE})
    message(FATAL_ERROR "slangc not found at ${SLANGC_EXECUTABLE}")
endif()

set(SHADERS_COMPILE_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

add_custom_target(BlinnPhong_CopyShadersToFinal ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:BlinnPhong>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SHADERS_COMPILE_DIR}
        $<TARGET_FILE_DIR:BlinnPhong>/shaders
    COMMENT "Copying compiled shaders to final directory"
    DEPENDS BlinnPhong_ShaderAssetsCompiling
)

function (add_slang_shader_target TARGET)
    cmake_parse_arguments ("SHADER" "" "" "SOURCES" ${ARGN})
    set(ENTRY_POINTS -entry vertMain -entry fragMain)
    set(SPV_FILES "")

    foreach (SOURCE_FILE ${SHADER_SOURCES})
        get_filename_component(FILE_NAME ${SOURCE_FILE} NAME_WE)
        set(OUTPUT_SPV ${SHADERS_COMPILE_DIR}/${FILE_NAME}.spv)
        list(APPEND SPV_FILES ${OUTPUT_SPV})

        add_custom_command (
            OUTPUT ${OUTPUT_SPV}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_COMPILE_DIR}
            COMMAND ${SLANGC_EXECUTABLE} ${SOURCE_FILE}
                    -target spirv -profile spirv_1_4
                    -emit-spirv-directly -fvk-use-entrypoint-name
                    ${ENTRY_POINTS} -o ${FILE_NAME}.spv
            WORKING_DIRECTORY ${SHADERS_COMPILE_DIR}
            DEPENDS ${SOURCE_FILE}
            COMMENT "Compiling Slang shader ${SOURCE_FILE} -> ${OUTPUT_SPV}"
            VERBATIM
        )
    endforeach()

    add_custom_target (${TARGET} DEPENDS ${SPV_FILES})
endfunction()

file(GLOB SHADER_SLANG_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.slang)

add_slang_shader_target(BlinnPhong_ShaderAssetsCompiling SOURCES ${SHADER_SLANG_SOURCES})

add_dependencies(BlinnPhong_CopyShadersToFinal BlinnPhong_ShaderAssetsCompiling)
add_dependencies(BlinnPhong BlinnPhong_CopyShadersToFinal)
