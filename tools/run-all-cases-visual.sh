#!/usr/bin/env bash
# Print commands for every case. Optional: ./tools/run-all-cases-visual.sh --run <stem>
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CASES_DIR="${ROUTES_CASES_DIR:-$ROOT/assets/cases}"

if [[ ! -d "$CASES_DIR" ]]; then
  echo "Cases dir not found: $CASES_DIR" >&2
  exit 1
fi

stems=()
while IFS= read -r f; do
  stems+=("$(basename "$f" .json)")
done < <(find "$CASES_DIR" -maxdepth 1 -name '*.json' \( -type f -o -type l \) | sort)

echo "Test cases (${#stems[@]}):"
for s in "${stems[@]}"; do
  echo ""
  echo "# $s"
  echo "./tools/run-cases.sh -v $s"
  echo "./tools/run-case-visual.sh $s"
done
echo ""
echo "All headless: ./tools/run-cases.sh -v"

if [[ "${1:-}" == "--run" && -n "${2:-}" ]]; then
  exec "$SCRIPT_DIR/run-case-visual.sh" "$2"
fi
