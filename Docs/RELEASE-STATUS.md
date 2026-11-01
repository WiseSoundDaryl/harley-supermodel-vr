# Release status

**First public release: in preparation. No player download is available yet.**

The main game has been played through in development, including positive PSVR2 feedback. **Rift S compatibility is also confirmed in Windows development playtests through direct Meta OpenXR**, with smooth motion, working controls, and physically confirmed crash/landing controller vibration. A development playtest does not replace validation of the final portable player package.

## November 1 development update

- **Rift S:** the direct Meta runtime path and temporary ASW-off setting produced smooth wearer-reported motion without the earlier deformation or wobble. HarleyVR's own 3D scene interpolation remains enabled.
- **Crash feedback:** wall and car impacts now use existing completed game collision-response signals instead of relying only on sudden slowing. The wearer confirmed that these impacts vibrate correctly and smoothness remains intact. Hard-landing feedback stays strong; engine vibration remains off.
- **Frame delivery:** the two successful Rift S sessions averaged 79.983 and 79.958 application frame submissions per second at an 80 Hz target. These are application measurements and positive wearer reports, not proof of a perfectly fresh physical frame at every refresh.
- **Presentation:** current development work includes improved motion matching for eligible scene objects, selection/credits text-depth handling, a shared binocular comfort vignette and theater presentation for flat screens. Original game visibility limits and some hill-triggered vignette events remain.

The successful Rift S path differs from the earlier SteamVR-to-Oculus test in runtime route, ASW policy, driver and actual eye resolution. The improvement is not attributed to ASW alone. Quest headsets and other runtime routes remain unvalidated.

## Separate menu candidate

The new ride menu has **passed its CPU, preference-file and actual stereo-renderer checks; it is not wearer-tested or published as a player ZIP**. The successful main-game build remains separate. Its interface previews show the actual menu rasterizer; they are not headset recordings or frame-rate measurements.

- **Ride controls:** resume, start, insert coin, recenter and confirmed exit; keyboard and supported VR-controller navigation.
- **Comfort and camera:** vignette on/off and strength, rider height/pullback, virtual-screen width and desktop-only FOV. Headset lens FOV remains controlled by the OpenXR runtime.
- **Display and audio:** 50/75/100% headset render-scale presets for the next launch; live master volume; music levels 0–8 for the next launch on the verified supported ROM revision.
- **Physical steering:** travel adjustment for a single supported, already-mapped wheel/joystick axis. This does not change the VR thumbstick or establish G29, macOS or wheel force-feedback compatibility.
- **Arcade cabinet:** access to the game's real Test/Service menu, including instructions for Coin Assignments #27 / Free Play. Existing cabinet settings are not silently overwritten and Free Play is not claimed as the default.
- **Original presentation:** the three-rider closing image is decoded from the player's ROM at load time. The player package does not ship an extracted copy of that game asset.

Menu preferences use a separate sidecar instead of rewriting the original Supermodel configuration. Settings that require a restart are labelled **Next launch**; camera offsets apply when resuming. See the [menu guide](MENU.md) for the complete controls and scope.

## Public release work

| Item | Status |
| --- | --- |
| Repository identity and presentation | Published at WiseSoundDaryl/harley-supermodel-vr |
| Real screenshots, controls and player documentation | Prepared |
| PSVR2 development compatibility | Playtested through SteamVR with Sony's official PC adapter |
| Rift S development compatibility | Playtested through direct Meta OpenXR; controls and impact/landing vibration confirmed |
| Portable Meta and SteamVR launchers | In development; incomplete packages deliberately cannot launch |
| In-game settings and arcade-service access | Separate candidate passed offline checks; no headset playtest or public player package yet |
| Complete modified source integrated into the public fork | Pending |
| Clean build without retained development objects | Pending |
| Required library notices and provenance audit | Identified omissions restored, including nested dependencies; notice-integrity gate added. Final source/package review remains open. |
| Player ZIP with clean defaults | Pending |
| Fresh-folder checks and consolidated final headset playtest | Pending |
| Tagged downloadable beta | Not published |

Only regular arcade play is targeted. Dream Ride, hands/walking and alternate HUD previews are excluded from the player-facing release.

Windows 64-bit remains the target. No macOS VR build, Logitech G29 wheel-range control or wheel force-feedback compatibility is claimed by these headset tests. Controller vibration and steering-wheel force feedback are separate systems.

Existing upstream code or automated artifacts in the fork must not be mistaken for a validated HarleyVR release. Release publication remains disabled while the [release checklist](RELEASE-CHECKLIST.md) is open.
