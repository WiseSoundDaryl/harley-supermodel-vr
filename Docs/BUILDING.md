# Developer build status

This is release-preparation documentation, not a verified standalone source release. The regular-game development references are identified by SHA-256:

```text
PSVR2 main reference:
7DDD463B30818ED3A21CE0E5BDDA966A99B8769E81586F873F4164B23D9712A7

Rift S direct-Meta collision-feedback reference, wearer-tested November 1:
5C10BA397D591A5ED1A1B4659833C8F05AE98A4009205F398C1B9D0A1E3F8680
```

The Rift S reference retains the main renderer and frame scheduling, passes completed collision-response metadata into controller feedback, and was reported smooth with working wall/car vibration. A clean build from a fresh public checkout, and validation of the final packaged release on each advertised headset/runtime route, are still release gates. These development references are not public player downloads.

## Source that must accompany the release

The release needs the complete corresponding modified Supermodel source, the selected source revision, and scripts/instructions sufficient to build it. The unmodified upstream repository alone is not the source for this executable. Keep upstream copyright and license notices and identify the VR changes.

The executable currently contains optional experimental code even though the intended public launcher exposes only regular riding. Its corresponding source must include that compiled code too. Omitting preview launchers does not remove code from the executable. Removing those features from source would create a new build requiring validation.

Source staging is not yet complete. A public source archive must include required new files that were not previously tracked in the development repository. It must exclude ROMs, copied game assets, save states, personal configuration, logs, diagnostic captures, credentials, old binaries and development-only object folders.

## Build system and dependencies

The implementation is based on the [official Supermodel source](https://github.com/trzy/Supermodel). The private development build includes the Windows OpenXR implementation; the public checkout does not yet contain that integration or its complete build inputs. The upstream-style entry point, from a complete source checkout in an MSYS2 MINGW64 shell, is:

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
| JsonCpp | 1.9.6-3 in the loader's recorded build inputs | Statically included in the OpenXR loader; its own notice is required |
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

The public Meta and SteamVR launchers must locate their own files relative to the extracted folder, use the matching installed VR runtime without relying on fixed machine paths, and create their own writable settings/save locations. They must not depend on old builds or personal game profiles, or change the system's default OpenXR runtime. The direct Meta path must retain the verified temporary ASW-off and exact restoration behavior independently of HarleyVR's scene-interpolation setting.

Windows 64-bit is the validated development platform. Upstream platform support does not establish that this VR integration builds or works on macOS or Linux. Logitech G29 driver support, steering range and wheel force feedback require their own platform-specific checks; successful OpenXR controller haptics do not validate wheel force feedback.

All automated emulator tests must use `-sound -dsb -sound-volume=0 -music-volume=0`, then verify both effective volumes are zero and sound/DSB emulation remains enabled. Muting test output must not remove audio emulation work. Normal user play can retain sound.

See [the release checklist](RELEASE-CHECKLIST.md) and [the license inventory](THIRD-PARTY-NOTICES.md) for remaining gates.

## Automated source checks

[HarleyVR source integrity](../.github/workflows/harleyvr-checks.yml) runs on pushes and pull requests to `master`, or by manual dispatch. It uses a Windows 2022 runner with read-only repository permissions and a pinned checkout action. Checkout preserves the recorded notice bytes instead of converting line endings.

The workflow verifies notice hashes, the 15 recorded Dear ImGui source identities, an audited subset of 15 source references in the license manifest, tracked PowerShell syntax, and the [source-distribution policy](SOURCE-SAFETY.md). Three other recorded CPU/sound source identities remain unresolved; a passing subset check does not verify them. See [recorded source verification](RECORDED-SOURCE-VERIFICATION.md) for the scope, line-ending rules and findings. The two source counts overlap and must not be added together.

Synthetic regression tests cover recorded source identities, Meta ASW recovery and both launcher preflight routes. They use disposable fixtures and mocked diagnostic, process and runtime boundaries. They do not start the game, contact the installed Meta diagnostic tool, change actual ASW settings, use ROMs or headsets, build player software, or publish a release.

The checks also run locally with Windows PowerShell 5.1 or later:

```powershell
packaging/Test-ReleaseNotices.ps1 -Root . -SourceRoot .
packaging/Test-VendoredSourceIdentitiesFixtures.ps1
packaging/Test-SourceSyntax.ps1 -Root .
packaging/Test-SourceSafety.ps1 -Root .
packaging/Test-SourceSafetyFixtures.ps1
packaging/Test-MetaAswRecovery.ps1
packaging/Test-LauncherPreflight.ps1
```

Use this checkout as `SourceRoot` to verify its recorded ImGui snapshot and the explicitly audited subset of other manifest references, or omit that argument when checking notices in a player package. The result separately reports verified and pending references; `AllRecordedSourcesVerified` remains false. The standalone `Test-VendoredSourceIdentities.ps1` defaults to strict `All` scope and currently fails on the three unresolved identities. `-Scope AuditedSubset` selects the same bounded policy used by the notice checker and CI.

Syntax checks read tracked working files. Source-safety checks read the staged Git blobs, including files omitted by sparse checkout; stage intended changes before running that check locally.

The regression harnesses require no private reference manifest or external test framework. They create small, isolated temporary directories and remove only their own fixtures. ASW cases cover initial modes, interrupted sessions, identity changes, external settings, invalid recovery records and failed restoration. Launcher cases exercise the actual `-PreflightOnly` path, diagnostic messages, launch arguments and unchanged fixture files. A synthetic passing package contains inert test data, not runnable game files.

`packaging/validation-result.json` remains the historical development record for the original launcher draft. `Test-LauncherDraft.ps1` retains its private-reference comparison for development use; the standalone commands above are the reproducible public checks. Current results appear in the workflow log, rather than treating the older record as validation of changed files.

A green source-integrity check is **not** a clean-build result, complete corresponding-source review, dependency-provenance check, legal clearance, physical-headset validation or release approval. Those gates remain outstanding as described above.

[Upstream Supermodel build (not HarleyVR)](../.github/workflows/build.yml) is the inherited reusable workflow. Its Windows/Linux/macOS matrix and dependencies do not validate the VR integration; the new checks do not invoke it. The separate [release-preparation workflow](../.github/workflows/release.yml) remains intentionally blocked and cannot publish a player package.
