# HarleyVR Windows launcher source

For a published build, download the matching emulator sources and launcher sources from the same [beta 2 release](https://github.com/WiseSoundDaryl/harley-supermodel-vr/releases/tag/v0.1.0-beta.2).

## Build the launchers

On 64-bit Windows, open Windows PowerShell in this extracted folder. Run:

```powershell
.\Launcher\Build-Launcher.ps1 -OutputDirectory .\out
.\MetaGraphics\Build-MetaGraphics.ps1 -Msys2Root C:\msys64 -OutputDirectory .\out
```

The compiler is Microsoft's Windows .NET Framework 4.x C# compiler. The three Windows Forms launchers contain identical code. Their filenames select the headset route or the chooser. The compiler writes a new PE timestamp and managed module ID on each build.

The second command builds the direct Meta headset graphics menu using MINGW64, the OpenXR headers and Windows D3D11. The menu shares the loader shipped beside the Meta emulator.

## Assemble a private local package

Use the companion **HarleyVR-PSVR2-source.zip** and **HarleyVR-RiftS-source.zip** to build the two versions of supermodel.exe. Place each one alongside the four matching 64-bit runtime libraries from MSYS2 MINGW64. The loader uses OpenXR SDK 1.1.62-1, libgcc and libstdc++ use 16.2.0-4, and libwinpthread uses 14.0.0.r426.g4564ee4b5-1. These filenames are libopenxr_loader.dll, libgcc_s_seh-1.dll, libstdc++-6.dll and libwinpthread-1.dll.

With Python 3.9 or newer, assemble the portable files:

```powershell
py -3 Package-Windows.py --steamvr .\psvr2-binaries --meta .\rift-binaries --launchers .\out --output .\HarleyVR-draft.zip
```

The player scripts, clean factory INI, emulator crosshairs, game database and notices are included. The EXE chooses a runtime for the game process and keeps Meta ASW recovery active during direct Meta PC VR sessions. Personal game data is kept in the extracted installation. Supply your own legally obtained Revision B ROM.

## Release verification

The ordinary packaging command creates a draft. To produce a launchable package, the maintainer supplies a JSON record using `--verified-release RECORD.json --source-assets DIRECTORY`. The record contains the release `tag`, a `files` map of every reviewed player file to its uppercase SHA-256 (forward-slash paths), and a `sources` map with SHA-256 entries for `SteamVR`, `Meta` and `Launchers`. The packer checks every byte against the record, including the three separately distributed source archives, before writing the final release manifest.

The recorded source checksums identify the archives for each binary version. The SteamVR edition includes the ride menu and Dream Ride startup option. The Meta PC VR edition opens its graphics menu before starting the original arcade interface. Use `HarleyVR-SteamVR.exe --check` or `HarleyVR-Meta.exe --check` for read-only installation and runtime checks.

See data/LICENSE.txt and data/Docs/THIRD-PARTY-NOTICES.md for the license and component credits.

The release asset **HarleyVR-Launchers-source.zip** includes the exact data inputs for the packaging recipe. This directory contains the source, build scripts and installation inventory.
