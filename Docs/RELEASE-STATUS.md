# Release status

**The first public Windows release is in preparation. There is no player download yet.**

HarleyVR's first release focuses on regular arcade gameplay. The game has been played through in development, and both PSVR2 and Rift S have been playtested on Windows.

## Tested headsets

| Headset | Runtime | Status |
| --- | --- | --- |
| PSVR2 with Sony's official PC adapter | SteamVR / OpenXR | Playtested during development |
| Rift S with Touch controllers | Direct Meta OpenXR | Playtested with smooth riding, head tracking, controls and crash/landing vibration |
| Quest 2 and other PC VR headsets | — | Not yet tested |

The Meta launcher temporarily disables Asynchronous Spacewarp during play and restores the previous setting on exit. HarleyVR's own scene interpolation remains active. Performance depends on the PC, runtime and rendering resolution.

## Latest improvements

- Direct Meta support for Rift S, with separate Meta and SteamVR launchers.
- Controller vibration for wall/car impacts and hard jump landings.
- Improved motion for supported scene objects and text depth in rider selection and credits.
- A shared comfort vignette and virtual-theater presentation for flat screens.

## Coming next: the ride menu

The new [ride menu](MENU.md) is ready for headset testing. It brings together ride controls, comfort settings, camera position, rendering quality, sound and access to the original arcade settings.

The menu uses the game's original closing artwork from the player's ROM and saves preferences separately from existing Supermodel settings. Changes that need a restart are labelled **Next launch**. Original cabinet settings, including Free Play, remain under the player's control.

The [menu screenshots](../README.md#the-ride-menu--in-development) show interface previews. The menu has not yet been playtested in a headset.

## Road to release

| Work | Status |
| --- | --- |
| Regular arcade gameplay on PSVR2 and Rift S | Playtested in development |
| Ride menu | Headset testing next |
| Portable Meta and SteamVR launchers | Final package testing pending |
| Complete VR source and standalone build instructions | Integration in progress |
| Windows player ZIP | Packaging in progress |
| Final installation and headset checks | Pending |
| Public beta download | Not yet available |

Dream Ride, hands/walking and alternate bike-mounted instruments remain separate experiments. macOS, Linux and standalone headset builds are outside the first release's scope. Steering-wheel force feedback is not implemented for Harley; VR controller vibration is a separate feature.

Follow the [project page](../README.md) for screenshots and controls, or see the [release checklist](RELEASE-CHECKLIST.md) and [build documentation](BUILDING.md) for contributor details.
