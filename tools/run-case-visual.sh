#!/usr/bin/env bash
# Visualize one case JSON in the GUI (Vulkan demo).
# Usage:
#   tools/run-case-visual.sh <stem>     e.g. trunk-fork-rejoin-crowding
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CASES_DIR="${ROUTES_CASES_DIR:-$ROOT/assets/cases}"
BUILD_DIR="$ROOT/build"
BIN="$BUILD_DIR/bin/routes_label"

if [[ $# -lt 1 ]]; then
  exec "$SCRIPT_DIR/run-all-cases-visual.sh"
fi

CASE="$1"
CASE_JSON="$(cd "$(dirname "$CASES_DIR/$CASE")" && pwd)/$(basename "$CASE").json"
if [[ ! -f "$CASE_JSON" ]]; then
  echo "Case not found: $CASES_DIR/${CASE}.json" >&2
  exit 1
fi

if [[ ! -d "$BUILD_DIR" ]] || [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
  echo "[run-case-visual] configuring: cmake -S \"$ROOT\" -B \"$BUILD_DIR\""
  cmake -S "$ROOT" -B "$BUILD_DIR"
fi

need_build=0
if [[ ! -x "$BIN" ]]; then
  need_build=1
elif ! strings "$BIN" 2>/dev/null | grep -q 'ROUTES_SCENE'; then
  need_build=1
elif [[ "$ROOT/src/renderer/RoutesRenderer.cpp" -nt "$BIN" ]]; then
  need_build=1
fi
if [[ $need_build -eq 1 ]]; then
  echo "[run-case-visual] building routes_label..."
  cmake --build "$BUILD_DIR" -j --target routes_label
fi

ACTIVE="$BUILD_DIR/bin/assets/cases/_active_case.json"
mkdir -p "$(dirname "$ACTIVE")"
cp -f "$CASE_JSON" "$ACTIVE"
export ROUTES_SCENE="$ACTIVE"

export PATH="/opt/homebrew/bin:$PATH"
echo "[run-case-visual] ROUTES_SCENE=$ROUTES_SCENE"
exec "$SCRIPT_DIR/run-mac.sh"
