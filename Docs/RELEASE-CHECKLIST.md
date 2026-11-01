# First public release checklist

Scope: a Windows 64-bit **regular-game** VR release. Dream Ride, hands/walking and the alternate bike HUD are not presented as first-release features. Optional code that remains compiled into the binary still belongs in its corresponding source.

This checklist is intentionally open. A local staging folder is not a published or release-validated package. Record evidence and the exact candidate hash when closing each gate.

## 1. Freeze the release candidate

- [ ] Choose the exact regular-game executable and source revision; preserve the successful previous build unchanged.
- [ ] Record executable, four real runtime DLL and configuration hashes.
- [ ] Select one release source/binary baseline and record which headset/runtime combinations were tested with that exact package. Preserve the successful November 1 Rift S collision-feedback build and the previous PSVR2 references.
- [ ] Keep the familiar steering camera and regular controls. Do not accidentally enable experimental hands/walking, alternate HUD, rejected camera filters or diagnostic capture.
- [ ] Give the release an honest version such as a beta/prerelease until its release gates pass. Do not advertise a locked 120 FPS or universal headset compatibility.

## 2. Complete source and licensing

- [ ] Prepare the complete corresponding modified source, including required files that were previously untracked and optional code compiled into the selected executable.
- [ ] Include the scripts and declared inputs needed to build and install it; resolve the retained-object/profile-guided-build dependency described in [BUILDING.md](BUILDING.md).
- [ ] Preserve upstream licensing, copyright and credits; finish [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md), including the missing full ImGui license text.
- [ ] Record dependency versions, provenance and source availability; verify real loader/runtime DLL identities.
- [ ] Audit both public files and Git history for ROMs, extracted game assets, game-bearing saves, personal paths/settings, credentials, logs and private captures. Do not upload the development workspace wholesale.

## 3. Make a self-contained player package

- [ ] Provide clearly named Meta and SteamVR regular-game launchers that work from any extracted folder, including paths with spaces, without editor software, a developer toolchain or old experimental packages.
- [ ] Resolve each supported installed VR runtime without hardcoded developer-machine or Steam paths. Give a useful error if the matching runtime, headset or ROM is unavailable; do not change the system's default runtime.
- [ ] Verify the Meta launcher's temporary ASW-off behavior, exact prior-setting restoration and interrupted-session recovery. Keep HarleyVR scene interpolation enabled independently.
- [ ] Include the executable, required real runtime DLLs, clean default configuration, required permitted upstream assets, license texts, README and checksums.
- [ ] Create writable settings, NVRAM and saves for the user on first launch. Include no personal player data and no required starter save state.
- [ ] Start the game from its normal attract flow. Let users supply the documented compatible ROM set; include no ROMs or download links.
- [ ] Keep sound enabled for normal user play, while automated validation uses verified muted output with audio emulation enabled.
- [ ] Keep bulk tracing and synthetic-runtime support inactive in normal launch. Bound normal logs and describe where settings/saves are stored.

## 4. Validate outside the development installation

- [ ] Build from a fresh checkout using only the documented source/dependencies. No old development objects may be silently reused.
- [ ] Extract the proposed player ZIP into a fresh folder and run without the development workspace being required.
- [ ] Confirm missing-ROM/runtime/headset messages and orderly shutdown.
- [ ] Check attract screen, coin/start, building intro, every rider and transmission choice, gameplay, pause/recenter, controller exit, game-over/restart, ending and credits.
- [ ] Check first-person default, full bike, independent head movement, stereo depth and theater presentation. Check menus/text occlusion and existing HUD readability.
- [ ] Check crash and landing vibration, vignette response and hills; document remaining false positives rather than claiming they are all solved.
- [ ] Validate the final settings menu and its controls in the headset. Keep game service-menu options distinct from emulator input/audio settings, and preserve existing NVRAM/settings when upgrading.
- [ ] Validate intended free-play defaults only on newly created profiles; keep coin insertion and configurable arcade coin settings available. Do not overwrite existing player choices.
- [ ] Check saving/loading and preservation of existing user progress during upgrades.
- [ ] Run bounded native-state, image, audio and performance checks. Treat desktop/synthetic timing as offline evidence, not fresh physical headset FPS.
- [ ] Arrange one consolidated final candidate playtest at the user's convenience. Do not require continuous wear during development. Record the exact package, hardware/runtime and feedback.
- [ ] Prefer at least one fresh-machine tester before a broad announcement. Label tested headset/runtime combinations explicitly; untested devices remain unverified.

## 5. Prepare the GitHub release

- [x] Publish the project page at WiseSoundDaryl/harley-supermodel-vr.
- [ ] Confirm the complete modified source tree for the selected release; the published project page is not the VR implementation.
- [ ] Create a clean source commit/tag and a release ZIP matching that source. Keep development objects, tools installations, captures and historical binaries out of the repository.
- [ ] Update build automation for the VR dependencies and verify it from the public source layout before claiming automated builds work.
- [ ] Write short installation steps, controls, tested setup, known limitations, troubleshooting and source/license links. Give users a useful bug-report template that excludes ROMs and private files.
- [ ] Attach the player ZIP and checksum file to a GitHub Release; make corresponding source available from the same release context.
- [ ] Review the exact draft repository/release contents before publishing. Creating this local staging folder does not publish anything.
- [ ] Download the published asset into a fresh folder and verify its hash and contents before announcing it.

GitHub's official instructions: [creating releases](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository), [about releases](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases).

## Evidence to retain privately

Keep compact reports, hashes and the useful rollback build. Remove redundant generated test copies only after comparisons and within the workspace's preservation rules. Do not publish private logs or raw game-bearing captures as a substitute for a public test summary.
