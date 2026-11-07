# WreckingBall for Forza Horizon 3

WreckingBall adds breakable scenery to mainland Australia in **Forza Horizon 3 on Azimuth**. It converts eligible solid trees and palms, fences, railings, Armco guardrails, bollards, and concrete-and-wood benches.

## Install

1. Use the Xbox One FH3 build **1.0.125.0** on Azimuth with FH3 Mod Platform installed.
2. Close FH3 and Azimuth.
3. Extract the [Windows x64 ZIP](../../releases/latest) into the folder containing `azimuth.exe`. The DLL goes in `fh3-mods\WreckingBall.dll`.
4. Start FH3 and open **Pause → MODS → Installed mods → WreckingBall**.

See [Readme.txt](Readme.txt) for migration from the ExtraOptions edition and the details of collision and debris behavior.

## Settings

Use D-pad Left/Right or left-stick Left/Right to stage changes. Press **A** to apply them or **Back** to discard them. The DLL writes confirmed settings to `fh3-mods\WreckingBall.ini`. The [default configuration](WreckingBall.ini) documents the available options.

Debris can fade by age or distance. The converted concrete-and-wood bench uses its original model as a single loose piece with ground collision. Tree debris depends on the fracture meshes loaded in the area.

## Build from source

Install Visual Studio or Visual Studio Build Tools with **Desktop development with C++**, an x64 MSVC toolset, and a Windows SDK. Run the following in Command Prompt from the repository root.

```bat
build.cmd test
```

The Mod Platform build is `build\WreckingBallPlatform.dll`. Copy it to `fh3-mods\WreckingBall.dll` to install it manually. The build also produces the legacy ExtraOptions DLL at `build\WreckingBall.dll`. The bundled regression tests run without an FH3 installation.

## License and credits

The FH3 port is distributed under [GPL-3.0-or-later](LICENSE). It is based on Trinculo54's [WreckingBall for FH2](https://github.com/Trinculo54/WreckingBall). See [CREDITS.md](CREDITS.md) for attribution and third-party licenses.
