# Developer build status

This is a local release-staging draft, not a verified standalone source release. The regular-game reference executable is identified by SHA-256:

```text
7DDD463B30818ED3A21CE0E5BDDA966A99B8769E81586F873F4164B23D9712A7
```

It passed the recorded offline rendering checks. A clean build from a fresh public checkout, and a headset playtest of the final packaged release, are still release gates. Do not treat the existence of a development executable as evidence that these gates have passed.

## Source that must accompany the release

The release needs the complete corresponding modified Supermodel source, the selected source revision, and scripts/instructions sufficient to build it. The unmodified upstream repository alone is not the source for this executable. Keep upstream copyright and license notices and identify the VR changes.

The executable currently contains optional experimental code even though the intended public launcher exposes only regular riding. Its corresponding source must include that compiled code too. Omitting preview launchers does not remove code from the executable. Removing those features from source would create a new build requiring validation.

Source staging is not yet complete. A public source archive must include required new files that were not previously tracked in the development repository. It must exclude ROMs, copied game assets, save states, personal configuration, logs, diagnostic captures, credentials, old binaries and development-only object folders.

## Build system and dependencies

The implementation is based on the [official Supermodel source](https://github.com/trzy/Supermodel). The Windows makefile includes the Windows OpenXR implementation. The upstream-style entry point, from a complete source checkout in an MSYS2 MINGW64 shell, is:

```sh
make -f Makefiles/Makefile.Win32 -j4
```

This is an orientation command, **not yet a validated recipe for reproducing the reference executable**. Do not run a clean target against a preserved development installation. Test a separate fresh checkout instead.

The recorded development installation contains:

| Component | Recorded package version | Purpose |
| --- | --- | --- |
| MinGW-w64 GCC | 16.2.0-4 | C/C++ compiler and runtime libraries |
| SDL2 | 2.32.10-1 | Window, input and audio support; linked statically |
| SDL2_net | 2.4.0-1 | Upstream networking support; linked statically |
| zlib | 1.3.2-2 | ZIP/decompression support; linked statically |
| OpenXR SDK | 1.1.62-1 | OpenXR headers and dynamically loaded runtime loader |
| GNU Make, Git, ZIP utilities | Pin during build preparation | Build and packaging tools |

These are an inventory of the inspected installation, not a claim that an unpinned package update recreates it. Record compiler versions, dependency package hashes, all build options and the final source commit in the public release manifest. Use the [MSYS2 documentation](https://www.msys2.org/docs/) for the toolchain environment and the [OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK) for loader/header provenance.

## Work required before a clean build is claimed

1. Prepare a complete source tree and a portable build script with no developer-specific drive paths or installed-editor dependencies.
2. Replace the development-only incremental build procedure. It currently reuses 93 pinned object files from multiple earlier experiments, including a profile-trained PowerPC emulation object. A fresh build must generate every required object from documented inputs.
3. Decide how to reproduce the profile-guided optimization. Either document and provide permissible build-profile inputs and the training/rebuild process, or produce and validate a new release configuration that does not depend on that private object. Do not distribute game ROMs or save-state fixtures as build inputs.
4. Update the Windows build workflow to install the OpenXR SDK, pin dependencies and package the dynamically loaded OpenXR loader and its non-system runtime dependencies. The inherited upstream workflow does not currently cover the VR additions.
5. Build in a fresh checkout outside the development workspace. Verify that the source, scripts and declared dependencies are sufficient. Reused development objects are not an acceptable substitute for this check.
6. Compare native game state, stereo images, scene interpolation, menus, audio and representative performance with the retained reference. Build changes may affect behavior or timing even when source appears unchanged.
7. Record the resulting executable hash and package that exact binary. If it differs from the retained reference, label and validate it as a new candidate.

Bit-for-bit reproducibility has not been demonstrated. A documented successful clean build is required; a claim of identical output additionally needs its own verification.

## Runtime packaging

The current reference uses `supermodel.exe` plus `libopenxr_loader.dll`, `libgcc_s_seh-1.dll`, `libstdc++-6.dll` and `libwinpthread-1.dll`. Keep the verified real loader. Never package a synthetic test runtime. Include clean configuration, the required upstream assets, license texts, concise user instructions and checksums. Do not package compiler tools or development reports for players.

The public launcher must locate its own files relative to the extracted folder, use the supported installed VR runtime without relying on a fixed Steam installation path, and create its own writable settings/save locations. It must not depend on old builds or personal game profiles.

All automated emulator tests must use `-sound -dsb -sound-volume=0 -music-volume=0`, then verify both effective volumes are zero and sound/DSB emulation remains enabled. Muting test output must not remove audio emulation work. Normal user play can retain sound.

See [the release checklist](RELEASE-CHECKLIST.md) and [the license inventory](THIRD-PARTY-NOTICES.md) for remaining gates.
