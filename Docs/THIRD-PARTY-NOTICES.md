# Third-party notices and source inventory

**Draft inventory: redistribution review is not complete.** The copied license texts below are preserved verbatim from the inspected dependency installation. Their hashes are listed in `licenses/SHA256.json`. Their presence does not by itself establish that every notice or corresponding-source obligation has been satisfied.

## Supermodel and this modification

Supermodel is copyright its contributors and is distributed under GNU GPL version 3 or, where stated in its source notices, any later version. Retain the upstream `LICENSE.txt`, user manual and component notices supplied with this distribution. Clearly identify this as an unofficial modification of [Supermodel](https://github.com/trzy/Supermodel), with no claim of endorsement by its developers, Sega, Harley-Davidson or headset manufacturers.

Provide the complete corresponding modified source and the build/install scripts for the released executable. A link to unmodified upstream source is insufficient for this fork. Optional code compiled into the executable remains part of the corresponding-source review even if the regular-game launcher does not enable it. The practical release plan is a tagged source revision/source archive paired with the downloadable Windows package. See [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html), particularly sections 4–6, and [BUILDING.md](BUILDING.md).

## External dependencies identified

| Component | Use in the inspected build | License text staged here | Remaining review |
| --- | --- | --- | --- |
| OpenXR SDK loader | Dynamically loaded real OpenXR loader | `licenses/OpenXR-Apache-2.0.txt` | Confirm the copied loader's exact package provenance and any additional notices required by its build. |
| GCC runtime / libstdc++ | Runtime dependencies shipped beside the loader/executable | `licenses/GCC-GPL-3.0.txt`, `licenses/GCC-Runtime-Library-Exception-3.1.txt` | Confirm applicability and exact versions for each shipped DLL. Do not remove the Runtime Library Exception. |
| winpthreads | Runtime dependency | `licenses/winpthreads-COPYING.txt` | Preserve the complete copyright and permission notices. |
| MinGW-w64 CRT | Runtime code linked into the executable | `licenses/MinGW-w64-runtime-notices.txt` | Retain the composite notices applicable to the linked runtime. |
| SDL2 | Statically linked window, input and audio support | `licenses/SDL2-LICENSE.txt` | Preserve source notices; record exact source/package version. |
| SDL2_net | Statically linked upstream networking support | `licenses/SDL2_net-LICENSE.txt` | Preserve source notices; record exact source/package version. |
| zlib | Statically linked decompression support | `licenses/zlib-LICENSE.txt` | Preserve source notices; record exact source/package version. |

The inspected versions are recorded in [BUILDING.md](BUILDING.md). Review source availability for the actual linked dependencies as well as their notices; a notices folder is not a replacement for complete corresponding source where required.

Primary project references: [Khronos OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK), [GCC Runtime Library Exception](https://www.gnu.org/licenses/gcc-exception-3.1.html), [SDL2](https://github.com/libsdl-org/SDL/tree/SDL2), [SDL_net](https://github.com/libsdl-org/SDL_net), [zlib](https://zlib.net/zlib_license.html), [MinGW-w64](https://www.mingw-w64.org/).

## Components embedded in upstream source

Preserve upstream source notices and the upstream manual's acknowledgments. The inspected source/link list includes GLEW, minizip, tinyxml2, Dear ImGui, minimp3 and CPU-emulation components, among others. This list is not a completed license audit of every source file.

- **GLEW:** the upstream Supermodel manual carries copyright, redistribution conditions and disclaimer, including a binary-redistribution notice requirement. Keep that text in the release materials.
- **Dear ImGui:** the source identifies itself as `1.92.6 WIP` and references the standard MIT license. The referenced `LICENSE.txt` is absent from the inspected vendored directory. **Obtain and verify the complete license/copyright text for the actual vendored revision before release.** Do not assume a later release's copyright notice is the exact one required here. [Primary source](https://github.com/ocornut/imgui).
- **tinyxml2:** its header contains its permissive license and origin notices. Preserve those notices in the source distribution. [Primary source](https://github.com/leethomason/tinyxml2).
- **minimp3:** its header declares a public-domain dedication under CC0. Preserve that declaration and source provenance. [Primary source](https://github.com/lieff/minimp3).
- **CPU-emulation and other embedded code:** retain the existing component notices and any upstream relicensing notes. Do not replace them with a blanket statement claiming original authorship of the entire emulator.

## Game content and branding

No permission to distribute the original game's ROMs, music, extracted textures/models, save states containing game data or other game content is granted by the emulator's software license. This release should contain the modified emulator and permitted supporting files only. Players supply their own legally obtained compatible ROM set. Do not include ROM download links.

Screenshots, videos, logos and other promotional material require a separate content review; software licensing does not automatically cover them. Keep project naming and descriptions clear that this is an unofficial community modification.

## Before publishing

Finish the component inventory, resolve the missing ImGui license text, pair source with the final binary, verify all shipped DLL provenance, and review the source/archive exclusion list. No public-compliance certification is being claimed by this draft.
