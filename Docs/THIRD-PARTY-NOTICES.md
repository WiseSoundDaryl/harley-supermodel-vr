# Third-party notices

HarleyVR builds on Supermodel and the open-source components listed below. Their copyright notices and license terms are included in the [licenses folder](licenses/). File checksums and source references are available in the [license manifest](licenses/SHA256.json).

## Supermodel

[Supermodel](https://github.com/trzy/Supermodel) is copyright its contributors and is distributed under GNU GPL version 3 or, where stated in its source notices, any later version. HarleyVR retains its upstream license, [manual and acknowledgments](Supermodel-Manual.txt), and component notices.

Redistribution of a HarleyVR executable must include access to its complete corresponding modified source and the scripts needed to build and install it. This includes optional code compiled into the executable, even when a launcher leaves that feature disabled. See [GPLv3, sections 4–6](https://www.gnu.org/licenses/gpl-3.0.html) and the [build documentation](BUILDING.md).

## Runtime libraries

| Component | Purpose | License and notices |
| --- | --- | --- |
| OpenXR SDK loader | Connects the application to the installed OpenXR runtime | [Apache 2.0](licenses/OpenXR-Apache-2.0.txt) |
| JsonCpp 1.9.6 | JSON support included in the OpenXR loader | [MIT/public-domain terms](licenses/JsonCpp-1.9.6-LICENSE.txt) |
| GCC runtime / libstdc++ | C/C++ runtime support | [GPLv3](licenses/GCC-GPL-3.0.txt) with the [GCC Runtime Library Exception 3.1](licenses/GCC-Runtime-Library-Exception-3.1.txt) |
| winpthreads | Threading support | [Copyright and permission notices](licenses/winpthreads-COPYING.txt) |
| MinGW-w64 CRT | Windows runtime support | [Runtime notices](licenses/MinGW-w64-runtime-notices.txt) |
| SDL2 | Window, input and audio support | [SDL2 license](licenses/SDL2-LICENSE.txt) |
| SDL2_net | Networking support | [SDL2_net license](licenses/SDL2_net-LICENSE.txt) |
| zlib | ZIP and decompression support | [zlib license](licenses/zlib-LICENSE.txt) |

JsonCpp is linked into the OpenXR loader; it has no separate DLL. Package versions are listed in [BUILDING.md](BUILDING.md).

Project references: [Khronos OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK), [GCC Runtime Library Exception](https://www.gnu.org/licenses/gcc-exception-3.1.html), [SDL2](https://github.com/libsdl-org/SDL/tree/SDL2), [SDL_net](https://github.com/libsdl-org/SDL_net), [zlib](https://zlib.net/zlib_license.html), [MinGW-w64](https://www.mingw-w64.org/).

## Embedded components

- **GLEW:** [copyright and license notices](licenses/GLEW-vendored-notices.txt), including Nigel Stewart, Mesa/Brian Paul and Khronos.
- **Dear ImGui:** [MIT license](licenses/Dear-ImGui-LICENSE.txt), copyright 2014–2025 Omar Cornut. The bundled version is `1.92.6 WIP`, from [upstream revision `05581da`](https://github.com/ocornut/imgui/tree/05581da18321bee38f6d5915a1466665cdd66fc8). The notice also appears in `Src/Pkgs/imgui/LICENSE.txt`; [source references](licenses/Dear-ImGui-SOURCE.json) identify the included files.
- **ImGui support code and data:** [STB dual-license terms](licenses/ImGui-STB-dual-license.txt), [gl3w public-domain dedication](licenses/ImGui-gl3w-public-domain.txt), and [Japanese glyph-list CC BY 4.0 attribution](licenses/ImGui-Japanese-glyph-list-attribution.txt). The glyph-list license applies to that data.
- **ImGui embedded fonts:** [font attributions](licenses/ImGui-embedded-font-attributions.txt), [ProggyClean MIT license](licenses/ProggyClean-MIT.txt), and [ProggyVector license](licenses/ProggyVector-LICENSE.txt). ProggyVector includes Bitstream Vera terms and DejaVu public-domain material alongside its MIT notices.
- **MAME-derived components:** [BSD-3-Clause terms](licenses/MAME-BSD-3-Clause.txt) and [component copyright holders](licenses/MAME-component-attributions.txt), including the security-chip code.
- **tinyxml2 and minizip:** [tinyxml2 notice](licenses/tinyxml2-vendored-notice.txt) and [minizip notice](licenses/minizip-vendored-notice.txt). The [Info-ZIP license](licenses/Info-ZIP-source-LICENSE.txt) accompanies the source decryption header; ZIP decryption is disabled in the current build.
- **minimp3:** [public-domain dedication](licenses/minimp3-vendored-dedication.txt) and [CC0 text](licenses/minimp3-CC0-1.0.txt) for the MPEG audio decoder.
- **CPU and sound emulation:** [Musashi GPL permission and historical notices](licenses/Musashi-vendored-notices.txt), plus [CPU and sound component notices](licenses/CPU-Sound-vendored-notices.txt).

## Menu typography

The menu uses Barlow Condensed SemiBold and Russo One under the **SIL Open Font License 1.1**. Their full copyright and license texts are included: [Barlow Condensed](licenses/BarlowCondensed-OFL.txt) and [Russo One](licenses/RussoOne-OFL.txt). Source fonts are available at [Google Fonts revision `6c1dbe2`](https://github.com/google/fonts/tree/6c1dbe27dcf7d817d4b62cec033ba77fa8e2a5ec/ofl).

The converted raster font data is named **Harley Menu Typography** and retains its OFL terms. Original font names are used for attribution. Redistributed font data must retain the applicable copyright, license and reserved-name conditions. See the [OFL FAQ](https://openfontlicense.org/ofl-faq/).

## Game content and trademarks

HarleyVR is an unofficial fan project, not affiliated with or endorsed by SEGA, Harley-Davidson, Supermodel's developers or headset manufacturers. Names, marks and game visuals belong to their respective owners.

The software licenses do not grant rights to the game's ROMs, music, models, textures, game-bearing save states, screenshots or trademarks. Players supply their own legally obtained compatible ROM set. The project does not provide ROMs or ROM download links; game imagery and branding remain subject to separate rights requirements.

## Redistribution and verification

Retain the applicable notices with source and binary distributions, including the complete GCC Runtime Library Exception and font terms. Match the source, build instructions and dependency versions to the executable being distributed.

Maintainers can run `packaging/Test-ReleaseNotices.ps1` to check the recorded notice files. Its optional `-SourceRoot` argument also checks the bundled ImGui source and license identities. This verifies file integrity; corresponding-source obligations, dependency coverage and rights to game imagery require separate review. See [BUILDING.md](BUILDING.md) for release preparation.
