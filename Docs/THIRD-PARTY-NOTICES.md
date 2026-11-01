# Third-party notices and source inventory

**November 1 notice audit completed; final release review remains open.** Missing notices identified in the inspected build have been restored from the matched source or dependency package. Exact copies and extracted attribution blocks are distinguished in [licenses/SHA256.json](licenses/SHA256.json). Their presence does not establish complete corresponding source or permission to use game imagery and trademarks.

## Supermodel and this modification

Supermodel is copyright its contributors and is distributed under GNU GPL version 3 or, where stated in its source notices, any later version. Retain the upstream `LICENSE.txt`, user manual and component notices supplied with this distribution. Clearly identify this as an unofficial modification of [Supermodel](https://github.com/trzy/Supermodel), with no claim of endorsement by its developers, Sega, Harley-Davidson or headset manufacturers.

Provide the complete corresponding modified source and the build/install scripts for the released executable. A link to unmodified upstream source is insufficient for this fork. Optional code compiled into the executable remains part of the corresponding-source review even if the regular-game launcher does not enable it. The practical release plan is a tagged source revision/source archive paired with the downloadable Windows package. See [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html), particularly sections 4–6, and [BUILDING.md](BUILDING.md).

## External dependencies identified

| Component | Use in the inspected build | License text staged here | Remaining review |
| --- | --- | --- | --- |
| OpenXR SDK loader | Dynamically loaded real OpenXR loader | `licenses/OpenXR-Apache-2.0.txt` | Confirm the copied loader's exact package provenance and any additional notices required by its build. |
| JsonCpp 1.9.6 | Statically included inside the inspected OpenXR loader | `licenses/JsonCpp-1.9.6-LICENSE.txt` | Retain the MIT/public-domain notice with the loader; this dependency has no separate DLL. |
| GCC runtime / libstdc++ | Runtime dependencies shipped beside the loader/executable | `licenses/GCC-GPL-3.0.txt`, `licenses/GCC-Runtime-Library-Exception-3.1.txt` | Confirm applicability and exact versions for each shipped DLL. Do not remove the Runtime Library Exception. |
| winpthreads | Runtime dependency | `licenses/winpthreads-COPYING.txt` | Preserve the complete copyright and permission notices. |
| MinGW-w64 CRT | Runtime code linked into the executable | `licenses/MinGW-w64-runtime-notices.txt` | Retain the composite notices applicable to the linked runtime. |
| SDL2 | Statically linked window, input and audio support | `licenses/SDL2-LICENSE.txt` | Preserve source notices; record exact source/package version. |
| SDL2_net | Statically linked upstream networking support | `licenses/SDL2_net-LICENSE.txt` | Preserve source notices; record exact source/package version. |
| zlib | Statically linked decompression support | `licenses/zlib-LICENSE.txt` | Preserve source notices; record exact source/package version. |

The four runtime DLLs were matched to the installed files and cached MSYS2 package members. Seven historical package recipes were matched to their recorded build metadata. OpenXR 1.1.62's upstream root and installed package supplied no separate NOTICE file; its incorporated JsonCpp notice is now included. The inspected versions are recorded in [BUILDING.md](BUILDING.md). Source availability and delivery for the final package still need review; a notices folder does not replace corresponding source where required.

Primary project references: [Khronos OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK), [GCC Runtime Library Exception](https://www.gnu.org/licenses/gcc-exception-3.1.html), [SDL2](https://github.com/libsdl-org/SDL/tree/SDL2), [SDL_net](https://github.com/libsdl-org/SDL_net), [zlib](https://zlib.net/zlib_license.html), [MinGW-w64](https://www.mingw-w64.org/).

## Components embedded in upstream source

Preserve upstream source notices and the upstream manual's acknowledgments. The November 1 audit checked the selected source/link list and nested components; it is not a certification covering future source changes or every possible build configuration.

- **GLEW:** [complete vendored-header notices](licenses/GLEW-vendored-notices.txt) now supplement the original manual, including Nigel Stewart, Mesa/Brian Paul and Khronos text omitted from its shorter acknowledgment.
- **Dear ImGui:** the missing MIT notice has been restored to `Src/Pkgs/imgui/LICENSE.txt` and [licenses/Dear-ImGui-LICENSE.txt](licenses/Dear-ImGui-LICENSE.txt). All 15 vendored source/header files match the Git blobs at upstream revision [`05581da`](https://github.com/ocornut/imgui/tree/05581da18321bee38f6d5915a1466665cdd66fc8), identifying the bundled `1.92.6 WIP` snapshot. The restored 2014–2025 Omar Cornut notice is byte-for-byte identical to that revision's license. [File-by-file provenance](licenses/Dear-ImGui-SOURCE.json).
- **ImGui's nested components:** retain the [STB dual-license block](licenses/ImGui-STB-dual-license.txt), [embedded-font attributions](licenses/ImGui-embedded-font-attributions.txt), [ProggyClean MIT notice](licenses/ProggyClean-MIT.txt), [ProggyVector terms](licenses/ProggyVector-LICENSE.txt), [gl3w dedication](licenses/ImGui-gl3w-public-domain.txt), and [Japanese glyph-list CC BY 4.0 attribution](licenses/ImGui-Japanese-glyph-list-attribution.txt). ProggyVector includes Bitstream Vera terms and DejaVu public-domain material; these are not replaced by ImGui's MIT license. The glyph-list terms concern that data, not the surrounding emulator code.
- **MAME-derived components:** retain both the [BSD-3-Clause terms](licenses/MAME-BSD-3-Clause.txt) and [specific copyright holders](licenses/MAME-component-attributions.txt), including the security-chip code. Existing local source notices remain intact.
- **tinyxml2 and minizip:** their [tinyxml2](licenses/tinyxml2-vendored-notice.txt) and [minizip](licenses/minizip-vendored-notice.txt) source notices are staged. The [Info-ZIP terms](licenses/Info-ZIP-source-LICENSE.txt) accompany the source-distributed decryption header; decryption is disabled in the inspected minizip build.
- **minimp3:** retain its [dedication](licenses/minimp3-vendored-dedication.txt) and [CC0 text](licenses/minimp3-CC0-1.0.txt). This is the selected MPEG decoder; the manual's historical Amp credit alone did not identify it.
- **CPU and sound emulation:** retain the [Musashi GPL-permission and historical notices](licenses/Musashi-vendored-notices.txt) and [CPU/sound source credits](licenses/CPU-Sound-vendored-notices.txt). Do not replace the recorded permission history with a blanket claim of original authorship.

## Menu typography

The forthcoming menu uses Barlow Condensed SemiBold and Russo One under the **SIL Open Font License 1.1**. Preserve both complete notices in player and source packages: [Barlow Condensed](licenses/BarlowCondensed-OFL.txt) and [Russo One](licenses/RussoOne-OFL.txt). The original font files and their notices were verified byte-for-byte against [Google Fonts revision `6c1dbe2`](https://github.com/google/fonts/tree/6c1dbe27dcf7d817d4b62cec033ba77fa8e2a5ec/ofl).

The generated raster derivative is named **Harley Menu Typography**. Original font names are used for attribution; the original font files remain unchanged. Keep the font notices and reserved-name conditions with the generated atlas and its build inputs. [OFL FAQ](https://openfontlicense.org/ofl-faq/).

## Game content and branding

No permission to distribute the original game's ROMs, music, extracted textures/models, save states containing game data or other game content is granted by the emulator's software license. This release should contain the modified emulator and permitted supporting files only. Players supply their own legally obtained compatible ROM set. Do not include ROM download links.

Screenshots, videos, logos and other promotional material require a separate content review; software licensing does not automatically cover them. Keep project naming and descriptions clear that this is an unofficial community modification.

## Before publishing

Run `packaging/Test-ReleaseNotices.ps1` against the staged package. Its optional `-SourceRoot` check also verifies the restored ImGui notice and all 15 recorded source blobs. It fails on missing, changed or unsafe notice paths. **A passing notice check is not release clearance**: it does not prove complete corresponding source, future linked dependency coverage or artwork rights.

Finish the remaining component/source inventory, pair complete source with the final binary, verify all shipped DLL provenance, and review the source/archive exclusion list. Restoring ImGui's notice resolves that specific omission; it does not complete every release obligation or establish rights to game imagery or trademarks.
