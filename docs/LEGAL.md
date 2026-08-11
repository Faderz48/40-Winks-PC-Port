# Legal And Redistribution

This is a conservative repository policy, not legal advice.

## Included

- Independently written host/runtime integration code
- Tests and build scripts
- Static-recompilation configuration and interoperability metadata
- Patches to GPL-3.0 and MIT-licensed dependencies, with attribution
- Documentation written for this project

## Excluded

- ROM images and ROM patches containing copyrighted game data
- Extracted textures, models, audio, video, fonts, or text dumps
- Disassembly and private reverse-engineering output
- N64Recomp-generated translations of the original game program
- Executables, AppImages, object files, screenshots, and save files

Every contributor must obtain their own lawful copy of the supported game and perform generation locally. Do not request, link to, or share ROM downloads in this repository.

## Why The Boundary Is Narrow

New Zealand's Copyright Act 1994 section 80A provides a limited decompilation exception for a lawful user when decompilation is necessary to obtain information needed to create an independent interoperable program and that information is used only for that objective. It does not provide a blanket permission to publish the original program or a machine translation of it. See the [official current legislation](https://www.legislation.govt.nz/act/public/1994/143/en/latest/sections/DLM4127268/).

GitHub can remove material in response to copyright claims under its [DMCA Takedown Policy](https://docs.github.com/en/site-policy/content-removal-policies/dmca-takedown-policy). Keeping game-derived output outside the repository reduces that risk but cannot guarantee that a rights holder will agree with every interoperability decision.

Before distributing a binary, seek qualified legal advice or written permission from the relevant rights holders. Requiring a ROM at runtime prevents redistribution of assets, but the current executable also contains locally generated translations of CPU instructions.

## Licensing

The original host project is GPL-3.0-only to comply with N64ModernRuntime. RT64, N64Recomp, and the true split-screen layout are MIT-licensed. Their notices are preserved in `LICENSES/` and `THIRD_PARTY_NOTICES.md`.

No project license grants rights to the **40 Winks** game, its ROM, characters, branding, artwork, music, or other content.
