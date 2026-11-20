// Recover the original ride card from the player's already loaded Harley ROM.
// Contains format knowledge only: no game artwork, palettes, or ROM byte arrays.
#ifndef HARLEY_MENU_ARTWORK_H
#define HARLEY_MENU_ARTWORK_H

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace HarleyMenuArtwork {

// Input is GameLoader's logical, big-endian banked_crom region before Model3
// mutates/copies it. Output is native 496x384 top-down RGBA, including the card's
// original black border. Unsupported layouts fail closed without changing RAM,
// TileGen, ROM data, saves, or settings. No rendering or file access occurs here.
inline bool DecodeOriginalRideCard(const uint8_t* rom, size_t size,
  std::vector<uint8_t>& topDownRGBA, int& width, int& height) noexcept
{
  topDownRGBA.clear();
  width = height = 0;
  try {
    constexpr size_t bankBase = 0x3800000;
    constexpr size_t bankEnd = 0x4000000;
    constexpr size_t descriptor = bankBase + 0x10 + 3 * 8; // ID 0x2AA - 0x2A7
    constexpr size_t patternStart = bankBase + 0x4AD4;
    constexpr size_t mapStart = bankBase + 0x317F0;
    constexpr size_t columns = 62, rows = 48, groups = 22;
    constexpr size_t pixelWidth = columns * 8, pixelHeight = rows * 8;
    constexpr size_t outputBytes = pixelWidth * pixelHeight * 4;
    constexpr size_t missing = std::numeric_limits<size_t>::max();

    if (!rom || size < bankBase || size > bankEnd) return false;
    const auto contains = [size](size_t at, size_t bytes) {
      return at <= size && bytes <= size - at;
    };
    const auto be16 = [rom](size_t at) -> uint16_t {
      return uint16_t((uint16_t(rom[at]) << 8) | rom[at + 1]);
    };
    const auto be32 = [rom](size_t at) -> uint32_t {
      return (uint32_t(rom[at]) << 24) | (uint32_t(rom[at + 1]) << 16)
        | (uint32_t(rom[at + 2]) << 8) | rom[at + 3];
    };
    if (!contains(bankBase, 16) || !contains(descriptor, 8)) return false;
    // This implementation intentionally supports only the inspected asset
    // table/layout. Never guess an address when an unfamiliar ROM is supplied.
    if (be32(bankBase) != 282) return false;
    const uint32_t patternPointer = be32(descriptor);
    const uint32_t mapPointer = be32(descriptor + 4);
    if (patternPointer < 0xFF000000u || patternPointer >= 0xFF800000u
      || mapPointer < 0xFF000000u || mapPointer >= 0xFF800000u) return false;
    if (bankBase + size_t(patternPointer - 0xFF000000u) != patternStart
      || bankBase + size_t(mapPointer - 0xFF000000u) != mapStart) return false;
    if (!contains(patternStart, 4) || !contains(mapStart, 4 + columns * rows * 2)) return false;
    if (be16(patternStart) != 0x100 || be16(patternStart + 2) != groups
      || be16(mapStart) != columns || be16(mapStart + 2) != rows) return false;

    std::vector<uint16_t> palette(32768, 0);
    std::vector<uint8_t> paletteLoaded(32768, 0);
    std::vector<size_t> tileOffsets(groups * 128, missing);
    size_t cursor = patternStart + 4;
    const auto patternContains = [&](size_t at, size_t bytes) {
      return at <= mapStart && bytes <= mapStart - at && contains(at, bytes);
    };
    for (size_t group = 0; group < groups; ++group) {
      if (!patternContains(cursor, 2)) return false;
      const size_t colorCount = be16(cursor);
      cursor += 2;
      if (colorCount == 0 || colorCount > 256
        || !patternContains(cursor, colorCount * 2)) return false;
      const size_t tileBase = group * 128;
      const size_t paletteBase = (((tileBase << 9) & 0x10000)
        | ((tileBase << 2) & 0xFC00)) / 4;
      if (paletteBase > palette.size() || colorCount > palette.size() - paletteBase) return false;
      for (size_t color = 0; color < colorCount; ++color) {
        palette[paletteBase + color] = be16(cursor + color * 2);
        paletteLoaded[paletteBase + color] = 1;
      }
      cursor += colorCount * 2;
      if (colorCount & 1) {
        if (!patternContains(cursor, 2)) return false;
        cursor += 2;
      }
      if (!patternContains(cursor, 2)) return false;
      const size_t tileCount = be16(cursor);
      cursor += 2;
      if (tileCount == 0 || tileCount > 128
        || !patternContains(cursor, tileCount * 64)) return false;
      for (size_t tile = 0; tile < tileCount; ++tile)
        tileOffsets[tileBase + tile] = cursor + tile * 64;
      cursor += tileCount * 64;
    }
    // The original resource ends exactly at its adjacent name table.
    if (cursor != mapStart) return false;
    std::vector<uint8_t> pixels(outputBytes, 0);
    for (size_t ty = 0; ty < rows; ++ty) {
      for (size_t tx = 0; tx < columns; ++tx) {
        const uint16_t name = be16(mapStart + 4 + (ty * columns + tx) * 2);
        const size_t tileIndex = name & 0x3FFF;
        const size_t paletteBase = name & 0x7F00;
        if (tileIndex >= tileOffsets.size() || tileOffsets[tileIndex] == missing) return false;
        const size_t tile = tileOffsets[tileIndex];
        for (size_t py = 0; py < 8; ++py) {
          for (size_t px = 0; px < 8; ++px) {
            const size_t colorIndex = paletteBase | rom[tile + py * 8 + px];
            if (colorIndex >= palette.size() || !paletteLoaded[colorIndex]) return false;
            const uint16_t color = palette[colorIndex];
            const uint8_t alpha = (color & 0x8000) ? 0 : 255;
            const size_t out = ((ty * 8 + py) * pixelWidth + tx * 8 + px) * 4;
            pixels[out] = uint8_t((color & 31) * 255 / 31) & alpha;
            pixels[out + 1] = uint8_t(((color >> 5) & 31) * 255 / 31) & alpha;
            pixels[out + 2] = uint8_t(((color >> 10) & 31) * 255 / 31) & alpha;
            pixels[out + 3] = alpha;
          }
        }
      }
    }
    topDownRGBA.swap(pixels);
    width = int(pixelWidth);
    height = int(pixelHeight);
    return true;
  } catch (...) {
    // Optional artwork must never prevent the ordinary menu from opening.
    topDownRGBA.clear();
    width = height = 0;
    return false;
  }
}

} // namespace HarleyMenuArtwork
#endif
