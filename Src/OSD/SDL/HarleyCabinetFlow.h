#ifndef INCLUDED_HARLEY_CABINET_FLOW_H
#define INCLUDED_HARLEY_CABINET_FLOW_H

#include <cstdint>

// No game writes or synthetic navigation. Observe completed native frames only,
// with the PPC worker parked, and restore normal controls after the ROM exits
// its cabinet phase. Boot/unknown state is never treated as a successful EXIT.
namespace HarleyCabinetFlow {
struct Sample { bool verified=false; uint32_t phase=0; };

template<class ReadWord>
Sample ObserveRevisionB(bool supported, ReadWord read) {
  if (!supported) return {};
  // Same Revision B dispatch/selection signatures as HarleyPresentationState.
  // These addresses refer to the ROM program copied to native main RAM.
  constexpr uint32_t signatures[][2] = {
    {0x0E6D4C,0x0002F24C},{0x0E6D5C,0x0002F4FC},
    {0x0E6D64,0x0002F6C4},{0x0E6D84,0x0002FAD4},
    {0x0E6D88,0x0002FB98},{0x04D654,0x3D20001F},
    {0x04D658,0x93A9013C},{0x04D6A4,0x3D20001F},
    {0x04D6A8,0x38000001},{0x04D6AC,0x9009013C},
    {0x04D888,0x3D200021},{0x04D88C,0x38000001},
    {0x04D890,0x90090EFC},{0x04D8CC,0x3D200021},
    {0x04D8D0,0x38000002},{0x04D8D4,0x90090F04}
  };
  for (const auto& signature:signatures)
    if (read(signature[0])!=signature[1]) return {};
  return {true,read(0x20A500)};
}

class Controller {
public:
  void Begin() { m_active=true; m_seenTest=false; }
  void Cancel() { m_active=false; m_seenTest=false; }
  bool Active() const { return m_active; }
  bool Observe(const Sample& sample) {
    if (!m_active || !sample.verified) return false;
    if (sample.phase==2) m_seenTest=true;
    // Retained native EXIT fixture returns to phase4 (attract/startup).
    // Phase5 is the normal game dispatcher; it also must never keep TEST mapped.
    if (m_seenTest && (sample.phase==4 || sample.phase==5)) {
      Cancel(); return true;
    }
    return false;
  }
private:
  bool m_active=false,m_seenTest=false;
};
}
#endif
