# Building HarleyVR on Windows

The beta uses two Windows x64 executables. The repository's `Src/` and `Makefiles/` carry the SteamVR edition. The Meta PC VR edition uses a separately playtested executable. The versioned source assets include the PowerPC training counters and the edition-specific build instructions. Use the matching archive from the [GitHub Releases page](https://github.com/WiseSoundDaryl/harley-supermodel-vr/releases).

| Edition | Source archive | Reference executable SHA-256 |
| --- | --- | --- |
| SteamVR | `HarleyVR-PSVR2-source.zip` | `ad45a8fb2911fd94b40c132b499ccc748043d4307e3e44056201032ece38223a` |
| Meta PC VR | `HarleyVR-RiftS-source.zip` | `5c10ba397d591a5ed1a1b4659833c8f05ae98a4009205f398c1b9d0a1e3f8680` |
| Windows launcher and package builder | `HarleyVR-Launchers-source.zip` | [Launcher source](../packaging/beta/Launcher/HarleyVR.cs) |

## Compile an emulator

1. Extract the edition-specific source ZIP into an empty folder on a local Windows drive.
2. Install Windows Python 3.10 or later and an MSYS2 **MINGW64** toolchain. Use GNU Make 4.4.1-3, GCC 16.2.0-4, binutils 2.47-3, SDL2 2.32.10-1, SDL2_net 2.4.0-1, zlib 1.3.2-2 and OpenXR SDK 1.1.62-1 for the reference build. [MSYS2 setup](https://www.msys2.org/) · [MINGW64 package archive](https://repo.msys2.org/mingw/mingw64/)
3. From inside the extraction, run `py -3 Build-Windows.py --msys2 C:\msys64`, substituting your install path. The archive's `BUILD.md` covers runtime DLLs and output locations.
4. Compare `bin64/supermodel.exe` with the reference SHA-256 above. The builds regenerate all 93 linked objects, the 68K core and the Windows resources. No game, headset or old development object files are required to compile.

## Build the launchers

The launcher source asset includes the Windows C# launcher, its tests, the portable scripts, the packaging recipe and its clean installation data. Its README describes how to build the three entry points and assemble the Windows player ZIP. The regular PowerShell integrity checks and inert test fixtures under `packaging/` can run independently. The GitHub publication workflow stays disabled.

## Dependency source archives

The following MSYS2 source packages correspond to the libraries used by Windows beta 1. Each contains the package build recipe. For an ordinary build, install the MINGW64 binary packages listed above.

| Component | Source for the shipped version |
| --- | --- |
| GCC runtime (`libgcc_s_seh-1.dll` and `libstdc++-6.dll`) | [GCC 16.2.0-4](https://repo.msys2.org/mingw/sources/mingw-w64-gcc-16.2.0-4.src.tar.zst) |
| OpenXR loader (`libopenxr_loader.dll`) and its JSON parser | [OpenXR SDK 1.1.62-1](https://repo.msys2.org/mingw/sources/mingw-w64-openxr-sdk-1.1.62-1.src.tar.zst) · [JsonCpp 1.9.6-3](https://repo.msys2.org/mingw/sources/mingw-w64-jsoncpp-1.9.6-3.src.tar.zst) |
| Windows threading (`libwinpthread-1.dll`) | [winpthreads 14.0.0.r426.g4564ee4b5-1](https://repo.msys2.org/mingw/sources/mingw-w64-winpthreads-14.0.0.r426.g4564ee4b5-1.src.tar.zst) |
| MinGW-w64 C runtime and headers | [CRT 14.0.0.r426.g4564ee4b5-1](https://repo.msys2.org/mingw/sources/mingw-w64-crt-14.0.0.r426.g4564ee4b5-1.src.tar.zst) · [matching headers](https://repo.msys2.org/mingw/sources/mingw-w64-headers-14.0.0.r426.g4564ee4b5-1.src.tar.zst) |
| Libraries linked into the emulator | [SDL2 2.32.10-1](https://repo.msys2.org/mingw/sources/mingw-w64-SDL2-2.32.10-1.src.tar.zst) · [SDL2_net 2.4.0-1](https://repo.msys2.org/mingw/sources/mingw-w64-SDL2_net-2.4.0-1.src.tar.zst) · [zlib 1.3.2-2](https://repo.msys2.org/mingw/sources/mingw-w64-zlib-1.3.2-2.src.tar.zst) |
