add_executable(Lab1Triangle)

target_sources(Lab1Triangle PRIVATE main.cpp)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

# Link dependencies
target_link_libraries(Lab1Triangle PRIVATE
        glfw
        glm::glm
        ${Vulkan_LIBRARIES}
)

target_include_directories(Lab1Triangle PRIVATE ${Vulkan_INCLUDE_DIRS})

# Validation layers configuration
option(ENABLE_VALIDATION_LAYERS "Enable Vulkan validation layers for debugging" ON)

if(ENABLE_VALIDATION_LAYERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Vulkan validation layers enabled for Debug build")
    target_compile_definitions(Lab1Triangle PRIVATE
        VULKAN_DEBUG=1
        ENABLE_VALIDATION_LAYERS=1
    )
else()
    message(STATUS "Vulkan validation layers disabled")
    target_compile_definitions(Lab1Triangle PRIVATE
        VULKAN_DEBUG=0
        ENABLE_VALIDATION_LAYERS=0
    )
endif()

if(WIN32)
    target_compile_definitions(RayTracing PUBLIC
            VK_USE_PLATFORM_WIN32_KHR
            GLFW_INCLUDE_VULKAN
    )
endif()
if (MSVC)
    target_compile_options(RayTracing PRIVATE /utf-8)
endif()

find_program(SLANGC_EXECUTABLE slangc HINTS "$ENV{VULKAN_SDK}/Bin")
if (NOT EXISTS ${SLANGC_EXECUTABLE})
    message(FATAL_ERROR "slangc not found at ${SLANGC_EXECUTABLE}")
endif()

# 定义编译输出目录（配置时绝对路径，避免生成器表达式在 OUTPUT 中使用）
set(SHADERS_COMPILE_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

# 自定义目标：将编译好的 shader 拷贝到可执行文件目录
add_custom_target(CopyShadersToFinal ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Lab1Triangle>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SHADERS_COMPILE_DIR}
        $<TARGET_FILE_DIR:Lab1Triangle>/shaders
    COMMENT "Copying compiled shaders to final directory"
    DEPENDS ShaderAssetsCompiling   # 等待编译完成
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
add_slang_shader_target(ShaderAssetsCompiling SOURCES ${SHADER_SLANG_SOURCES})

# 确保可执行文件依赖于最终的拷贝动作，而不是直接依赖编译目标
add_dependencies(CopyShadersToFinal ShaderAssetsCompiling)
add_dependencies(Lab1Triangle CopyShadersToFinal)

# 原有的 assets 拷贝保持不变
add_custom_target(Lab1TriangleAssets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Lab1Triangle>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:Lab1Triangle>/assets
)
add_dependencies(Lab1Triangle Lab1TriangleAssets)