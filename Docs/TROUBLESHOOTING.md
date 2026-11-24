# Troubleshooting

These instructions cover the SteamVR and Meta PC VR editions for Windows.

## The source download does not launch

GitHub's source-code archive is for developers. Download the **Windows player ZIP** from the [Releases page](https://github.com/WiseSoundDaryl/harley-supermodel-vr/releases).

## Missing game

Beta 1 looks for `ROMs/harleyb.zip` in the extracted player folder. The game is **Harley-Davidson & L.A. Riders (Export, Revision B)**. If your complete standalone Revision B archive is named `harley.zip`, make a copy in `ROMs` named `harleyb.zip`. Keep it zipped. Revision A and mixed-revision archives are unsuitable for this beta.

## Choose your VR runtime

**SteamVR.** Start SteamVR, confirm it sees your headset and controllers, then open `HarleyVR-SteamVR.exe`. For a PlayStation VR2 headset, first complete setup in Sony’s PlayStation VR2 App with its PC adapter.

**Meta PC VR.** Open Meta Horizon Link on the PC. On Quest, enter a wired Quest Link or wireless Air Link session in the headset. Once connected, close SteamVR and launch `HarleyVR-Meta.exe`. If you are using the Steam Link app in your headset, use `HarleyVR-SteamVR.exe` instead. If the direct Meta launcher fails, include its exact message, your headset model and your connection method when reporting it.

See [tested hardware](RELEASE-STATUS.md) for the headsets used in playtests.

## Distorted images or uneven motion in Meta PC VR

If the beta 2 headset graphics menu fails to appear, open **HarleyVR.exe** on the PC, choose **Meta graphics on this PC**, pick your preset and start the game. This skips the headset menu and still requires an active Meta PC VR connection. **Balanced (75%)** matches the original Meta riding preset. Mention whether the headset or desktop path worked when reporting the issue.

The Meta launcher temporarily disables **Asynchronous Spacewarp (ASW)** during play and restores its previous setting after normal exit. HarleyVR's own scene interpolation stays enabled to smooth motion between the arcade game's frames.

Start with the Meta launcher and its default settings. If an interrupted session leaves a restoration message, keep the recovery information and follow the launcher's instructions before changing ASW manually.

## The view is off-center

Sit in your playing position, look ahead and click the right stick or press keyboard **Home** with the game window focused. In the SteamVR edition, this also repositions the ride menu when it is open. The regular camera follows the bike's heading as you look around.

## The game appears upside down

One Quest player reported an upside-down road and HUD on the first SteamVR launch. Exiting and reopening HarleyVR restored the view. If this happens to you, exit the game. Before relaunching, keep a copy of `Supermodel.log` from the game folder. When reporting it, mention whether the view was inverted inside the headset, in the desktop VR View, or both, and whether the SteamVR dashboard appeared upright.

## The SteamVR image looks soft

Open **Graphics → Render quality** in the startup menu. Select **Quality (100%)**, then **Back → Enter game**. If Quality causes stutter, try **Balanced (75%)**. A fresh install uses **Performance (50%)**. Each percentage scales the width and height of each eye from SteamVR’s recommended size. If Quality still looks soft, include the two `OpenXR eye` lines from the gameplay session’s `Supermodel.log` and how the headset connects to the PC.

## Opening or leaving the ride menu

In the **SteamVR edition**, focus the game window and press **Escape**, **Tab** or **Alt+P** to open the ride menu. Escape/Backspace goes back one page or closes the main page. Tab/Alt+P toggles the whole panel. Choose **Exit game** and confirm to quit.

On a controller, click the **left stick**, or hold the **left side grip** and click the **right stick**. SteamVR handles the system button. See the [menu guide](MENU.md#opening-and-navigating) for controller navigation and the cabinet menu's **EXIT** procedure.

In the **Meta PC VR edition**, **Escape** exits the game and **Alt+P** pauses or resumes it.

## Uneven motion

In the SteamVR edition, a lower **Graphics → Render quality** preset at startup can reduce GPU load. If the problem continues, note whether the whole view stutters or only nearby vehicles or helicopter rotors appear jerky. Include the location, headset, CPU/GPU and render quality in your report.

## Controller vibration and wheel force feedback

Controller vibration responds to detected crashes and hard jump landings. Engine rumble is disabled, and some minor contacts may not trigger a pulse.

HarleyVR currently has no wheel force feedback. The SteamVR ride menu’s [Steering travel setting](MENU.md#physical-wheel-or-joystick-travel) adjusts how far a mapped physical axis must move to reach full steering. The wheel driver controls its hardware rotation range. The Logitech G29 still needs testing.

## Missing roadside objects

The arcade game stops drawing some traffic, pedestrians and scenery outside its original camera view. These objects can disappear as you look to the side or pass them. Increasing resolution will not restore them.

## Saves and upgrades

The Windows player package keeps settings in `Config`, progress in `Saves` and cabinet settings in `NVRAM`. Back up all three folders before upgrading. If the Meta PC VR launcher reports pending ASW restoration, preserve its recovery journal and complete recovery before replacing the old installation. Save states may not work across different versions. Keep these folders private.

## Reporting a problem

For beta 1, reply in the community thread where HarleyVR was shared, such as the [public launch thread on r/VRGaming](https://www.reddit.com/r/VRGaming/comments/1y1gvm4/harleydavidson_la_riders_pcvr_mod_out_now/). If you received a test build directly, you can also send your report to the person who shared it.

Include the build, headset, runtime, render preset, headset refresh rate (if known), PC specifications and steps to reproduce the problem. For a riding hitch, include the rider and the road or landmark where it happened. Remove personal paths from log excerpts. Never upload ROMs, save states or private account data.
