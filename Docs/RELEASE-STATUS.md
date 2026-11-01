# Release status

**First public release: in preparation. No player download is available yet.**

The main game has been played through in development, including positive PSVR2 feedback. **Rift S compatibility is also confirmed in Windows development playtests through direct Meta OpenXR**, with smooth motion, working controls, and physically confirmed crash/landing controller vibration. A development playtest does not replace validation of the final portable player package.

## November 1 development update

- **Rift S:** the direct Meta runtime path and temporary ASW-off setting produced smooth wearer-reported motion without the earlier deformation or wobble. HarleyVR's own 3D scene interpolation remains enabled.
- **Crash feedback:** wall and car impacts now use existing completed game collision-response signals instead of relying only on sudden slowing. The wearer confirmed that these impacts vibrate correctly and smoothness remains intact. Hard-landing feedback stays strong; engine vibration remains off.
- **Frame delivery:** the two successful Rift S sessions averaged 79.983 and 79.958 application frame submissions per second at an 80 Hz target. These are application measurements and positive wearer reports, not proof of a perfectly fresh physical frame at every refresh.
- **Presentation:** current development work includes improved motion matching for eligible scene objects, selection/credits text-depth handling, a shared binocular comfort vignette and theater presentation for flat screens. Original game visibility limits and some hill-triggered vignette events remain.

The successful Rift S path differs from the earlier SteamVR-to-Oculus test in runtime route, ASW policy, driver and actual eye resolution. The improvement is not attributed to ASW alone. Quest headsets and other runtime routes remain unvalidated.

## Public release work

| Item | Status |
| --- | --- |
| Repository identity and presentation | Published at WiseSoundDaryl/harley-supermodel-vr |
| Real screenshots, controls and player documentation | Prepared |
| PSVR2 development compatibility | Playtested through SteamVR with Sony's official PC adapter |
| Rift S development compatibility | Playtested through direct Meta OpenXR; controls and impact/landing vibration confirmed |
| Portable Meta and SteamVR launchers | In development; incomplete packages deliberately cannot launch |
| In-game settings and arcade-service access | In development; not yet a published feature |
| Complete modified source integrated into the public fork | Pending |
| Clean build without retained development objects | Pending |
| Required library notices and provenance audit | In progress |
| Player ZIP with clean defaults | Pending |
| Fresh-folder checks and consolidated final headset playtest | Pending |
| Tagged downloadable beta | Not published |

Only regular arcade play is targeted. Dream Ride, hands/walking and alternate HUD previews are excluded from the player-facing release.

Windows 64-bit remains the target. No macOS VR build, Logitech G29 wheel-range control or wheel force-feedback compatibility is claimed by these headset tests. Controller vibration and steering-wheel force feedback are separate systems.

Existing upstream code or automated artifacts in the fork must not be mistaken for a validated HarleyVR release. Release publication remains disabled while the [release checklist](RELEASE-CHECKLIST.md) is open.
