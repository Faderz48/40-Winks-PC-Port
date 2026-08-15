#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXTERNAL_DIR="$ROOT_DIR/work/external"
JOBS="${JOBS:-2}"

if [ -x /usr/bin/git ]; then
    GIT_BIN="${GIT_BIN:-/usr/bin/git}"
else
    GIT_BIN="${GIT_BIN:-git}"
fi

N64RECOMP_URL="https://github.com/N64Recomp/N64Recomp.git"
N64RECOMP_REV="ffb39cdad1da5de07eaaa48bd1db4a89a7986771"
MODERN_RUNTIME_URL="https://github.com/N64Recomp/N64ModernRuntime.git"
MODERN_RUNTIME_REV="ae1ffbb909d9f93c88c41830deb539f7feef5ed2"
RT64_URL="https://github.com/rt64/rt64.git"
RT64_REV="f0728a2520d5aa735886240de3fee75cc805f6d6"

checkout_dependency() {
    local name="$1"
    local url="$2"
    local revision="$3"
    local directory="$EXTERNAL_DIR/$name"

    if [ ! -d "$directory/.git" ]; then
        rm -rf "$directory"
        "$GIT_BIN" clone --filter=blob:none "$url" "$directory"
    fi

    if [ "$("$GIT_BIN" -C "$directory" rev-parse HEAD)" != "$revision" ]; then
        if [ -n "$("$GIT_BIN" -C "$directory" status --porcelain)" ]; then
            echo "$name has local changes and is not at the pinned revision." >&2
            echo "Move those changes aside before bootstrapping." >&2
            exit 1
        fi
        "$GIT_BIN" -C "$directory" fetch --depth 1 origin "$revision"
        "$GIT_BIN" -C "$directory" checkout --detach "$revision"
    fi

    "$GIT_BIN" -C "$directory" submodule update --init --recursive --depth 1
}

apply_patch_once() {
    local name="$1"
    local patch_file="$2"
    local directory="$EXTERNAL_DIR/$name"

    if "$GIT_BIN" -C "$directory" apply --reverse --check "$patch_file" >/dev/null 2>&1; then
        echo "$name compatibility patch is already applied."
    elif "$GIT_BIN" -C "$directory" apply --check "$patch_file"; then
        "$GIT_BIN" -C "$directory" apply "$patch_file"
        echo "Applied $name compatibility patch."
    else
        echo "The $name compatibility patch does not apply to the pinned revision." >&2
        exit 1
    fi
}

mkdir -p "$EXTERNAL_DIR"

checkout_dependency "N64Recomp" "$N64RECOMP_URL" "$N64RECOMP_REV"
checkout_dependency "N64ModernRuntime" "$MODERN_RUNTIME_URL" "$MODERN_RUNTIME_REV"
checkout_dependency "rt64" "$RT64_URL" "$RT64_REV"

apply_patch_once "N64ModernRuntime" "$ROOT_DIR/patches/N64ModernRuntime.patch"
apply_patch_once "rt64" "$ROOT_DIR/patches/RT64.patch"

cmake -S "$EXTERNAL_DIR/N64Recomp" \
    -B "$EXTERNAL_DIR/N64Recomp/build" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$EXTERNAL_DIR/N64Recomp/build" \
    --parallel "$JOBS" \
    --target N64RecompCLI RSPRecomp

echo "Pinned dependencies are ready."
