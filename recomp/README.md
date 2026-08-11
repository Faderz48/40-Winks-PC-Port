# Local Static Recompilation

This directory contains the tracked N64Recomp configuration and interoperability metadata. It intentionally does not contain the game ROM or generated game functions.

From the repository root, run:

```sh
make prepare ROM="/absolute/path/to/clean-40-winks.z64"
```

The command verifies the ROM, creates an ignored `baserom.z64` symlink, generates the ignored symbol and function output under `generated/`, and leaves the original ROM unchanged.

Do not commit `baserom.z64` or anything under `generated/`.
