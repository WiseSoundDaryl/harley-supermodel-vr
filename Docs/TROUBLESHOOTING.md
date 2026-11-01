# Troubleshooting

These notes describe the intended first Windows release. A public player ZIP has not been published yet.

## The source download does not launch

GitHub's source-code archive is not a player package. Wait for a named Windows player ZIP on the Releases page. The launcher scaffold deliberately rejects packages that have not passed release validation.

## Missing game

Place your own compatible Revision B archive at `ROMs/harleyb.zip` in the extracted player folder. Do not unzip or rename an unrelated set. No game files or ROM download links are supplied by this project.

## Choose the correct headset route

**PSVR2:** confirm the headset works with Sony's official PC adapter, the PlayStation VR2 App and SteamVR. Open SteamVR before using the SteamVR launcher.

**Rift S:** confirm the headset works in Meta Horizon Link first. Leave Meta Horizon Link open and SteamVR closed, then use the direct Meta launcher. The successful Rift S development path does not run through SteamVR.

The portable launchers are being prepared to select the matching installed OpenXR runtime for the game process without overwriting the system default. Use the headset manufacturer's normal setup instructions; OpenXR support alone does not establish compatibility for an untested headset.

## Rift S deformation or uneven motion

The successful direct Meta configuration temporarily disables Meta PC Asynchronous Spacewarp (ASW), verifies the setting, and restores its prior value after normal exit. HarleyVR's own scene interpolation stays on: it interpolates 3D scene motion and is separate from Meta's generated-frame smoothing. Do not treat those two switches as interchangeable.

Use the matching launcher and release defaults before changing several settings at once. If a release reports that restoration needs attention after an interrupted session, keep its recovery information and follow the displayed instructions; do not blindly overwrite a runtime setting used by other games.

## The view is off-center

Sit in your playing position, look ahead and press the right stick or keyboard Home. The regular camera follows the bike's heading; head tracking lets you look independently within that view.

## Uneven motion

Record the release version, CPU/GPU, headset, runtime and render resolution. Describe whether the whole view stutters or only traffic/rotors step. Try the release defaults before changing several settings at once. Report reproducible locations. No locked frame rate is promised across all hardware.

## Controller vibration and wheel force feedback

Rift S Touch vibration has been confirmed for wall/car impacts and hard jump landings in development playtests. Engine rumble is intentionally disabled. Collision-response signals and safety gates determine which events produce a pulse; this is not a promise that every roadside prop will rumble.

That controller feedback does not establish Logitech G29 wheel force-feedback support. Wheel range, analog input tuning and wheel drivers need separate validation. The in-game settings interface is in development, and no macOS VR build or Mac wheel-support result is being advertised.

## Missing roadside objects

Some traffic, pedestrians and scenery cease to be supplied by the original arcade game outside its expected camera view. Raising headset resolution cannot restore geometry the game has omitted.

## Saves and upgrades

The planned portable package keeps settings in `Config`, player progress in `Saves` and arcade settings in `NVRAM`. Back up those folders before replacing a release. Save-state compatibility across versions is not guaranteed. Do not upload those folders publicly.

## Reporting a problem

Use the repository bug-report form. Include the exact version and reproducible steps. Share only a reviewed, relevant log excerpt with personal paths removed. Never upload ROMs, save states, tokens or private account data.
