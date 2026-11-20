#ifndef HARLEY_MENU_AUDIO_PREVIEW_H
#define HARLEY_MENU_AUDIO_PREVIEW_H

#include <algorithm>
#include <cmath>
#include <cstdint>

// Original short calibration chime, not a recording or extracted game asset.
// No allocation, device access or clock sampling. The SDL owner serializes
// requests against its callback; a muted test cannot arm this voice.
class MenuAudioPreview {
public:
  static constexpr unsigned Rate = 44100, Length = Rate / 2;
  void Start(int volume, bool allowed) {
    m_gain = allowed ? std::clamp(volume, 0, 100) / 100.0 : 0.0;
    m_position = m_gain > 0 ? 0 : Length;
  }
  void Stop() { m_position = Length; }
  bool Active() const { return m_position < Length; }
  int16_t Next() {
    if (!Active()) return 0;
    const double t = double(m_position) / Rate;
    const double envelope = std::min(1.0, t / .012) *
      std::min(1.0, double(Length - 1 - m_position) / (Rate * .08));
    const double phase = 6.283185307179586 * t;
    const double chime = .60 * std::sin(phase * 220.0) +
                        .25 * std::sin(phase * 330.0) +
                        .15 * std::sin(phase * 440.0);
    ++m_position;
    return int16_t(std::lround(6000.0 * m_gain * envelope * chime));
  }
private:
  unsigned m_position = Length;
  double m_gain = 0;
};
#endif
