#!/usr/bin/env bash
# Headless GridCpu regression on assets/cases/*.json
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$ROOT/build"
BIN="$BUILD_DIR/bin/routes_label_cases"
export PATH="/opt/homebrew/bin:$PATH"

if [[ ! -d "$BUILD_DIR" ]] || [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  echo "[run-cases.sh] configuring: cmake -S \"$ROOT\" -B \"$BUILD_DIR\""
  cmake -S "$ROOT" -B "$BUILD_DIR"
fi

if [[ ! -x "$BIN" ]]; then
  echo "[run-cases.sh] building routes_label_cases..."
  cmake --build "$BUILD_DIR" -j --target routes_label_cases
fi

export ROUTES_CASES_DIR="${ROUTES_CASES_DIR:-$ROOT/assets/cases}"
exec "$BIN" --cases-dir "$ROUTES_CASES_DIR" "$@"
