cmake_minimum_required(VERSION 3.28)

project(death_counter
    VERSION   "0.1.0"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(FATAL_ERROR
        "death_counter must target Windows. "
        "Use: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-w64-mingw32.cmake (cross-compile) "
        "or a native MSVC generator.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9b
    GIT_SHALLOW    TRUE)

FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.4
    GIT_SHALLOW    TRUE)

FetchContent_Declare(inih
    GIT_REPOSITORY https://github.com/benhoyt/inih.git
    GIT_TAG        r62
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(imgui minhook inih)

add_library(inih_impl STATIC ${inih_SOURCE_DIR}/ini.c)
target_include_directories(inih_impl PUBLIC ${inih_SOURCE_DIR})

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)

add_library(death_counter SHARED
    src/dllmain.cpp
    src/renderer.cpp
    src/config.cpp)

target_include_directories(death_counter PRIVATE
    src
    ${CMAKE_SOURCE_DIR})

target_compile_definitions(death_counter PRIVATE
    _WIN32_WINNT=0x0601
    WIN32
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE)

set(MSVC_OR_CLANG_CL $<OR:$<CXX_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:Clang>>)

target_compile_options(death_counter PRIVATE
    $<$<NOT:${MSVC_OR_CLANG_CL}>:-Wall;-Wextra;-Wpedantic;-Werror;-Wno-pedantic-missing-modules-cond>
    $<${MSVC_OR_CLANG_CL}:/W4;/WX>)

target_link_libraries(death_counter PRIVATE
    imgui
    minhook
    inih_impl
    kernel32
    d3d12
    d3dcompiler
    dxgi
    dwmapi
    imm32
    user32)

target_link_options(death_counter PRIVATE
    $<$<NOT:${MSVC_OR_CLANG_CL}>:-static-libstdc++;-static-libgcc;-static>)

set_target_properties(death_counter PROPERTIES
    OUTPUT_NAME "death_counter"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
