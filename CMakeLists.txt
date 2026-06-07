cmake_minimum_required(VERSION 3.20)
project(ComputerGraphicsLecture LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ARCH x64)
if(WIN32)
    set(PLATFORM "Windows")
    add_definitions(-DNOMINMAX -DVK_USE_PLATFORM_WIN32_KHR)
elseif(APPLE)
    set(PLATFORM "macOS")
    add_definitions(-DVK_USE_PLATFORM_MACOS_MVK)
else()
    set(PLATFORM "Linux")
    add_definitions(-DVK_USE_PLATFORM_XLIB_KHR)
endif()

list(APPEND CMAKE_PREFIX_PATH "$ENV{VULKAN_SDK}")

if(DEFINED ENV{VULKAN_SDK})
    file(TO_CMAKE_PATH "$ENV{VULKAN_SDK}" VULKAN_SDK)
    set(ENV{VULKAN_SDK} "${VULKAN_SDK}")
endif()
find_package(Vulkan REQUIRED)
find_package(slang CONFIG HINTS "$ENV{VULKAN_SDK}/lib/cmake")

if(DEFINED ENV{VULKAN_SDK})
    find_package(slang CONFIG HINTS "$ENV{VULKAN_SDK}/lib/cmake")
endif()

set(SLANGC_EXECUTABLE "${VULKAN_SDK}/Bin/slangc.exe")
message("slangc executable at ${SLANGC_EXECUTABLE}")

# Add subdirectories for different executables
add_subdirectory(imgui)
add_subdirectory(BlinnPhong)
add_subdirectory(RayTracing)
add_subdirectory(VkFromScratch)