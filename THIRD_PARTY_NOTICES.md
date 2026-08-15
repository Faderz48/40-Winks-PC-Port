# Third-Party Notices

This repository does not vendor the dependency source trees. The bootstrap script downloads pinned revisions from their original repositories and applies the patches in `patches/`. The Windows setup also downloads verified portable build tools into its private local cache; those tools are not committed to this repository or included in the ROM-free setup executable.

| Component | Revision | License | Purpose |
| --- | --- | --- | --- |
| [N64Recomp](https://github.com/N64Recomp/N64Recomp) | `ffb39cdad1da5de07eaaa48bd1db4a89a7986771` | MIT | Static-recompilation generator |
| [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) | `ae1ffbb909d9f93c88c41830deb539f7feef5ed2` | GPL-3.0 | N64 host runtime |
| [RT64](https://github.com/rt64/rt64) | `f0728a2520d5aa735886240de3fee75cc805f6d6` | MIT | Graphics renderer |
| [40 Winks N64 True Split-screen](https://github.com/Faderz48/40-Winks-N64-True-Split-screen) | `b646ce99fa4c863e9bb8eefa6ee461c1f52c0c06` | MIT | Full-width top/bottom viewport layout |
| [AppImageKit](https://github.com/AppImage/AppImageKit) | appimagetool continuous build `5735cc5` | MIT | AppImage packaging and runtime |
| [.NET Runtime](https://github.com/dotnet/runtime) | 8.x self-contained runtime | MIT and third-party notices | Windows first-run setup application |
| [Git for Windows MinGit](https://github.com/git-for-windows/git) | `2.55.0.windows.4` | GPL-2.0 and bundled notices | Private source checkout tool |
| [CMake](https://github.com/Kitware/CMake) | `3.31.10` | BSD-3-Clause | Private build configuration tool |
| [Ninja](https://github.com/ninja-build/ninja) | `1.13.2` | Apache-2.0 | Private build executor |
| [Python](https://www.python.org/) | `3.13.14` embeddable package | Python Software Foundation License | Private symbol generation tool |
| [Visual Studio Build Tools](https://visualstudio.microsoft.com/downloads/) | 2022 current channel bootstrapper | Microsoft license terms | C++ and Clang compiler installed from Microsoft's service |

Copies of the applicable MIT license texts are under `LICENSES/`. N64ModernRuntime's GPL-3.0 terms are reproduced in the root `LICENSE` file. The .NET Runtime's complete current [third-party notices](https://github.com/dotnet/runtime/blob/main/THIRD-PARTY-NOTICES.TXT) remain available from its official repository.

The project name is used only to identify compatibility. No game ROM, artwork, audio, video, text dump, or other game asset is licensed or distributed here.
