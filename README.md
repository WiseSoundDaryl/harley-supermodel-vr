<p align="center"><img src="Docs/Images/banner.svg" width="100%" alt="HarleyVR — The arcade ride. A new perspective." /></p>

<p align="center"><img src="Docs/Images/status.svg" width="760" alt="Windows 64-bit · OpenXR · PSVR2 and Rift S playtested · Release in preparation" /></p>

<p align="center"><b><a href="#the-ride">Features</a> · <a href="#screenshots">Screenshots</a> · <a href="#the-ride-menu--in-development">Ride menu</a> · <a href="#getting-started">Setup</a> · <a href="#controls">Controls</a> · <a href="#known-limitations">Known limitations</a> · <a href="Docs/BUILDING.md">For developers</a></b></p>

# HarleyVR

**Experience Harley-Davidson & L.A. Riders from the rider's seat in PC VR.**

Cruise the streets of Los Angeles, look into a corner and choose your ride in 3D. HarleyVR is an unofficial VR conversion built on the [Supermodel emulator](https://github.com/trzy/Supermodel), with stereoscopic depth, independent head tracking and VR controller support.

> **First Windows release in development.** There is no public player download yet. Follow [release status](Docs/RELEASE-STATUS.md) for availability and upcoming work.
>
> **Bring your own game.** HarleyVR requires a legally obtained **Revision B** ROM set, `harleyb.zip`. ROMs are not included; please do not request or upload them here.

## The ride

- **First-person riding:** a raised viewpoint, visible bike and familiar arcade steering.
- **Genuine stereo depth:** separate views for each eye, with independent head tracking to look around as you ride.
- **Immersive rider selection:** explore the bikes and riders in the game's 3D selection scene.
- **Comfort options:** camera stabilization and vignettes for abrupt movement, with theater presentation for intros and flat screens.
- **VR controller support:** steer, accelerate, brake, change view and feel feedback from crashes and hard landings.
- **Smoother scene motion:** interpolation improves movement between game frames while preserving the original arcade simulation speed.

The first release focuses on **regular arcade gameplay**. Experimental walking, free hands, Dream Ride and alternate bike-mounted instruments are outside this release's scope.

## Screenshots

<table>
<tr>
<td width="50%" align="center"><img src="Docs/Images/ride.png" width="410" alt="First-person HarleyVR riding view on a palm-lined Los Angeles street" /><br /><b>The rider's view</b><br />Original arcade scenery and the regular HUD.</td>
<td width="50%" align="center"><img src="Docs/Images/look-around.png" width="410" alt="A head-turned view showing the handlebars and surrounding street" /><br /><b>Look around independently</b><br />The same scene, viewed from a different head pose.</td>
</tr>
<tr>
<td width="50%" align="center"><img src="Docs/Images/rider-selection.png" width="410" alt="A bike and rider in the immersive 3D selection scene" /><br /><b>Choose your ride</b><br />Rider selection stays in the 3D world.</td>
<td width="50%" align="center"><img src="Docs/Images/cinema.png" width="410" alt="AT and MT transmission selection shown on a virtual screen surrounded by darkness" /><br /><b>A screen when it makes sense</b><br />Transmission selection in the virtual theater.</td>
</tr>
</table>

<sub>Single-eye screenshots from the desktop VR renderer. Game visuals belong to their respective owners.</sub>

## The ride menu — in development

**A new in-game menu is in development, with headset playtesting next.** Pause your ride to adjust comfort, camera position, display quality and audio, or open the original arcade settings.

<table>
<tr>
<td width="33%" align="center"><img src="Docs/Images/menu-main.png" width="280" alt="HarleyVR ride menu with the original three-rider closing artwork" /><br /><b>Everything starts here</b><br />Resume, start, insert a coin or recenter.</td>
<td width="33%" align="center"><img src="Docs/Images/menu-comfort.png" width="280" alt="HarleyVR comfort page with vignette on/off and strength controls" /><br /><b>Make the ride yours</b><br />Comfort strength, camera position and display options.</td>
<td width="33%" align="center"><img src="Docs/Images/menu-audio.png" width="280" alt="HarleyVR audio page titled Things that Go Vroom" /><br /><b>Things that Go Vroom...</b><br />Master sound and supported arcade music levels.</td>
</tr>
</table>

<sub>Desktop interface previews. The design uses original game artwork and typography inspired by its arcade-era flyers.</sub>

The menu includes a vignette toggle and strength control, rider height and camera pullback, virtual-screen size, render-quality presets and audio settings. It also provides access to the game's **Test/Service menu**, including Free Play and other cabinet options. Existing arcade settings are preserved until you change them.

Open the preview menu with **Tab**, **Alt+P**, or a supported controller's **Menu/Options** button. See the [menu guide](Docs/MENU.md) for navigation, settings and Free Play setup.

## Getting started

While the public package is being prepared:

1. Set up your headset and controllers using their official PC software.
2. For **Rift S**, open Meta Horizon Link and keep SteamVR closed. For **PSVR2**, open SteamVR.
3. Keep your own compatible **`harleyb.zip`** archive ready. Leave it zipped.

The release will include separate **Meta** and **SteamVR** launchers and installation instructions. The Meta launcher temporarily disables Asynchronous Spacewarp during play and restores the previous setting on exit. HarleyVR's own scene interpolation remains enabled.

### Compatibility

| Setup | Status |
| --- | --- |
| Windows 64-bit + PSVR2 + official PC adapter + SteamVR | Playtested |
| Windows 64-bit + Rift S + Touch controllers + direct Meta OpenXR | Playtested, including crash and landing vibration |
| Quest 2 / other PC VR headsets | Testing pending |
| Linux / macOS / standalone headset installation | Not supported by the first release |

Hardware requirements are still being established. Performance depends on your PC, headset and render resolution.

## Controls

PSVR2 Sense and Rift S Touch mappings for the regular-game configuration:

| PSVR2 Sense | Rift S Touch | Action |
| --- | --- | --- |
| **Left stick** | **Left stick** | Steer / move through rider choices |
| **R2** | **Right trigger** | Accelerate |
| **L2** | **Left trigger** | Brake |
| **Cross** | **A** | Start / confirm |
| **Square** | **X** | Insert coin |
| **Triangle** | **Y** | Change view |
| **Circle** | **B** | Game special / music selection |
| **Right stick up / down** | **Right stick up / down** | Manual gear change |
| **Right stick click** | **Right stick click** | Recenter |
| **Options** | **Menu** | Pause, when the runtime delivers this button to the game |
| **Both side grips, held for 2 seconds** | **Both side grips, held for 2 seconds** | Exit |
| **Keyboard Home / Escape** | **Keyboard Home / Escape** | Recenter / exit |
| **Keyboard F5 / F7** | **Keyboard F5 / F7** | Save / load the selected emulator save slot |

Circle/B controls the game's music/special input. The [menu preview](Docs/MENU.md) uses separate navigation bindings: Escape opens or backs out of that menu. Controller vibration covers crashes and hard landings; headset rumble is not implemented.

## Comfort and presentation

The driving camera softens bumps and impacts, and a binocular vignette narrows peripheral vision during abrupt motion. Rider selection, riding and the ending remain immersive; intros, AT/MT selection and flat screens appear in a dark virtual theater. Start seated, recenter before riding and stop if you feel uncomfortable.

## Known limitations

- **Original visibility limits:** traffic, pedestrians or scenery can disappear when the game stops drawing them outside its original camera view.
- **Object animation:** some vehicle and helicopter components still move at the original game's animation rate.
- **Vignette detection:** hills can occasionally trigger the comfort mask.
- **Text and overlays:** selection and credits depth have been improved; occasional clipping may remain elsewhere.
- **Wheel feedback:** Harley-specific steering-wheel force feedback is not implemented. VR controller vibration is supported. Physical wheel setup, including the Logitech G29, needs further testing.

See [troubleshooting](Docs/TROUBLESHOOTING.md) before reporting a problem.

## Feedback and development

For bug reports, include your build, headset, runtime, PC specifications and steps to reproduce the issue. Remove personal information from logs, and never attach ROMs or save states.

- [Release status](Docs/RELEASE-STATUS.md)
- [Ride menu and arcade settings](Docs/MENU.md)
- [Build preparation and dependencies](Docs/BUILDING.md)
- [Report a bug](https://github.com/WiseSoundDaryl/harley-supermodel-vr/issues/new?template=bug_report.yml)

## Credits and license

- **Supermodel:** [Bart Trzynadlowski and the Supermodel Team](https://github.com/trzy/Supermodel), whose emulator makes this work possible. The [original manual and acknowledgments](Docs/Supermodel-Manual.txt) are retained.
- **HarleyVR:** project direction and playtesting by [WiseSoundDaryl](https://github.com/WiseSoundDaryl), with AI-assisted development.
- **OpenXR:** Khronos Group; see the [dependency notices](Docs/THIRD-PARTY-NOTICES.md).
- **Community inspiration:** [headsplosion's Lost World PC VR project](https://github.com/headsplosion/lost-world-supermodel-pcvr).

Emulator modifications are licensed under **GPL-3.0-or-later**. Third-party components and font data retain their respective licenses. See [LICENSE](LICENSE) and [third-party notices](Docs/THIRD-PARTY-NOTICES.md).

Harley-Davidson, Harley-Davidson & L.A. Riders, SEGA and other names and marks belong to their respective owners. **This is an unofficial fan project, not affiliated with or endorsed by Harley-Davidson, SEGA or Sony.** The emulator's software license does not license the game, its ROMs or its artwork.
