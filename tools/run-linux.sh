#!/usr/bin/env bash
# tools/run-linux.sh
# 在 Linux 上运行 routes_label。
#
# 大多数发行版（Ubuntu/Debian/Fedora/Arch）只要装好系统包，
# 直接 ./build/bin/routes_label 就能跑：
#   Ubuntu/Debian:  sudo apt install vulkan-tools libvulkan-dev vulkan-validationlayers \
#                                    glslang-tools mesa-vulkan-drivers
#   Fedora:         sudo dnf install vulkan-tools vulkan-loader-devel vulkan-validation-layers \
#                                    glslang mesa-vulkan-drivers
#   Arch:           sudo pacman -S vulkan-tools vulkan-icd-loader vulkan-validation-layers \
#                                  glslang vulkan-radeon vulkan-intel
#
# 这个脚本主要解决两个非默认场景:
#   1) 使用 LunarG Vulkan SDK（setup-env.sh）—— 自动 source $VULKAN_SDK/setup-env.sh
#   2) 强制开启 validation layer —— 设置 VK_INSTANCE_LAYERS / VK_LOADER_LAYERS_ENABLE
#
# 用法:
#   tools/run-linux.sh                                  # 跑默认产物 build/bin/routes_label
#   tools/run-linux.sh path/to/routes_label [args]      # 自定义路径 / 透传命令行参数
#
# 环境变量开关:
#   ROUTES_VALIDATION=1   强制启用 VK_LAYER_KHRONOS_validation
#   ROUTES_API_DUMP=1     额外启用 VK_LAYER_LUNARG_api_dump（调试用，输出非常多）
#   VULKAN_SDK=...        若指向 LunarG SDK 解压目录，会自动 source setup-env.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BIN="${1:-$PROJECT_ROOT/build/bin/routes_label}"
shift || true

if [[ "$(uname)" != "Linux" ]]; then
    echo "[run-linux.sh] 警告: 当前不是 Linux（uname=$(uname)）。"
    echo "[run-linux.sh] 如需在 macOS 上运行，请改用 tools/run-mac.sh。"
    exit 1
fi

# -----------------------------------------------------------------------------
# 1) LunarG Vulkan SDK 集成
# -----------------------------------------------------------------------------
# 如果显式设了 VULKAN_SDK 且其下有 setup-env.sh，则 source 之，让 VK_LAYER_PATH /
# LD_LIBRARY_PATH / PATH 等指向 SDK；这样 SDK 自带的 validation layer 优先生效。
if [[ -n "${VULKAN_SDK:-}" && -f "$VULKAN_SDK/setup-env.sh" ]]; then
    # shellcheck disable=SC1090,SC1091
    source "$VULKAN_SDK/setup-env.sh"
    echo "[run-linux.sh] sourced LunarG SDK: $VULKAN_SDK"
fi

# -----------------------------------------------------------------------------
# 2) Validation / API dump 开关
# -----------------------------------------------------------------------------
LAYERS=()
if [[ "${ROUTES_VALIDATION:-0}" == "1" ]]; then
    LAYERS+=("VK_LAYER_KHRONOS_validation")
fi
if [[ "${ROUTES_API_DUMP:-0}" == "1" ]]; then
    LAYERS+=("VK_LAYER_LUNARG_api_dump")
fi
if [[ ${#LAYERS[@]} -gt 0 ]]; then
    # ":" 分隔，是 Vulkan loader 在 Linux 上的标准约定
    LAYERS_JOINED="$(IFS=:; echo "${LAYERS[*]}")"
    export VK_INSTANCE_LAYERS="$LAYERS_JOINED"
    # Vulkan loader >= 1.3.234 推荐使用以下变量；老版本读 VK_INSTANCE_LAYERS 也兼容
    export VK_LOADER_LAYERS_ENABLE="$LAYERS_JOINED"
    echo "[run-linux.sh] enable layers: $LAYERS_JOINED"
fi

# -----------------------------------------------------------------------------
# 3) 打印诊断信息（仅当对应变量已设置时才打印，避免空行干扰）
# -----------------------------------------------------------------------------
[[ -n "${VK_LAYER_PATH:-}"        ]] && echo "[run-linux.sh] VK_LAYER_PATH=$VK_LAYER_PATH"
[[ -n "${VK_ICD_FILENAMES:-}"     ]] && echo "[run-linux.sh] VK_ICD_FILENAMES=$VK_ICD_FILENAMES"
[[ -n "${VK_DRIVER_FILES:-}"      ]] && echo "[run-linux.sh] VK_DRIVER_FILES=$VK_DRIVER_FILES"
[[ -n "${LD_LIBRARY_PATH:-}"      ]] && echo "[run-linux.sh] LD_LIBRARY_PATH=$LD_LIBRARY_PATH"

if [[ ! -x "$BIN" ]]; then
    echo "[run-linux.sh] 错误: 找不到可执行文件: $BIN"
    echo "[run-linux.sh]       请先构建: cmake -S . -B build && cmake --build build -j"
    exit 1
fi

echo "[run-linux.sh] exec: $BIN $*"
exec "$BIN" "$@"
