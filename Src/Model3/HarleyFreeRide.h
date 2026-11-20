#ifndef INCLUDED_HARLEY_FREE_RIDE_H
#define INCLUDED_HARLEY_FREE_RIDE_H

#include <cstddef>
#include <cstdint>
#include <cstring>

// A narrowly scoped, opt-in experiment for Harley Export Revision B. The
// original game copies this routine from fixed CROM into executable RAM. Only
// its active-race countdown subtraction becomes a no-op. The following
// clamp/store and all independent game simulation/elapsed time stay intact.
// Neither a ROM archive nor an on-disk save needs to contain modified code.
namespace HarleyFreeRide
{
constexpr size_t RamBytes = 0x800000;
constexpr size_t Routine = 0x02F7A4;
constexpr size_t Instruction = 0x02F7C0;
constexpr size_t RomRoutine = 0x4AF7A4;
constexpr uint32_t Original = 0x7D004050;
constexpr uint32_t Held = 0x60000000;
constexpr size_t MainPhase = 0x20A500;
constexpr size_t RacePhase = 0x2020AC;
constexpr size_t AttractFlag = 0x1F6368;
constexpr size_t Countdown = 0x20C62C;
constexpr size_t ElapsedFrames = 0x2020B0;
constexpr size_t MusicRoutine = 0x05F578;
constexpr size_t MusicInstruction = 0x05F588;
constexpr size_t MusicRomRoutine = 0x4DF578;
constexpr uint32_t MusicOriginal = 0x88090134;  // lbz r0,0x134(r9)

inline uint32_t Read(const uint8_t* bytes, size_t offset)
{
  uint32_t value;
  std::memcpy(&value, bytes + offset, sizeof(value));
  return value;
}

inline bool Matches(const uint8_t* bytes, size_t size, size_t offset, bool allowHeld)
{
  constexpr uint32_t words[] = {
    0x7C000026, 0x540047FE, 0x7C0000D0, 0x540907FE,
    0x7C0000F8, 0x540007BC, 0x7D200378, Original,
    0x7D0040F8, 0x7C00FE70, 0x7C004038, 0x90070084
  };
  if (!bytes || offset > size || sizeof(words) > size - offset)
    return false;
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
  {
    const uint32_t actual = Read(bytes, offset + i * sizeof(uint32_t));
    if (actual != words[i] && !(allowHeld && i == 7 && actual == Held))
      return false;
  }
  return true;
}

inline bool IsPatched(const uint8_t* ram, size_t size)
{
  return Matches(ram, size, Routine, true) && Read(ram, Instruction) == Held;
}

// The game's BGM VOLUME service setting is separate from ENGINE VOLUME. This
// verified routine reads only BGM, maps level 1..8 to the BGM command's volume,
// and enqueues it. Override the read, not the setting/NVRAM or SCSP master mix.
inline bool MatchesMusic(const uint8_t* bytes, size_t size, size_t offset, bool allowOverride)
{
  constexpr uint32_t words[] = {
    0x9421FFF0, 0x7C0802A6, 0x90010014, 0x3D20001E,
    MusicOriginal, 0x54001838, 0x3000003E, 0x3C6000A0,
    0x60630100, 0x7C031B78, 0x38800003, 0x4BFFFB4D
  };
  if (!bytes || offset > size || sizeof(words) > size - offset)
    return false;
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
  {
    const uint32_t actual = Read(bytes, offset + i * sizeof(uint32_t));
    const bool knownOverride = i == 4 && actual >= 0x38000001 && actual <= 0x38000008;
    if (actual != words[i] && !(allowOverride && knownOverride))
      return false;
  }
  return true;
}

inline bool IsMusicPatched(const uint8_t* ram, size_t size)
{
  return MatchesMusic(ram, size, MusicRoutine, true) && Read(ram, MusicInstruction) != MusicOriginal;
}

inline bool UpdateMusic(uint8_t* ram, size_t size, unsigned level)
{
  if (level > 8 || !MatchesMusic(ram, size, MusicRoutine, true))
    return false;
  const uint32_t desired = level ? 0x38000000 | level : MusicOriginal;
  if (Read(ram, MusicInstruction) != desired)
    std::memcpy(ram + MusicInstruction, &desired, sizeof(desired));
  return level != 0;
}

// Call exclusively on the emulated PPC's owner thread, before executing a
// native frame. The main phase and attract flag keep the ordinary attract loop
// and all menu timers intact. Race phase 8 is the game's active driving phase.
// An already-expired race is deliberately not revived.
inline bool Update(uint8_t* ram, size_t size, bool enabled)
{
  if (size < RamBytes || !Matches(ram, size, Routine, true))
    return false;
  const uint32_t ticks = Read(ram, Countdown);
  const bool active = enabled && Read(ram, MainPhase) == 5 &&
    Read(ram, AttractFlag) == 0 && Read(ram, RacePhase) == 8 &&
    ticks > 0 && ticks < 60 * 60 * 60;
  const uint32_t desired = active ? Held : Original;
  if (Read(ram, Instruction) != desired)
    std::memcpy(ram + Instruction, &desired, sizeof(desired));
  return active;
}
}

#endif
