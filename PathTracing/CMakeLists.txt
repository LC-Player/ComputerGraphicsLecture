# PathTracing executable
add_executable(PathTracing)

# Core module sources
target_sources(PathTracing PRIVATE
        src/core/Exception.cpp
        src/core/Logger.cpp
)

# Utils module sources
target_sources(PathTracing PRIVATE
        src/utils/FileUtil.cpp
)

# Window module sources
target_sources(PathTracing PRIVATE
        src/window/WindowManager.cpp
)

# Vulkan module sources
target_sources(PathTracing PRIVATE
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
        src/vulkan/AccelerationStructure.cpp
)

# Main application sources
target_sources(PathTracing PRIVATE
        src/main.cpp
        src/Application.cpp
        src/Camera.cpp
        src/imgui_impl_glfw.cpp
        src/imgui_impl_vulkan.cpp
        src/imgui_impl_glfw.h
        src/imgui_impl_vulkan.h
        src/tiny_obj_loader_impl.cpp
        src/tinyxml2.cpp
        src/SceneConfig.cpp
        src/EditorUI.cpp
)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)
find_package(Stb REQUIRED)
# Set include directories (include all module directories)
target_include_directories(PathTracing PRIVATE
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
target_link_libraries(PathTracing PRIVATE
        glfw
        glm::glm
        ImGui
        Vulkan::Vulkan
        tinyobjloader::tinyobjloader
)

set_target_properties(PathTracing PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
)

# Validation layers configuration
option(ENABLE_VALIDATION_LAYERS "Enable Vulkan validation layers for debugging" ON)

if(ENABLE_VALIDATION_LAYERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Vulkan validation layers enabled for Debug build")
    target_compile_definitions(PathTracing PRIVATE
        VULKAN_DEBUG=1
        ENABLE_VALIDATION_LAYERS=1
    )
else()
    message(STATUS "Vulkan validation layers disabled")
    target_compile_definitions(PathTracing PRIVATE
        VULKAN_DEBUG=0
        ENABLE_VALIDATION_LAYERS=0
    )
endif()

target_compile_definitions(PathTracing PUBLIC
    $<$<CONFIG:Debug>:RT_DEBUG>
    $<$<CONFIG:RelWithDebInfo>:RT_REL_WITH_DEB_INFO>
    $<$<CONFIG:Release>:RT_RELEASE>
)

if(WIN32)
    target_compile_definitions(PathTracing PUBLIC
            RT_PLATFORM_WINDOWS
            VK_USE_PLATFORM_WIN32_KHR
            GLFW_INCLUDE_VULKAN
    )
endif()
if (MSVC)
    target_compile_options(PathTracing PRIVATE /utf-8)
endif()

# Copy assets to build directory
add_custom_target(PathTracingAssets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:PathTracing>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:PathTracing>/assets
)

add_dependencies(PathTracing PathTracingAssets)

find_program(SLANGC_EXECUTABLE slangc HINTS "$ENV{VULKAN_SDK}/Bin")
if (NOT EXISTS ${SLANGC_EXECUTABLE})
    message(FATAL_ERROR "slangc not found at ${SLANGC_EXECUTABLE}")
endif()

set(SHADERS_COMPILE_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

add_custom_target(PathTracing_CopyShadersToFinal ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:PathTracing>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SHADERS_COMPILE_DIR}
        $<TARGET_FILE_DIR:PathTracing>/shaders
    COMMENT "Copying compiled shaders to final directory"
    DEPENDS PathTracing_ShaderAssetsCompiling PathTracing_RTShaders
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
list(FILTER SHADER_SLANG_SOURCES EXCLUDE REGEX "raytracer\\.slang$")
list(FILTER SHADER_SLANG_SOURCES EXCLUDE REGEX "pathtracer")

add_slang_shader_target(PathTracing_ShaderAssetsCompiling SOURCES ${SHADER_SLANG_SOURCES})

# Ray tracing shaders (each with its own entry point)
set(RT_SHADERS_COMPILE_DIR ${SHADERS_COMPILE_DIR})
set(RT_SPV_FILES "")
set(SLANG_BASE_FLAGS -target spirv -profile glsl_450+spirv_1_5 -emit-spirv-directly -fvk-use-entrypoint-name)
set(SLANG_SHADER_INC ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SLANG_COMMON_DEPS ${SLANG_SHADER_INC}/pathtracer_common.slang)

# Ray generation shader
add_custom_command(
    OUTPUT ${RT_SHADERS_COMPILE_DIR}/pathtracer.rgen.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RT_SHADERS_COMPILE_DIR}
    COMMAND ${SLANGC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pathtracer.rgen.slang
            ${SLANG_BASE_FLAGS} -I ${SLANG_SHADER_INC}
            -stage raygeneration -entry rayGen -o pathtracer.rgen.spv
    WORKING_DIRECTORY ${RT_SHADERS_COMPILE_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pathtracer.rgen.slang ${SLANG_COMMON_DEPS}
    COMMENT "Compiling ray generation shader"
)
list(APPEND RT_SPV_FILES ${RT_SHADERS_COMPILE_DIR}/pathtracer.rgen.spv)

# Closest hit shader
add_custom_command(
    OUTPUT ${RT_SHADERS_COMPILE_DIR}/pathtracer.rchit.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RT_SHADERS_COMPILE_DIR}
    COMMAND ${SLANGC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pathtracer.rchit.slang
            ${SLANG_BASE_FLAGS} -I ${SLANG_SHADER_INC}
            -stage closesthit -entry closestHit -o pathtracer.rchit.spv
    WORKING_DIRECTORY ${RT_SHADERS_COMPILE_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pathtracer.rchit.slang ${SLANG_COMMON_DEPS}
    COMMENT "Compiling closest hit shader"
)
list(APPEND RT_SPV_FILES ${RT_SHADERS_COMPILE_DIR}/pathtracer.rchit.spv)

# Miss shader (primary rays)
add_custom_command(
    OUTPUT ${RT_SHADERS_COMPILE_DIR}/pathtracer.rmiss.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RT_SHADERS_COMPILE_DIR}
    COMMAND ${SLANGC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pathtracer.rmiss.slang
            ${SLANG_BASE_FLAGS} -I ${SLANG_SHADER_INC}
            -stage miss -entry miss -o pathtracer.rmiss.spv
    WORKING_DIRECTORY ${RT_SHADERS_COMPILE_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pathtracer.rmiss.slang ${SLANG_COMMON_DEPS}
    COMMENT "Compiling miss shader"
)
list(APPEND RT_SPV_FILES ${RT_SHADERS_COMPILE_DIR}/pathtracer.rmiss.spv)

# Shadow miss shader
add_custom_command(
    OUTPUT ${RT_SHADERS_COMPILE_DIR}/pathtracer.smiss.spv
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RT_SHADERS_COMPILE_DIR}
    COMMAND ${SLANGC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pathtracer.smiss.slang
            ${SLANG_BASE_FLAGS} -I ${SLANG_SHADER_INC}
            -stage miss -entry shadowMiss -o pathtracer.smiss.spv
    WORKING_DIRECTORY ${RT_SHADERS_COMPILE_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pathtracer.smiss.slang ${SLANG_COMMON_DEPS}
    COMMENT "Compiling shadow miss shader"
)
list(APPEND RT_SPV_FILES ${RT_SHADERS_COMPILE_DIR}/pathtracer.smiss.spv)

add_custom_target(PathTracing_RTShaders DEPENDS ${RT_SPV_FILES})

add_dependencies(PathTracing_CopyShadersToFinal PathTracing_ShaderAssetsCompiling PathTracing_RTShaders)
add_dependencies(PathTracing PathTracing_CopyShadersToFinal)
