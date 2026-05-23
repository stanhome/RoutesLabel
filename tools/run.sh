#!/usr/bin/env bash
# tools/run.sh
# 在 macOS 上运行 routes_label，自动设置 Vulkan loader / MoltenVK ICD / Validation layer 的环境变量。
# Linux 系统通常无需此脚本（系统包路径标准）。

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BIN="${1:-$PROJECT_ROOT/build/bin/routes_label}"
shift || true

if [[ "$(uname)" == "Darwin" ]]; then
    BREW_PREFIX="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"

    # MoltenVK ICD（告诉 Vulkan loader 哪个驱动可用）
    MOLTEN_ICD="$BREW_PREFIX/etc/vulkan/icd.d/MoltenVK_icd.json"
    if [[ -f "$MOLTEN_ICD" ]]; then
        export VK_DRIVER_FILES="$MOLTEN_ICD"
        export VK_ICD_FILENAMES="$MOLTEN_ICD"  # 老版本 loader 兼容
    fi

    # Validation layer 路径（layer json + dylib）
    LAYER_JSON_DIR="$BREW_PREFIX/share/vulkan/explicit_layer.d"
    if [[ -d "$LAYER_JSON_DIR" ]]; then
        export VK_LAYER_PATH="$LAYER_JSON_DIR"
    fi

    # Layer dylib 用相对名加载 → 让 dyld 能搜索到 brew 的 lib
    export DYLD_LIBRARY_PATH="$BREW_PREFIX/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"

    echo "[run.sh] VK_DRIVER_FILES=$VK_DRIVER_FILES"
    echo "[run.sh] VK_LAYER_PATH=$VK_LAYER_PATH"
    echo "[run.sh] DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH"
fi

echo "[run.sh] exec: $BIN $*"
exec "$BIN" "$@"
