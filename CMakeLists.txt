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
    src/rhi/CommandPool.cpp
    src/rhi/FrameSync.cpp

    src/renderer/TriangleRenderer.cpp

    src/utils/Log.cpp
    src/utils/FileSystem.cpp
)

target_include_directories(routes_label PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(routes_label PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)

target_compile_options(routes_label PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
        -Wall -Wextra -Wpedantic -Wno-missing-field-initializers
    >
)

target_compile_definitions(routes_label PRIVATE
    $<$<BOOL:${ROUTES_ENABLE_VALIDATION}>:ROUTES_ENABLE_VALIDATION=1>
)

# -----------------------------------------------------------------------------
# Shaders
# -----------------------------------------------------------------------------
routes_compile_shaders(routes_label
    SHADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.vert
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag
    OUTPUT_DIR
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders
)
