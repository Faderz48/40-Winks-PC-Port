#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "Initialize the Git repository before running the public-tree audit." >&2
    exit 1
fi

mapfile -t tracked_files < <(git ls-files)
if [ "${#tracked_files[@]}" -eq 0 ]; then
    echo "No tracked files were found." >&2
    exit 1
fi

failed=0
for path in "${tracked_files[@]}"; do
    case "$path" in
        analysis/*|build/*|dist/*|work/*|pc-port/*|recomp/generated/*|recomp/baserom*|\
        *.z64|*.n64|*.v64|*.rom|*.ips|*.AppImage|*.elf|*.o|*.a|*.so|*.dll|*.exe|\
        *.png|*.jpg|*.jpeg|*.webp)
            echo "Blocked public path: $path" >&2
            failed=1
            continue
            ;;
    esac

    if [ -f "$path" ]; then
        size="$(stat -c %s "$path")"
        if [ "$size" -gt 5242880 ]; then
            echo "Tracked file exceeds 5 MiB: $path" >&2
            failed=1
        fi
        if ! grep -Iq . "$path"; then
            echo "Unexpected binary file: $path" >&2
            failed=1
        fi
    fi
done

if git grep -n -I -E '/home/[A-Za-z0-9._-]+/|gh[pousr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,}|BEGIN (RSA|OPENSSH|EC) PRIVATE KEY' -- .; then
    echo "Machine-specific path, credential, or private key found." >&2
    failed=1
fi

if [ "$failed" -ne 0 ]; then
    exit 1
fi

echo "Public-tree audit passed for ${#tracked_files[@]} tracked files."
