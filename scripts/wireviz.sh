#!/usr/bin/env bash
set -euo pipefail

SRC="${1:-circuit/top.yml}"
OUT_DIR="circuit"
FORMAT="ph"   # h=html, p=png, s=svg

if ! command -v dot &>/dev/null; then
  echo "No graphviz. brew install graphviz" >&2
  exit 1
fi

[[ -f "$SRC" ]] || { echo "$SRC 없음" >&2; exit 1; }

mkdir -p "$OUT_DIR"
uv run wireviz -f "$FORMAT" -o "$OUT_DIR" "$SRC"

echo "→ $OUT_DIR/$(basename "${SRC%.*}").{html,png,svg}"
