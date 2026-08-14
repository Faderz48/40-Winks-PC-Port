# Building

## Linux Requirements

- Linux on x86-64
- Git and Python 3
- CMake 3.20 or newer
- A C11/C++20 compiler
- SDL2 development files
- Vulkan loader, headers, and a working Vulkan driver
- The build dependencies required by N64ModernRuntime and RT64
- `appimagetool` only when producing an AppImage; the packaging script can download it when network access is available

On the first build, the bootstrap script clones pinned revisions of N64Recomp, N64ModernRuntime, and RT64 into the ignored `work/external/` directory. It initializes their submodules and applies the tracked compatibility patches without modifying the upstream repositories on GitHub.

## Windows Requirements

- Windows 10 or 11 on x86-64
- Visual Studio 2022 Build Tools with Desktop development with C++ and the Clang compiler
- Git, Python 3, CMake, and Ninja
- A Direct3D 12 or Vulkan capable GPU with current drivers

The public Windows setup checks these requirements and can install them directly through Windows Package Manager after confirmation. It does not invoke PowerShell. The Visual Studio Build Tools download is large and may take several minutes.

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
make playable-appimage ROM="/absolute/path/to/40-winks.z64"
```

The AppImage does not contain the ROM or extracted assets. It asks the player to select their own ROM on first launch. The executable does contain locally generated machine translations of the game CPU code, so this project's publication policy does not permit attaching AppImages to public GitHub releases without separate rights-holder permission and a fresh legal review.

## Self-Building Release AppImage

The redistributable AppImage contains only this public source tree and first-run setup logic:

```sh
make appimage
```

It produces `dist/rom-free-appimage/40-Winks-PC-Port-x86_64.AppImage`. That one AppImage performs the normal dependency bootstrap, ROM verification, CPU translation, native build, and runtime packaging on the player's computer. It then starts the game and remains the entry point for later launches.

Optional maintenance commands:

```text
--rebuild       Rebuild the locally generated playable AppImage
--select-rom    Ask for the supported ROM again
--choose-output Choose where the playable AppImage is installed
--output PATH   Install the playable AppImage at PATH
--build-only    Build without launching the game
--rom PATH      Use PATH instead of opening the file chooser
-- GAME_ARGS    Pass the remaining options to the playable game
```

Build logs are written to `~/.cache/40-winks-pc-port/build.log`. On first setup the application asks where to install the generated playable AppImage, defaults to `~/Applications/40-Winks-Recompiled-x86_64.AppImage`, and remembers that choice. The generated runtime must not be redistributed under this project's publication policy.

## Windows Playable Build

The downloadable Windows setup performs the private generation flow from its native C# pipeline. Select the ROM and output folder in the setup window, then choose **Build & Play**. It invokes Git, Python, CMake, Ninja, and the Visual Studio compiler directly; PowerShell is not used on the player's computer.

The older command-line helper remains available for developers who explicitly prefer it:

```powershell
powershell -ExecutionPolicy Bypass -File packaging\windows\build_playable.ps1 `
    -RomPath "C:\path\to\40-winks.z64" `
    -OutputDirectory "$HOME\Documents\40 Winks PC Port"
```

Both paths import the Visual Studio x64 environment, build with `clang-cl` and Ninja, copy `SDL2.dll`, `dxcompiler.dll`, and `dxil.dll` beside the native executable, and never copy the ROM into the output folder. When invoked directly, start it with `forty-winks-recomp.exe --rom C:\path\to\40-winks.z64`. The public setup also copies `40-Winks-PC-Port.exe` into the output folder so later launches need no arguments.

The redistributable setup executable is built by the Windows GitHub Actions workflow or locally with PowerShell and the .NET 8 SDK:

```powershell
packaging\windows\build_release.ps1 -Version "0.1.2-alpha" -BuildId "local"
```

It produces `dist\windows\40-Winks-PC-Port-Windows-x64.exe` and its SHA-256 file. This setup contains only the audited public source and first-run logic. The playable output generated from a ROM remains excluded from public releases.

## Updating Dependencies

Dependency revisions are intentionally pinned in `tools/bootstrap_dependencies.sh` and documented in `THIRD_PARTY_NOTICES.md`. Review upstream changes and licenses before updating a revision, regenerate the applicable patch, then validate a fresh checkout.
