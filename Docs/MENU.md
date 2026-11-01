# HarleyVR ride menu

**Candidate documentation.** This menu is being validated separately from the successful main build. It has not had a headset playtest and is not yet available in a published player ZIP. The screenshots below are CPU-rendered interface previews, not headset captures.

<p align="center"><img src="Images/menu-main.png" width="840" alt="HarleyVR ride menu with the original game's three-rider closing image" /></p>

The ride pauses while the menu is open. Resume returns to the current game; Start sends the game's start/confirm input, and Insert coin adds a credit when needed. Recenter places the view and menu in front of you. Exit asks for confirmation.

## Opening and navigating

| Action | Keyboard | VR controllers |
| --- | --- | --- |
| Open / close panel | Tab or Pause | Menu / Options, if delivered by the runtime |
| Move between rows | Up / Down | Right stick up / down |
| Adjust selected setting | Left / Right | Left stick left / right |
| Select / confirm | Enter | A / Cross |
| Back within this menu | Backspace or Escape | B / Circle |

Escape opens the candidate's menu during riding. Its Back action belongs to this menu; the original arcade game still has its own controls. Release navigation controls before resuming so a held selection does not also act on the game. If a headset reserves its menu button for system software, use keyboard Tab.

## Comfort, camera and display

<p align="center"><img src="Images/menu-comfort.png" width="740" alt="Comfort page with vignette enable and strength settings" /></p>

| Setting | What it changes | Takes effect |
| --- | --- | --- |
| Comfort vignette | Enables or disables the comfort mask; retains the chosen strength | During this session |
| Vignette strength | Scales the existing event-based mask from 0–100% | During this session |
| Rider height / camera pullback | Changes the seated camera offset, in game units rather than metres | On Resume |
| Virtual screen width | Changes the apparent width of theater screens, from 40–100 degrees | During this session |
| Desktop FOV | Changes the desktop preview's field of view | On Resume; unavailable in headset sessions |
| Headset render quality | 50% Performance, 75% Balanced or 100% Quality of the runtime's recommended eye dimensions | **Next launch** |

Render quality changes resolution, not headset refresh rate. Higher values cost more GPU time. The headset's projection and lens FOV remain runtime-controlled. Turning off the vignette does not remove the dark surroundings of a theater screen or change camera stabilization. Existing motion-event detection, including occasional hill triggers, is unchanged by these controls.

## Sound

<p align="center"><img src="Images/menu-audio.png" width="740" alt="Things that Go Vroom audio menu with master sound and arcade music level" /></p>

**Master sound** changes overall output immediately, including music. **Music level** uses the original game's level scale: **0 / Game setting** retains the cabinet choice, while **1–8** runs from quieter to louder. Music-level changes apply on the **next launch**, only for the ROM revision whose support has been verified; the control is unavailable otherwise. It is not an independent percentage-volume slider.

## Physical wheel or joystick travel

With a supported physical steering axis already assigned, lower **Steering travel** to reach full steering with less physical movement. For example, 50% asks the emulator to reach full input sooner than 100%; it does not reprogram a wheel's hardware rotation stops. The control respects the axis's existing dead zone.

This setting is unavailable when no supported axis is mapped or the binding is ambiguous. It does not change the VR-controller thumbstick. Wheel mapping, Logitech G29 behavior, hardware wheel range and wheel force feedback still need their own validation. **This is not a macOS HarleyVR compatibility claim.** Controller vibration is separate from wheel force feedback.

## Original arcade settings and Free Play

Choose **Arcade settings → Enter arcade settings** to send the game's Test input. While arcade controls are active:

| Function | Keyboard | VR controllers |
| --- | --- | --- |
| Test / select | 6 | A / Cross |
| Service / next item | 5 | X / Square |

To choose Free Play using the original cabinet menu:

1. Open **COIN ASSIGNMENTS**.
2. Use Service to reach **COIN/CREDIT SETTING**, then Test to select **#27 / FREE PLAY**.
3. Select **EXIT** in each original arcade menu to save and return to the game.
4. Reopen the HarleyVR panel and leave arcade controls to restore the normal riding mapping.

Leaving the HarleyVR arcade-control mapping does **not** exit the game's test menu. Use the game's own EXIT options first. Free Play is documented in the original [SEGA owner's manual](https://www.arcade-museum.com/manuals-videogames/H/Harley-Davidson_00004.pdf), printed pages 26–27. Existing NVRAM is preserved unless you deliberately change cabinet settings; Free Play is not silently enabled.

For the original region selector, open **GAME ASSIGNMENTS**, then enter **Shift up, Shift up, Shift down, Shift down, View, Music, View, Music**. The original menu handles the region choice. These inputs remain available while arcade controls are active.

## Preferences and artwork

Interactive menu changes are stored in **`Config/HarleyVRMenu.ini`** for the selected profile. The menu does not rewrite the original `Supermodel.ini`, ROM archive or save slots. Its sidecar is verified before replacement; if saving fails, the existing file remains intact and the pending changes remain unsaved.

The closing three-rider picture is the game's authentic artwork, decoded locally from the player's ROM at load time. No extracted background file is shipped in the player package. The menu's “Feel the freedom only a Harley can give.” and “Things that Go Vroom...” lines come from the original flyer presentation. The pictured interface remains a development candidate; follow [release status](RELEASE-STATUS.md) for package and headset validation.
