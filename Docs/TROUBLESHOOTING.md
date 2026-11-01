# Troubleshooting

HarleyVR's first public Windows release is in preparation. These notes cover setup and common questions; check [release status](RELEASE-STATUS.md) for availability.

## The source download does not launch

GitHub's source-code archive is for developers. When the public release is available, download the **Windows player ZIP** from the [Releases page](https://github.com/WiseSoundDaryl/harley-supermodel-vr/releases).

## Missing game

Place your own compatible Revision B archive at `ROMs/harleyb.zip` in the extracted player folder. Do not unzip or rename an unrelated set. No game files or ROM download links are supplied by this project.

## Choose the correct headset route

**PSVR2:** confirm the headset works with Sony's official PC adapter, the PlayStation VR2 App and SteamVR. Open SteamVR before using the SteamVR launcher.

**Rift S:** confirm the headset works in Meta Horizon Link first. Leave Meta Horizon Link open and SteamVR closed, then use the **Meta** launcher.

Follow your headset manufacturer's setup instructions before launching HarleyVR. Other headsets have not yet been tested.

## Rift S deformation or uneven motion

The Meta launcher temporarily disables **Asynchronous Spacewarp (ASW)** during play and restores its previous setting after normal exit. HarleyVR's own scene interpolation stays enabled to smooth motion between the arcade game's frames.

Start with the Meta launcher and its default settings. If an interrupted session leaves a restoration message, keep the recovery information and follow the launcher's instructions before changing ASW manually.

## The view is off-center

Sit in your playing position, look ahead and press the right stick or keyboard Home. The regular camera follows the bike's heading; head tracking lets you look independently within that view.

## Uneven motion

Start with the default settings. In the menu preview, a lower **Headset render quality** setting can reduce GPU load after restarting the game. If the problem continues, note whether the whole view stutters or only nearby vehicles or helicopter rotors appear jerky. Include the location, headset, CPU/GPU and render quality in your report.

## Controller vibration and wheel force feedback

Controller vibration responds to detected crashes and hard jump landings. Engine rumble is disabled, and some minor contacts may not trigger a pulse.

**Wheel force feedback is not implemented for HarleyVR.** The upcoming menu's [Steering travel setting](MENU.md#physical-wheel-or-joystick-travel) adjusts a mapped physical axis, but does not change a wheel's hardware rotation range. G29 support remains untested, and HarleyVR currently targets Windows.

## Missing roadside objects

The arcade game stops drawing some traffic, pedestrians and scenery outside its original camera view. These objects can disappear as you look to the side or pass them. Increasing resolution will not restore them.

## Saves and upgrades

The planned player package keeps settings in `Config`, progress in `Saves` and cabinet settings in `NVRAM`. Back up all three folders before upgrading. Save states may not work across different versions. Keep these folders private.

## Reporting a problem

Use the repository's [bug-report form](https://github.com/WiseSoundDaryl/harley-supermodel-vr/issues/new?template=bug_report.yml). Include the version, headset, PC specifications and steps to reproduce the problem. Remove personal paths from log excerpts. Never upload ROMs, save states or private account data.
