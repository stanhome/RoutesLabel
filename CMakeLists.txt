cmake_minimum_required(VERSION 3.20)
project(RoutesLabel
    VERSION 0.1.0
    DESCRIPTION "Cross-platform Vulkan demo for map routes rendering"
    LANGUAGES CXX C
)

# -----------------------------------------------------------------------------
# 全局编译选项
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 默认构建类型 Debug（开 validation layer 方便调试）
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# 把可执行产物统一放到 build/bin，方便运行时定位 shaders/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# -----------------------------------------------------------------------------
# 自定义选项
# -----------------------------------------------------------------------------
option(ROUTES_ENABLE_VALIDATION "Enable Vulkan validation layers" OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(ROUTES_ENABLE_VALIDATION ON CACHE BOOL "" FORCE)
endif()
message(STATUS "[RoutesLabel] Validation layers: ${ROUTES_ENABLE_VALIDATION}")

# Compute shader pipeline 总开关（doc/routes-select-grid-gpu.md §7.1）
# ON  → 编译并链接 GridGpu + 5 个 .comp shader（默认）
# OFF → 仍然编译 GridGpu 源码（保持单一 build target），但运行期 widget 不会切到 GPU
#       —— 通过 ROUTES_LABEL_USE_GPU compile definition 给运行期开关一个提示。
option(ROUTES_LABEL_USE_GPU "Enable Vulkan compute shader pipeline for label placement" ON)
message(STATUS "[RoutesLabel] GPU label pipeline: ${ROUTES_LABEL_USE_GPU}")

# -----------------------------------------------------------------------------
# 依赖
# -----------------------------------------------------------------------------
# Vulkan SDK（系统安装：mac brew / linux apt 或 LunarG SDK）
find_package(Vulkan REQUIRED)
message(STATUS "[RoutesLabel] Vulkan: ${Vulkan_LIBRARY}")
message(STATUS "[RoutesLabel] Vulkan headers: ${Vulkan_INCLUDE_DIR}")

# 优先使用系统/SDK 自带的 glslangValidator 或 glslc 编译 shader
find_program(GLSLANG_VALIDATOR_EXE
    NAMES glslangValidator
    HINTS
        "$ENV{VULKAN_SDK}/bin"
        "$ENV{VULKAN_SDK}/macOS/bin"
)
find_program(GLSLC_EXE
    NAMES glslc
    HINTS
        "$ENV{VULKAN_SDK}/bin"
        "$ENV{VULKAN_SDK}/macOS/bin"
)
if(NOT GLSLANG_VALIDATOR_EXE AND NOT GLSLC_EXE)
    message(FATAL_ERROR
        "Neither glslangValidator nor glslc was found.\n"
        "Install Vulkan SDK or 'brew install glslang shaderc' (macOS) / 'apt install glslang-tools' (Linux)."
    )
endif()
message(STATUS "[RoutesLabel] glslangValidator: ${GLSLANG_VALIDATOR_EXE}")
message(STATUS "[RoutesLabel] glslc: ${GLSLC_EXE}")

# GLFW（git submodule）
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)

# GLM（git submodule，header-only）
add_subdirectory(third_party/glm EXCLUDE_FROM_ALL)

# nlohmann/json（git submodule，header-only）
set(JSON_BuildTests        OFF CACHE INTERNAL "" FORCE)
set(JSON_Install           OFF CACHE INTERNAL "" FORCE)
set(JSON_MultipleHeaders   OFF CACHE INTERNAL "" FORCE)
add_subdirectory(third_party/json EXCLUDE_FROM_ALL)

# Dear ImGui（git submodule，源码以 STATIC 库聚合）
# 仅编译 6 个核心 + 2 个后端 cpp。imgui_demo 用于调试期参考，可保留也可移除。
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(imgui_lib STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
)
# 让 imgui 头文件以 SYSTEM 形式暴露给依赖方，自动屏蔽 -Wall/-Wextra/-Wpedantic
# 引发的 imgui 内部告警，主项目仍保持严格开关。
target_include_directories(imgui_lib SYSTEM PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC
    Vulkan::Vulkan
)
target_link_libraries(imgui_lib PRIVATE
    glfw
)
# imgui 自身源在某些 Clang 版本下可能产生少量告警，关闭以保证主目标 0 warning
target_compile_options(imgui_lib PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
        -w
    >
)

# Shader 编译辅助函数
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileShaders.cmake)

# -----------------------------------------------------------------------------
# 主可执行目标
# -----------------------------------------------------------------------------
add_executable(routes_label
    src/main.cpp

    src/app/Application.cpp

    src/platform/Window.cpp

    src/rhi/VulkanCommon.cpp
    src/rhi/Instance.cpp
    src/rhi/PhysicalDevice.cpp
    src/rhi/Device.cpp
    src/rhi/Swapchain.cpp
    src/rhi/RenderPass.cpp
    src/rhi/Framebuffer.cpp
    src/rhi/ShaderModule.cpp
    src/rhi/GraphicsPipeline.cpp
    src/rhi/ComputePipeline.cpp
    src/rhi/CommandPool.cpp
    src/rhi/FrameSync.cpp
    src/rhi/Buffer.cpp
    src/rhi/Descriptor.cpp

    src/renderer/MapView.cpp
    src/renderer/RouteScene.cpp
    src/renderer/RoutesRenderer.cpp

    src/algo/GridCpu.cpp
    src/algo/GridGpu.cpp

    src/debug/DebugOverlay.cpp
    src/debug/FpsWidget.cpp
    src/debug/GridDebugWidget.cpp
    src/debug/LabelWidget.cpp

    src/utils/Log.cpp
    src/utils/FileSystem.cpp
    src/utils/Json.cpp
)

target_include_directories(routes_label PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(routes_label PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    nlohmann_json::nlohmann_json
    imgui_lib
)

target_compile_options(routes_label PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
        -Wall -Wextra -Wpedantic -Wno-missing-field-initializers
    >
)

target_compile_definitions(routes_label PRIVATE
    $<$<BOOL:${ROUTES_ENABLE_VALIDATION}>:ROUTES_ENABLE_VALIDATION=1>
    $<$<BOOL:${ROUTES_LABEL_USE_GPU}>:ROUTES_LABEL_USE_GPU=1>
)

# -----------------------------------------------------------------------------
# Shaders
# -----------------------------------------------------------------------------
routes_compile_shaders(routes_label
    SHADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/routes.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/routes.frag
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grid_clip.comp
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grid_scan.comp
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grid_pca.comp
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grid_score.comp
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grid_label.comp
    OUTPUT_DIR
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders
)

# -----------------------------------------------------------------------------
# Assets：把 source assets/ 拷贝到 build/bin/assets/，与 shader 同级目录布局
# -----------------------------------------------------------------------------
add_custom_command(TARGET routes_label POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets
    COMMENT "[RoutesLabel] copying assets/ to runtime dir"
    VERBATIM
)
