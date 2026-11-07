# Repository guide

## Build, configuration, and documentation

| File | Role |
| --- | --- |
| [`.gitattributes`](../.gitattributes) | Sets consistent line endings for text, Windows batch files, and workflows. |
| [`.gitignore`](../.gitignore) | Keeps local builds, IDE files, and logs out of Git. |
| [`.github/workflows/build.yml`](../.github/workflows/build.yml) | Builds and tests the Windows x64 DLL, creates the install and source archives, and publishes the release assets. |
| [`build.cmd`](../build.cmd) | Locates MSVC and builds the Mod Platform and legacy ExtraOptions DLLs. Pass `test` to run the regression suite. |
| [`WreckingBall.ini`](../WreckingBall.ini) | Sets and documents the default scenery switches, impact slowdown, and debris cleanup. |
| [`README.md`](../README.md) | Explains what the mod does, how to install it, and how to build it. |
| [`Readme.txt`](../Readme.txt) | Ships in the download with install, migration, and gameplay details. |
| [`RELEASE_NOTES.md`](../RELEASE_NOTES.md) | Records the installation and compatibility notes for v1.0.0 and its November update. |
| [`CREDITS.md`](../CREDITS.md) | Credits the upstream mod, platform SDK, and dependencies. |
| [`LICENSE`](../LICENSE) | Contains the GNU GPL v3 license text. |
| [`docs/repository-guide.md`](repository-guide.md) | Explains the role of every tracked file. |

## Mod source

| File | Role |
| --- | --- |
| [`wreckingball_fh3.cpp`](../src/wreckingball_fh3.cpp) | Installs FH3 hooks and handles scenery conversion, collision, loose debris, cleanup, and slowdown. |
| [`platform_menu.h`](../src/platform_menu.h) | Handles controller previews, applying changes with A, and discarding them with Back. |
| [`rules.h`](../src/rules.h) | Defines scenery selection, category switches, and impact slowdown calculations. |
| [`coverage.h`](../src/coverage.h) | Finds placements the mod can add to partial batches while preserving stock breakable groups. |
| [`australia_scenery_models.h`](../src/australia_scenery_models.h) | Maps recognized mainland model names to FH3 asset indices. |
| [`shallow_bridge_rails.h`](../src/shallow_bridge_rails.h) | Lists the audited railing, post, and guardrail placements over shallow water and identifies their collision meshes. |
| [`native_clutter_catalog.h`](../src/native_clutter_catalog.h) | Lists stock physics definition names used to resolve supported scenery at runtime. |
| [`placed_catalog.h`](../src/placed_catalog.h) | Labels mainland model IDs for the F8 scenery diagnostic. |
| [`fh3mod.h`](../src/fh3mod.h) | Provides the FH3 Mod Platform SDK interface. |
| [`fh3_plugin.h`](../src/fh3_plugin.h) | Defines the legacy FH3 ExtraOptions loader interface. |
| [`WreckingBall.rc`](../src/WreckingBall.rc) | Embeds the default INI in the DLL. |

## Tests

| File | Role |
| --- | --- |
| [`rules_test.cpp`](../tests/rules_test.cpp) | Checks scenery classification, exclusions, and slowdown calculations. |
| [`coverage_test.cpp`](../tests/coverage_test.cpp) | Checks gap detection for partial stock breakable groups. |
| [`runtime_test.cpp`](../tests/runtime_test.cpp) | Uses game data mocks to check tree routes, shallow bridge railings, collision protection, debris, bench inertia, and runtime settings. |
| [`platform_settings_test.cpp`](../tests/platform_settings_test.cpp) | Checks preview navigation, A and Back behavior, and INI persistence. |
| [`platform_smoke.cpp`](../tests/platform_smoke.cpp) | Tests real loader integration in a fixture that maps the supported FH3 executable without starting the game. |

## Third-party code and licenses

| File | Role |
| --- | --- |
| [`LICENSE-FH3-Mod-Platform-SDK.txt`](../third_party/LICENSE-FH3-Mod-Platform-SDK.txt) | Contains the MIT terms for the bundled Mod Platform header. |
| [`minhook/LICENSE.txt`](../third_party/minhook/LICENSE.txt) | Contains license notices for MinHook and its instruction decoder. |
| [`minhook/include/MinHook.h`](../third_party/minhook/include/MinHook.h) | Declares the MinHook API used by the legacy build. |
| [`minhook/src/buffer.c`](../third_party/minhook/src/buffer.c) | Manages executable memory for hook trampolines. |
| [`minhook/src/buffer.h`](../third_party/minhook/src/buffer.h) | Declares MinHook’s executable buffer helpers. |
| [`minhook/src/hook.c`](../third_party/minhook/src/hook.c) | Implements hook registration, activation, and teardown. |
| [`minhook/src/trampoline.c`](../third_party/minhook/src/trampoline.c) | Builds trampolines that preserve original function instructions. |
| [`minhook/src/trampoline.h`](../third_party/minhook/src/trampoline.h) | Declares MinHook’s trampoline data structures and helpers. |
| [`minhook/src/hde/hde64.c`](../third_party/minhook/src/hde/hde64.c) | Decodes x86-64 instructions for MinHook. |
| [`minhook/src/hde/hde64.h`](../third_party/minhook/src/hde/hde64.h) | Declares the decoder interface and result structure. |
| [`minhook/src/hde/pstdint.h`](../third_party/minhook/src/hde/pstdint.h) | Defines fixed-width integer types used by the decoder. |
| [`minhook/src/hde/table64.h`](../third_party/minhook/src/hde/table64.h) | Holds opcode tables for the x86-64 decoder. |
