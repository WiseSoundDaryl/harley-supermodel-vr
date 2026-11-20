# First public release checklist

This checklist covers the first Windows beta. PSVR2 and Rift S use separate, playtested emulator builds. PSVR2 includes the optional startup Dream Ride. Direct Meta retains the original arcade interface. The [release status](RELEASE-STATUS.md) and the [Releases page](https://github.com/WiseSoundDaryl/harley-supermodel-vr/releases) identify the candidate and published downloads.

## 1. Freeze the release candidate

- [x] Choose the exact regular-game executable and source revision; preserve the successful previous build unchanged.
- [x] Record executable, four real runtime DLL and configuration hashes.
- [x] Record a separate reference executable and source archive for the playtested PSVR2 and Rift S editions.
- [ ] Keep the familiar steering camera and regular controls. Do not accidentally enable experimental hands/walking, alternate HUD, rejected camera filters or diagnostic capture.
- [x] Give the release an honest version such as a beta/prerelease until its release gates pass. Do not advertise a locked 120 FPS or universal headset compatibility.

## 2. Complete source and licensing

- [x] Prepare the complete corresponding modified source, for the two emulator executables, including optional code compiled into each edition.
- [x] Include the scripts and declared inputs needed to build and install it; resolve the retained-object/profile-guided-build dependency described in [BUILDING.md](BUILDING.md).
- [x] Restore the exact ImGui MIT notice after matching all 15 vendored files to an upstream revision; include the original menu-font OFL notices.
- [x] Stage the identified GLEW, MAME-derived code, JsonCpp, STB, embedded-font and glyph-data notices; add a failing-on-error notice-integrity gate.
- [ ] Preserve upstream licensing, copyright and credits; finish the remaining [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) component and corresponding-source checks.
- [ ] Record dependency versions, provenance and source availability; verify real loader/runtime DLL identities.
- [x] Audit both public files and Git history for ROMs, extracted game assets, game-bearing saves, personal paths/settings, credentials, logs and private captures. Do not upload the development workspace wholesale.
- [x] Add a read-only staged-source policy and synthetic rejection fixtures. This is preventive coverage; historical findings and redistribution rights still require the review above.

## 3. Make a self-contained player package

- [x] Provide clearly named Meta and SteamVR regular-game launchers that work from any extracted folder, including paths with spaces, without editor software, a developer toolchain or old experimental packages.
- [ ] Resolve each supported installed VR runtime without hardcoded developer-machine or Steam paths. Give a useful error if the matching runtime, headset or ROM is unavailable; do not change the system's default runtime.
- [ ] Verify the Meta launcher's temporary ASW-off behavior, exact prior-setting restoration and interrupted-session recovery. Keep HarleyVR scene interpolation enabled independently.
- [x] Include the executable, required real runtime DLLs, clean default configuration, required permitted upstream assets, license texts, README and checksums.
- [ ] Create writable settings, NVRAM and saves for the user on first launch. Include no personal player data and no required starter save state.
- [ ] Start the game from its normal attract flow. Let users supply the documented compatible ROM set; include no ROMs or download links.
- [x] Keep sound enabled for normal user play, while automated validation uses verified muted output with audio emulation enabled.
- [ ] Keep bulk tracing and synthetic-runtime support inactive in normal launch. Bound normal logs and describe where settings/saves are stored.

## 4. Validate outside the development installation

- [x] Build both emulator editions from the standalone source archives using only the documented inputs and dependencies. No old development objects may be silently reused.
- [x] Launch both editions from fresh extractions with clean factory settings.
- [ ] Verify headset-specific error messages across additional machines.
- [x] Make launcher preflight and Meta ASW recovery regression tests runnable from the public checkout with synthetic fixtures. These do not validate the final player package or an installed Meta runtime.
- [x] Exercise every rider in AT and MT through the bounded PSVR2 release replay matrix.
- [ ] Cover remaining manual interactions and full-length rides separately from replay checkpoints.
- [ ] Check first-person default, full bike, independent head movement, stereo depth and theater presentation. Check menus/text occlusion and existing HUD readability.
- [ ] Check crash and landing vibration, vignette response and hills; document remaining false positives rather than claiming they are all solved.
- [ ] Validate the final settings menu and its controls in the headset. Keep game service-menu options distinct from emulator input/audio settings, and preserve existing NVRAM/settings when upgrading.
- [ ] Validate intended free-play defaults only on newly created profiles; keep coin insertion and configurable arcade coin settings available. Do not overwrite existing player choices.
- [ ] Check saving/loading and preservation of existing user progress during upgrades.
- [ ] Run bounded native-state, image, audio and performance checks. Treat desktop/synthetic timing as offline evidence, not fresh physical headset FPS.
- [x] Record the player feedback for the selected PSVR2 and Rift S executables. Final package startup and fresh-machine coverage are tracked separately.
- [ ] Prefer at least one fresh-machine tester before a broad announcement. Label tested headset/runtime combinations explicitly; untested devices remain unverified.

## 5. Publish the GitHub release

[Beta 1](https://github.com/WiseSoundDaryl/harley-supermodel-vr/releases/tag/v0.1.0-beta.1) was downloaded and checked without signing in. All five public files matched the staged checksums. The extracted player contained the same two executables used in the headset playtests. [Source-integrity workflow](https://github.com/WiseSoundDaryl/harley-supermodel-vr/actions/runs/44816316232)

- [x] Publish the project page at WiseSoundDaryl/harley-supermodel-vr.
- [x] Identify the two edition-specific source archives and confirm their clean Windows rebuilds against the selected player executables.
- [x] Create a clean source commit/tag and a release ZIP matching that source. Keep development objects, tools installations, captures and historical binaries out of the repository.
- [ ] Update build automation for the VR dependencies and verify it from the public source layout before claiming automated builds work.
- [x] Write short installation steps, controls, tested setup, known limitations, troubleshooting and source/license links. Give users a useful bug-report template that excludes ROMs and private files.
- [x] Attach the player ZIP and checksum file to a GitHub Release; make corresponding source available from the same release context.
- [x] Review the exact draft repository/release contents before publishing. Creating this local staging folder does not publish anything.
- [x] Download the published asset into a fresh folder and verify its hash and contents before announcing it.

GitHub's official instructions: [creating releases](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository), [about releases](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases).

## Evidence to retain privately

Keep compact reports, hashes and the useful rollback build. Remove redundant generated test copies only after comparisons and within the workspace's preservation rules. Do not publish private logs or raw game-bearing captures as a substitute for a public test summary.
