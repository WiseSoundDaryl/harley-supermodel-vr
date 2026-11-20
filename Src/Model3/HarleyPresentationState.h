#ifndef INCLUDED_HARLEY_PRESENTATION_STATE_H
#define INCLUDED_HARLEY_PRESENTATION_STATE_H

#include "Graphics/HarleyPresentation.h"
#include <cstddef>
#include <cstring>

namespace HarleyPresentation
{
constexpr size_t RamBytes = 0x800000;
constexpr size_t RomCodeBase = 0x480000;
constexpr size_t MainPhase = 0x20A500;
constexpr size_t RacePhase = 0x2020AC;
constexpr size_t AttractFlag = 0x1F6368;
constexpr size_t IntroComplete = 0x1F013C;
constexpr size_t RiderConfirmed = 0x210EFC;
constexpr size_t SelectionStage = 0x210F04;

inline uint32_t ReadWord(const uint8_t* bytes, size_t offset)
{
  uint32_t word;
  std::memcpy(&word, bytes + offset, sizeof(word));
  return word;
}

// Exact Revision B dispatch table plus the instructions that set the menu
// flags. Input is Supermodel's host-order word layout, not archive bytes.
// Check the original ROM once at load and its RAM copy when sampling a save.
inline bool MatchesCode(const uint8_t* bytes, size_t size, size_t base)
{
  struct Signature { size_t address; uint32_t word; };
  constexpr Signature signatures[] = {
    {0x0E6D4C, 0x0002F24C}, {0x0E6D5C, 0x0002F4FC},
    {0x0E6D64, 0x0002F6C4}, {0x0E6D84, 0x0002FAD4},
    {0x0E6D88, 0x0002FB98}, {0x04D654, 0x3D20001F},
    {0x04D658, 0x93A9013C}, {0x04D6A4, 0x3D20001F},
    {0x04D6A8, 0x38000001}, {0x04D6AC, 0x9009013C},
    {0x04D888, 0x3D200021}, {0x04D88C, 0x38000001},
    {0x04D890, 0x90090EFC}, {0x04D8CC, 0x3D200021},
    {0x04D8D0, 0x38000002}, {0x04D8D4, 0x90090F04}
  };
  if (!bytes || base > size)
    return false;
  for (const auto& signature : signatures)
  {
    if (signature.address > size - base ||
        sizeof(uint32_t) > size - base - signature.address ||
        ReadWord(bytes, base + signature.address) != signature.word)
      return false;
  }
  return true;
}

inline bool MatchesMotionCode(const uint8_t* ram, size_t size)
{
  struct Signature { size_t address; uint32_t word; };
  // Verified against the original Revision B ROM and its saved RAM image.
  // Allocation/type, player selection, support solve and impact timer writers.
  constexpr Signature signatures[] = {
    {0xBFBC, 0x388006A0}, {0xBFCC, 0x93A30034},
    {0xBFF4, 0x392963D0}, {0xBFFC, 0x7C7D492E},
    {0xCAA4, 0x90A9053C}, {0x73620, 0x9009053C},
    {0x73630, 0x90090498}, {0x73EC4, 0xC8095978},
    {0x73EDC, 0x901C0498}, {0x46260, 0x80096414},
    {0x46268, 0x392963D0}, {0x46278, 0x38A006A0}
  };
  if (!ram || size < RamBytes) return false;
  for (const auto& signature : signatures)
    if (ReadWord(ram, signature.address) != signature.word) return false;
  return true;
}

inline MotionState SampleMotion(const uint8_t* ram, size_t size, const State& state)
{
  MotionState motion;
  if (Classify(state) != Mode::Driving || !MatchesMotionCode(ram, size)) return motion;
  // This mod's verified single-cabinet path. Network player records are delayed
  // copies and their index is not a safe selector for the four live objects.
  if (ReadWord(ram, 0x20C560) != 1 || ReadWord(ram, 0x210E08) != 0) return motion;
  const uint32_t index = ReadWord(ram, 0x1F6414);
  if (index >= 4) return motion;
  const uint32_t bike = ReadWord(ram, 0x1F63D0 + 4 * index);
  if (!bike || (bike & 3) || bike > RamBytes - 0x6A0) return motion;
  if (ReadWord(ram, bike) != 0xBE18 || ReadWord(ram, bike + 0xC) != 0x80018691 ||
      ReadWord(ram, bike + 0x34) != index || ReadWord(ram, bike + 0x70) > 4 ||
      ReadWord(ram, bike + 0x30) > 8) return motion;
  const uint32_t front = ReadWord(ram, bike + 0x498);
  const uint32_t rear = ReadWord(ram, bike + 0x49C);
  const uint32_t frontImpact = ReadWord(ram, bike + 0x53C);
  const uint32_t rearImpact = ReadWord(ram, bike + 0x540);
  if (front > 1 || rear > 1 || frontImpact > 30 || rearImpact > 30) return motion;
  motion.valid = true;
  motion.frontSupported = front != 0;
  motion.rearSupported = rear != 0;
  motion.frontImpactFrames = frontImpact;
  motion.rearImpactFrames = rearImpact;
  return motion;
}

// Call only with PPC execution parked, alongside SyncSnapshots or a state
// load. No state mutation or presentation clock advancement occurs here.
inline State Sample(const uint8_t* ram, size_t size, bool supportedROM)
{
  State state;
  if (!supportedROM || size < RamBytes || !MatchesCode(ram, size, 0))
    return state;
  state.revisionB = true;
  state.mainPhase = ReadWord(ram, MainPhase);
  state.racePhase = ReadWord(ram, RacePhase);
  state.attractFlag = ReadWord(ram, AttractFlag);
  state.introComplete = ReadWord(ram, IntroComplete);
  state.riderConfirmed = ReadWord(ram, RiderConfirmed);
  state.selectionStage = ReadWord(ram, SelectionStage);
  state.motion = SampleMotion(ram, size, state);
  return state;
}
}

#endif
