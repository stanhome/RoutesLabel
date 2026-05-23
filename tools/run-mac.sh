#!/usr/bin/env bash
# tools/run-mac.sh
# 在 macOS 上运行 routes_label：
#   - 通过 brew 安装的 MoltenVK ICD 让 Vulkan loader 找到 Metal 驱动
#   - 通过 brew 安装的 vulkan-validationlayers 让 VK_LAYER_KHRONOS_validation 可加载
#   - 通过 DYLD_LIBRARY_PATH 让 dyld 找到 layer 引用的 dylib
#
# 用法:
#   tools/run-mac.sh                               # 跑默认产物 build/bin/routes_label
#   tools/run-mac.sh path/to/routes_label [args]   # 自定义路径 / 透传命令行参数

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BIN="${1:-$PROJECT_ROOT/build/bin/routes_label}"
shift || true

if [[ "$(uname)" != "Darwin" ]]; then
    echo "[run-mac.sh] 警告: 当前不是 macOS（uname=$(uname)）。"
    echo "[run-mac.sh] 如需在 Linux 上运行，请改用 tools/run-linux.sh。"
    exit 1
fi

BREW_PREFIX="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"

# MoltenVK ICD（告诉 Vulkan loader 哪个驱动可用）
MOLTEN_ICD="$BREW_PREFIX/etc/vulkan/icd.d/MoltenVK_icd.json"
if [[ -f "$MOLTEN_ICD" ]]; then
    export VK_DRIVER_FILES="$MOLTEN_ICD"
    export VK_ICD_FILENAMES="$MOLTEN_ICD"  # 老版本 loader 兼容
else
    echo "[run-mac.sh] 警告: 未找到 MoltenVK ICD: $MOLTEN_ICD"
    echo "[run-mac.sh]       请执行: brew install molten-vk"
fi

# Validation layer 路径（layer json + dylib）
LAYER_JSON_DIR="$BREW_PREFIX/share/vulkan/explicit_layer.d"
if [[ -d "$LAYER_JSON_DIR" ]]; then
    export VK_LAYER_PATH="$LAYER_JSON_DIR"
fi

# Layer dylib 用相对名加载 → 让 dyld 能搜索到 brew 的 lib
export DYLD_LIBRARY_PATH="$BREW_PREFIX/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"

echo "[run-mac.sh] VK_DRIVER_FILES=$VK_DRIVER_FILES"
echo "[run-mac.sh] VK_LAYER_PATH=$VK_LAYER_PATH"
echo "[run-mac.sh] DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH"
echo "[run-mac.sh] exec: $BIN $*"
exec "$BIN" "$@"
