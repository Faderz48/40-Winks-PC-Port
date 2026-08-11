#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/recomp-appimage"
APPDIR="$ROOT_DIR/dist/recomp-appimage/AppDir"
DIST_DIR="$ROOT_DIR/dist/recomp-appimage"
TOOLS_DIR="$ROOT_DIR/dist/tools"
APP_NAME="40-Winks-Recompiled"
APPIMAGETOOL="${APPIMAGETOOL:-}"

mkdir -p "$DIST_DIR" "$TOOLS_DIR"
rm -rf "$APPDIR"

cmake -S "$ROOT_DIR/recomp-port" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD_DIR" --parallel "${JOBS:-2}"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --strip

install -Dm755 "$ROOT_DIR/packaging/linux/recomp-appimage/AppRun" "$APPDIR/AppRun"
install -Dm644 "$ROOT_DIR/packaging/linux/recomp-appimage/io.github.Faderz48.FortyWinksRecompiled.desktop" \
    "$APPDIR/io.github.Faderz48.FortyWinksRecompiled.desktop"
install -Dm644 "$ROOT_DIR/packaging/linux/recomp-appimage/io.github.Faderz48.FortyWinksRecompiled.desktop" \
    "$APPDIR/usr/share/applications/io.github.Faderz48.FortyWinksRecompiled.desktop"
install -Dm644 "$ROOT_DIR/packaging/linux/recomp-appimage/io.github.Faderz48.FortyWinksRecompiled.metainfo.xml" \
    "$APPDIR/usr/share/metainfo/io.github.Faderz48.FortyWinksRecompiled.appdata.xml"
install -Dm644 "$ROOT_DIR/packaging/linux/assets/io.github.Faderz48.FortyWinksRecompiled.svg" \
    "$APPDIR/usr/share/icons/hicolor/scalable/apps/io.github.Faderz48.FortyWinksRecompiled.svg"
install -Dm644 "$ROOT_DIR/packaging/linux/assets/io.github.Faderz48.FortyWinksRecompiled.svg" \
    "$APPDIR/io.github.Faderz48.FortyWinksRecompiled.svg"

if command -v magick >/dev/null 2>&1; then
    magick "$ROOT_DIR/packaging/linux/assets/io.github.Faderz48.FortyWinksRecompiled.svg" \
        "$APPDIR/io.github.Faderz48.FortyWinksRecompiled.png"
    install -Dm644 "$APPDIR/io.github.Faderz48.FortyWinksRecompiled.png" \
        "$APPDIR/usr/share/icons/hicolor/256x256/apps/io.github.Faderz48.FortyWinksRecompiled.png"
elif command -v convert >/dev/null 2>&1; then
    convert "$ROOT_DIR/packaging/linux/assets/io.github.Faderz48.FortyWinksRecompiled.svg" \
        "$APPDIR/io.github.Faderz48.FortyWinksRecompiled.png"
    install -Dm644 "$APPDIR/io.github.Faderz48.FortyWinksRecompiled.png" \
        "$APPDIR/usr/share/icons/hicolor/256x256/apps/io.github.Faderz48.FortyWinksRecompiled.png"
fi

mkdir -p "$APPDIR/usr/lib"
while IFS= read -r lib; do
    case "$lib" in
        /lib*/ld-linux*|/usr/lib*/ld-linux*|linux-vdso*|"")
            continue
            ;;
    esac

    base="$(basename "$lib")"
    case "$base" in
        libc.so.*|libdl.so.*|libm.so.*|libpthread.so.*|librt.so.*|libresolv.so.*)
            continue
            ;;
    esac

    if [ -f "$lib" ] && [ ! -e "$APPDIR/usr/lib/$base" ]; then
        cp -L "$lib" "$APPDIR/usr/lib/$base"
    fi
done < <(ldd "$APPDIR/usr/bin/forty-winks-recomp" | awk '/=>/ {print $(NF-1)} /^[[:space:]]*\// {print $1}')

if [ "${NO_APPIMAGE:-0}" = "1" ]; then
    echo "$APPDIR"
    exit 0
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
export VERSION="${VERSION:-dev}"
export APPIMAGE_EXTRACT_AND_RUN=1
"$APPIMAGETOOL" "$APPDIR" "$DIST_DIR/$APP_NAME-x86_64.AppImage"

echo "$DIST_DIR/$APP_NAME-x86_64.AppImage"
