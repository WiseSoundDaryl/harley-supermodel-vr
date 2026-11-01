# Troubleshooting

These notes describe the intended first Windows release. A public player ZIP has not been published yet.

## The source download does not launch

GitHub's source-code archive is not a player package. Wait for a named Windows player ZIP on the Releases page. The launcher scaffold deliberately rejects packages that have not passed release validation.

## Missing game

Place your own compatible Revision B archive at `ROMs/harleyb.zip` in the extracted player folder. Do not unzip or rename an unrelated set. No game files or ROM download links are supplied by this project.

## OpenXR or SteamVR error

Check that the headset works in SteamVR first. Select SteamVR as your active OpenXR runtime, then open SteamVR before launching the game. Use the headset manufacturer's normal setup instructions. The launcher reads the installed runtime; it does not overwrite system settings.

## The view is off-center

Sit in your playing position, look ahead and press the right stick or keyboard Home. The regular camera follows the bike's heading; head tracking lets you look independently within that view.

## Uneven motion

Record the release version, CPU/GPU, headset, runtime and render resolution. Describe whether the whole view stutters or only traffic/rotors step. Try the release defaults before changing several settings at once. Report reproducible locations. No locked frame rate is promised across all hardware.

## Missing roadside objects

Some traffic, pedestrians and scenery cease to be supplied by the original arcade game outside its expected camera view. Raising headset resolution cannot restore geometry the game has omitted.

## Saves and upgrades

The planned portable package keeps settings in `Config`, player progress in `Saves` and arcade settings in `NVRAM`. Back up those folders before replacing a release. Save-state compatibility across versions is not guaranteed. Do not upload those folders publicly.

## Reporting a problem

Use the repository bug-report form. Include the exact version and reproducible steps. Share only a reviewed, relevant log excerpt with personal paths removed. Never upload ROMs, save states, tokens or private account data.
