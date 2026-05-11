add_executable(VkFromScratch)

target_sources(VkFromScratch PRIVATE
    main.cpp
    Application.cpp
    Pipeline.cpp
    Resources.cpp
    Rendering.cpp
    Camera.cpp
    imgui_impl_glfw.cpp
    imgui_impl_vulkan.cpp
    stb_image.cpp
)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(tinyobjloader CONFIG REQUIRED)

# Link dependencies
target_link_libraries(VkFromScratch PRIVATE
        glfw
        glm::glm
        ImGui
        tinyobjloader::tinyobjloader
        ${Vulkan_LIBRARIES}
)

target_include_directories(VkFromScratch PRIVATE ${CMAKE_SOURCE_DIR} ${Vulkan_INCLUDE_DIRS})

# Validation layers configuration
option(ENABLE_VALIDATION_LAYERS "Enable Vulkan validation layers for debugging" ON)

if(ENABLE_VALIDATION_LAYERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Vulkan validation layers enabled for Debug build")
    target_compile_definitions(VkFromScratch PRIVATE
        VULKAN_DEBUG=1
        ENABLE_VALIDATION_LAYERS=1
    )
else()
    message(STATUS "Vulkan validation layers disabled")
    target_compile_definitions(VkFromScratch PRIVATE
        VULKAN_DEBUG=0
        ENABLE_VALIDATION_LAYERS=0
    )
endif()

if(WIN32)
    target_compile_definitions(VkFromScratch PUBLIC
            VK_USE_PLATFORM_WIN32_KHR
            GLFW_INCLUDE_VULKAN
    )
endif()
if (MSVC)
    target_compile_options(VkFromScratch PRIVATE /utf-8)
endif()

find_program(SLANGC_EXECUTABLE slangc HINTS "$ENV{VULKAN_SDK}/Bin")
if (NOT EXISTS ${SLANGC_EXECUTABLE})
    message(FATAL_ERROR "slangc not found at ${SLANGC_EXECUTABLE}")
endif()

# 定义编译输出目录（配置时绝对路径，避免生成器表达式在 OUTPUT 中使用）
set(SHADERS_COMPILE_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

# 自定义目标：将编译好的 shader 拷贝到可执行文件目录
add_custom_target(VkFromScratch_CopyShadersToFinal ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:VkFromScratch>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SHADERS_COMPILE_DIR}
        $<TARGET_FILE_DIR:VkFromScratch>/shaders
    COMMENT "Copying compiled shaders to final directory"
    DEPENDS VkFromScratch_ShaderAssetsCompiling   # 等待编译完成
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

# 收集所有 .slang 源文件
file(GLOB SHADER_SLANG_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.slang)

# 编译 shader（输出到 SHADERS_COMPILE_DIR）
add_slang_shader_target(VkFromScratch_ShaderAssetsCompiling SOURCES ${SHADER_SLANG_SOURCES})

# 确保可执行文件依赖于最终的拷贝动作，而不是直接依赖编译目标
add_dependencies(VkFromScratch_CopyShadersToFinal VkFromScratch_ShaderAssetsCompiling)
add_dependencies(VkFromScratch VkFromScratch_CopyShadersToFinal)

# 原有的 assets 拷贝保持不变
add_custom_target(VkFromScratchAssets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:VkFromScratch>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:VkFromScratch>/assets
)
add_dependencies(VkFromScratch VkFromScratchAssets)