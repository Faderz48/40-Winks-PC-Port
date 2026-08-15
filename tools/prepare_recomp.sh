#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXPECTED_SHA256="057232ef7618e25f5645df50d3cd45f08cb5a2cccb3e2fdf48faa8755c4ddb1a"
ROM_PATH="${1:-}"
N64RECOMP="$ROOT_DIR/work/external/N64Recomp/build/N64Recomp"
RSPRECOMP="$ROOT_DIR/work/external/N64Recomp/build/RSPRecomp"

if [ -z "$ROM_PATH" ]; then
    echo "Usage: $0 /path/to/clean-40-winks.z64" >&2
    exit 2
fi

if [ ! -f "$ROM_PATH" ]; then
    echo "ROM not found: $ROM_PATH" >&2
    exit 1
fi

ROM_PATH="$(readlink -f "$ROM_PATH")"
ACTUAL_SHA256="$(sha256sum "$ROM_PATH" | awk '{print $1}')"
if [ "$ACTUAL_SHA256" != "$EXPECTED_SHA256" ]; then
    echo "Unsupported ROM revision." >&2
    echo "Expected SHA-256: $EXPECTED_SHA256" >&2
    echo "Actual SHA-256:   $ACTUAL_SHA256" >&2
    exit 1
fi

if [ ! -x "$N64RECOMP" ]; then
    echo "N64Recomp is not built. Run tools/bootstrap_dependencies.sh first." >&2
    exit 1
fi

if [ ! -x "$RSPRECOMP" ]; then
    echo "RSPRecomp is not built. Run tools/bootstrap_dependencies.sh first." >&2
    exit 1
fi

mkdir -p "$ROOT_DIR/recomp/generated"
ln -sfn "$ROM_PATH" "$ROOT_DIR/recomp/baserom.z64"

cd "$ROOT_DIR"
python3 tools/generate_recomp_symbols.py "$ROM_PATH"

cd "$ROOT_DIR/recomp"
"$N64RECOMP" 40winks.toml
mkdir -p generated/rsp
"$RSPRECOMP" aspMain.toml

echo "Generated local recompilation sources from the verified user ROM."
