#ifndef INCLUDED_HARLEY_RIDER_PROFILES_H
#define INCLUDED_HARLEY_RIDER_PROFILES_H

#include <cstdint>

namespace New3D {
namespace HarleyRiders {

// Measured from each of Harley B's five rider selections in both native
// chase and cockpit views. These are immutable ROM model signatures, not
// volatile RAM node addresses. Runtime code also validates rigid topology,
// the local model transform, native camera offset, and unique nearby arms.
struct Profile {
  uint32_t chaseModel;
  uint32_t cockpitModel;
  float chaseOffset[3];
  float cockpitOffset[3];
  float headOffset[3];
  uint32_t arms[6];
};

static constexpr Profile Profiles[] = {
  { 0x96FD45, 0x97439D,
    { 0, 5.61865234375f, -3.43310546875f },
    { 0, 8.22705078125f, 1.732421875f }, { 0, 11.5f, 0 },
    { 0x85FA4D, 0x860C53, 0x8608B1, 0x85F8CE, 0x860AB7, 0x8606AB } },
  { 0xFDB54F, 0xFDFB7B,
    { 0, 5.2841796875f, -3.423828125f },
    { 0.3740234375f, 8.04931640625f, 2.61474609375f }, { 0, 12.5f, 0 },
    { 0x859BDC, 0x85AB9A, 0x85A888, 0x859A66, 0x85AA35, 0x85A6DB } },
  { 0x93769C, 0x93B1A1,
    { 0, 5.8583984375f, -2.6533203125f },
    { 0, 8.6220703125f, 2.71728515625f }, { 0, 12.5f, -0.5f },
    { 0xC8248D, 0xC83E99, 0xC83B24, 0xC822A5, 0xC83CCD, 0xC8397B } },
  { 0xCFCF1E, 0xD009B9,
    { 0.2451171875f, 5.4404296875f, -3.1943359375f },
    { 0, 0, 0 }, { 0, 13, -0.5f },
    { 0x873A98, 0x874B03, 0x874796, 0x873951, 0x874994, 0x874590 } },
  { 0xC9AF10, 0xC9F5FF,
    { 0, 7.66064453125f, -1.60009765625f },
    { 0, 8.53564453125f, 3.59521484375f }, { 0, 13, 0 },
    { 0x86E052, 0x86F210, 0x86EE83, 0x86DE90, 0x86F081, 0x86EC8D } }
};

inline const Profile* Find(uint32_t source, bool allowChase)
{
  for (const auto& profile : Profiles) {
    if (source == profile.cockpitModel || (allowChase && source == profile.chaseModel)) return &profile;
  }
  return nullptr;
}

inline unsigned ArmBit(const Profile& profile, uint32_t source)
{
  for (unsigned i = 0; i < 6; ++i) if (profile.arms[i] == source) return 1u << i;
  return 0;
}

} // namespace HarleyRiders
} // namespace New3D
#endif
