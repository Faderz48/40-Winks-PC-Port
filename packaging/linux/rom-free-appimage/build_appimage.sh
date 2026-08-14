#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD_DIR="$ROOT_DIR/dist/rom-free-appimage"
APPDIR="$BUILD_DIR/AppDir"
TOOLS_DIR="$ROOT_DIR/dist/tools"
APP_ID="io.github.Faderz48.FortyWinksPCPort"
APP_NAME="40-Winks-PC-Port"
VERSION="${VERSION:-0.1.2-alpha}"
BUILD_ID="${BUILD_ID:-0.1.2-alpha}"
SOURCE_REF="${SOURCE_REF:-HEAD}"
APPIMAGETOOL="${APPIMAGETOOL:-}"

cd "$ROOT_DIR"
tools/check_public_tree.sh

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" \
    "$APPDIR/usr/share/40-winks-pc-port/source" \
    "$APPDIR/usr/share/doc/40-winks-pc-port" \
    "$APPDIR/usr/share/applications" \
    "$APPDIR/usr/share/icons/hicolor/scalable/apps" \
    "$APPDIR/usr/share/metainfo" \
    "$TOOLS_DIR"

/usr/bin/git archive "$SOURCE_REF" | tar -x -C "$APPDIR/usr/share/40-winks-pc-port/source"

if find "$APPDIR/usr/share/40-winks-pc-port/source" -type f \
    \( -iname '*.z64' -o -iname '*.n64' -o -iname '*.v64' -o -iname '*.rom' \
       -o -iname '*.ips' -o -iname '*.AppImage' -o -iname 'baserom*' \) \
    -print -quit | grep -q .; then
    echo "Refusing to package a forbidden game-derived or binary file." >&2
    exit 1
fi

install -Dm755 "$ROOT_DIR/packaging/linux/rom-free-appimage/AppRun" "$APPDIR/AppRun"
install -Dm755 "$ROOT_DIR/packaging/linux/rom-free-appimage/forty-winks-port-launcher" \
    "$APPDIR/usr/bin/forty-winks-port-launcher"
install -Dm644 "$ROOT_DIR/packaging/linux/rom-free-appimage/$APP_ID.desktop" \
    "$APPDIR/$APP_ID.desktop"
install -Dm644 "$ROOT_DIR/packaging/linux/rom-free-appimage/$APP_ID.desktop" \
    "$APPDIR/usr/share/applications/$APP_ID.desktop"
install -Dm644 "$ROOT_DIR/packaging/linux/rom-free-appimage/$APP_ID.metainfo.xml" \
    "$APPDIR/usr/share/metainfo/$APP_ID.appdata.xml"
install -Dm644 "$ROOT_DIR/packaging/linux/assets/io.github.Faderz48.FortyWinksRecompiled.svg" \
    "$APPDIR/$APP_ID.svg"
install -Dm644 "$ROOT_DIR/packaging/linux/assets/io.github.Faderz48.FortyWinksRecompiled.svg" \
    "$APPDIR/usr/share/icons/hicolor/scalable/apps/$APP_ID.svg"
install -Dm644 "$ROOT_DIR/LICENSE" "$APPDIR/usr/share/doc/40-winks-pc-port/LICENSE"
install -Dm644 "$ROOT_DIR/THIRD_PARTY_NOTICES.md" \
    "$APPDIR/usr/share/doc/40-winks-pc-port/THIRD_PARTY_NOTICES.md"
install -Dm644 "$ROOT_DIR/LICENSES/AppImageKit.txt" \
    "$APPDIR/usr/share/doc/40-winks-pc-port/AppImageKit.txt"
printf '%s\n' "$VERSION" > "$APPDIR/usr/share/40-winks-pc-port/VERSION"
printf '%s\n' "$BUILD_ID" > "$APPDIR/usr/share/40-winks-pc-port/BUILD_ID"

if command -v magick >/dev/null 2>&1; then
    magick "$APPDIR/$APP_ID.svg" "$APPDIR/$APP_ID.png"
elif command -v convert >/dev/null 2>&1; then
    convert "$APPDIR/$APP_ID.svg" "$APPDIR/$APP_ID.png"
fi

if [ -f "$APPDIR/$APP_ID.png" ]; then
    install -Dm644 "$APPDIR/$APP_ID.png" \
        "$APPDIR/usr/share/icons/hicolor/256x256/apps/$APP_ID.png"
fi

if [ -z "$APPIMAGETOOL" ]; then
    if command -v appimagetool >/dev/null 2>&1; then
        APPIMAGETOOL="$(command -v appimagetool)"
    else
        APPIMAGETOOL="$TOOLS_DIR/appimagetool-x86_64.AppImage"
    fi
fi

if [ ! -x "$APPIMAGETOOL" ]; then
    curl -L \
        "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage" \
        -o "$APPIMAGETOOL"
    chmod +x "$APPIMAGETOOL"
fi

export ARCH=x86_64
export VERSION
export APPIMAGE_EXTRACT_AND_RUN=1
"$APPIMAGETOOL" "$APPDIR" "$BUILD_DIR/$APP_NAME-x86_64.AppImage"

(
    cd "$BUILD_DIR"
    sha256sum "$APP_NAME-x86_64.AppImage" \
        > "$APP_NAME-x86_64.AppImage.sha256"
)

echo "$BUILD_DIR/$APP_NAME-x86_64.AppImage"
