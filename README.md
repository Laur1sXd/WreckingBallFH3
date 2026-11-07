# WreckingBall for Forza Horizon 3

WreckingBall lets you crash through scenery that normally stops your car in mainland Australia. Large trees and palms, selected fences and railings, Armco guardrails, bollards, and concrete-and-wood benches can break on impact. Each group can be turned on or off in the game's Mods menu.

Converted trees can drop trunks and branches using FH3's own fracture pieces. Solid wood-topped benches leave their original model as a loose piece. Where matching debris is available, it can land on the ground and fade after a configurable time or distance. Bigger trees take more speed off the car, and you can adjust how much.

## Download

[**Download WreckingBall FH3 1.0.0**](https://github.com/Laur1sXd/WreckingBallFH3/releases/tag/v1.0.0)

Requires the Xbox One build of **Forza Horizon 3 (1.0.125.0)** running on **Azimuth** with **FH3 Mod Platform** installed.

## Settings

Use **D-pad Left/Right** or **left-stick Left/Right** to preview a value. Move between rows to stage multiple changes, then press **A** to apply them all or **Back** to discard them.

Confirmed settings are saved to `fh3-mods\WreckingBall.ini`. Scenery switches apply during the session. If you turn a conversion on in an area that loaded while it was off, leave and return so that scenery can stream in again. Restart Azimuth after editing the INI by hand.

| INI setting | Default | What it does |
| --- | --- | --- |
| `Enabled` | `1` | Turns the mod on or off. |
| `InjectTrees` | `1` | Lets eligible trees and palms break. |
| `Barriers` | `1` | Enables the individual barrier switches. |
| `Fences`, `Railings`, `Guardrails`, `Bollards` | `1` each | Controls which eligible barriers break. |
| `ConcreteBenches` | `1` | Lets solid concrete-and-wood benches break. |
| `SpeedLoss` | `0.12` | Sets the share of speed lost when hitting a normal-sized converted tree. |
| `SpeedLossMax` | `0.45` | Caps the extra tree slowdown. Set it to `0` to disable the extra slowdown for all converted objects. |
| `SpeedLossExponent` | `1.5` | Controls how strongly tree size affects slowdown. |
| `BarrierSpeedLoss` | `0.04` | Sets the share of speed lost when hitting a converted barrier. |
| `BarrierSkipEvents`, `BarrierNeedGround` | `1` each | Keep event barriers solid and require terrain on both sides before a barrier can break. |
| `DebrisCleanup` | `1` | Lets the mod fade out loose pieces. |
| `DebrisLifetime`, `DebrisDistance` | `45`, `120` | Fade debris after 45 seconds or at 120 metres away. Set either value to `0` to disable that limit. |
| `Debug` | `0` | Enables detailed logging. Press **F8** to log nearby scenery at any setting. |

See [WreckingBall.ini](WreckingBall.ini) for the full list of settings and defaults.

## Scenery and debris

The mod adds breakable physics only to eligible placements that FH3 treats as solid. Scenery FH3 already breaks, including bushes, crops, and breakable utility poles, keeps its stock behavior. Buildings, roads, terrain, and collision the mod cannot confidently match to a supported object stay solid.

The **Railings** and **Guardrails** switches also cover audited sections at three mainland bridges where the car can land in shallow water.

Tree debris depends on which meshes FH3 has loaded in the area. The mod prefers pieces from the same tree family and can use other loaded wood fragments as a fallback. A converted object can clear without visible debris when the required mesh is unavailable. Benches and some barriers use their original model as one loose piece.

## How it works

WreckingBall adds supported objects to FH3's native breakable scenery system. It matches the original object to its collision, then releases only the matched collision when the object breaks. Debris uses FH3's physics and rendering so the loose pieces can settle on the ground and fade.

## Build from source

Install Visual Studio or Visual Studio Build Tools with **Desktop development with C++**, an x64 MSVC toolset, and a Windows SDK. Run this command from the repository root in Command Prompt.

```bat
build.cmd test
```

The Mod Platform DLL is built at `build\WreckingBallPlatform.dll`. For a manual install, copy it to `fh3-mods\WreckingBall.dll`. The build also produces `build\WreckingBall.dll` for the legacy ExtraOptions loader. The bundled regression tests run without an FH3 installation.

See the [file-by-file repository guide](docs/repository-guide.md) for the source, tests, build scripts, and dependencies.

## Install

1. Close FH3 and Azimuth.
2. Extract the ZIP into the folder containing `azimuth.exe`. The mod DLL should be at `fh3-mods\WreckingBall.dll`.
3. If you used the ExtraOptions edition, remove its old DLL from `mods\WreckingBall\plugins` before starting the game.
4. Start FH3 and open **Pause → MODS → Installed mods → WreckingBall**.

WreckingBall is designed to run alongside other FH3 Mod Platform mods. Put their DLLs in the same `fh3-mods` folder. [Levilente's FH3 mods](https://github.com/Levilente) · [Installation guide](Readme.txt)

## Uninstall

Close FH3 and Azimuth, then remove `fh3-mods\WreckingBall.dll`. Remove `WreckingBall.ini` and `WreckingBall.log` from that folder if you also want to clear the mod's settings and log.

## License and credits

WreckingBall FH3 is an unofficial mod distributed under [GPL-3.0-or-later](LICENSE). It is based on Trinculo54's WreckingBall for Forza Horizon 2. [Original WreckingBall](https://github.com/Trinculo54/WreckingBall) · [Credits and third-party licenses](CREDITS.md)
