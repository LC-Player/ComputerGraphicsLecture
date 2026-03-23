cmake_minimum_required(VERSION 3.20)
project(ComputerGraphicsLecture LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ARCH x64)
if(WIN32)
    set(PLATFORM "Windows")
    add_definitions(-DNOMINMAX)
elseif(APPLE)
    set(PLATFORM "macOS")
else()
    set(PLATFORM "Linux")
endif()

add_executable(RayTracing)

target_sources(RayTracing PRIVATE 
        src/GraphicContext.cpp
        src/main.cpp
)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

target_include_directories(RayTracing PRIVATE
        src
        ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(RayTracing PRIVATE
        glfw
        yaml-cpp::yaml-cpp
        ${Vulkan_LIBRARIES}
)

target_compile_definitions(RayTracing PUBLIC
    $<$<CONFIG:Debug>:RT_DEBUG>
    $<$<CONFIG:RelWithDebInfo>:RT_REL_WITH_DEB_INFO>
    $<$<CONFIG:Release>:RT_RELEASE>
)

target_compile_definitions(RayTracing PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:RY_ENABLE_GPU_DEBUG_OUTPUT=1>
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

add_custom_target(RayTracingAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:RayTracing>/assets
)

add_dependencies(RayTracing RayTracingAssets)