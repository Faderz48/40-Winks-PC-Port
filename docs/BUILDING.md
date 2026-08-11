# Building

## Requirements

- Linux on x86-64
- Git and Python 3
- CMake 3.20 or newer
- A C11/C++20 compiler
- SDL2 development files
- Vulkan loader, headers, and a working Vulkan driver
- The build dependencies required by N64ModernRuntime and RT64
- `appimagetool` only when producing an AppImage; the packaging script can download it when network access is available

On the first build, the bootstrap script clones pinned revisions of N64Recomp, N64ModernRuntime, and RT64 into the ignored `work/external/` directory. It initializes their submodules and applies the tracked compatibility patches without modifying the upstream repositories on GitHub.

## Build

Use a clean, big-endian USA ROM whose SHA-256 is listed in the root README:

```sh
make build ROM="/absolute/path/to/40-winks.z64"
```

The build performs these local-only steps:

1. Verifies the supplied ROM hash.
2. Creates an ignored `recomp/baserom.z64` symlink to it.
3. Generates the symbol map and N64Recomp C output under ignored `recomp/generated/`.
4. Builds the native executable under ignored `build/`.

Nothing copies the ROM into the repository or package.

## Run

```sh
make run ROM="/absolute/path/to/40-winks.z64"
```

You can also launch the built program directly:

```sh
./build/recomp-port/forty-winks-recomp --rom "/absolute/path/to/40-winks.z64"
```

## Test

```sh
make test ROM="/absolute/path/to/40-winks.z64"
```

## AppImage

```sh
make appimage ROM="/absolute/path/to/40-winks.z64"
```

The AppImage does not contain the ROM or extracted assets. It asks the player to select their own ROM on first launch. The executable does contain locally generated machine translations of the game CPU code, so this project's publication policy does not permit attaching AppImages to public GitHub releases without separate rights-holder permission and a fresh legal review.

## Updating Dependencies

Dependency revisions are intentionally pinned in `tools/bootstrap_dependencies.sh` and documented in `THIRD_PARTY_NOTICES.md`. Review upstream changes and licenses before updating a revision, regenerate the applicable patch, then validate a fresh checkout.
