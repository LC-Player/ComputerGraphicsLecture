# ImGui/CMakeLists.txt
cmake_minimum_required(VERSION 3.10)

project(ImGui LANGUAGES CXX)

add_library(ImGui STATIC
    imconfig.h
    imgui.h
    imgui.cpp
    imgui_draw.cpp
    imgui_internal.h
    imgui_tables.cpp
    imgui_widgets.cpp
    imstb_rectpack.h
    imstb_textedit.h
    imstb_truetype.h
    imgui_demo.cpp
)

target_include_directories(ImGui PUBLIC .)

set_target_properties(ImGui PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# 禁用警告
target_compile_options(ImGui PRIVATE 
    $<$<CXX_COMPILER_ID:MSVC>:/W0>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
)

# Windows 特定定义
if(WIN32)
    target_compile_definitions(ImGui PRIVATE IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
endif()

# Linux 位置无关代码
if(UNIX AND NOT APPLE)
    set_target_properties(ImGui PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

#alias
add_library(ImGui::ImGui ALIAS ImGui)
