#ifndef INCLUDED_HEADSET_CHECKPOINT_BUFFER_H
#define INCLUDED_HEADSET_CHECKPOINT_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

// Owner-thread telemetry only. Reserve and touch storage before emulation;
// Append never allocates, formats text, locks or writes files. Drain only after
// headset rendering has stopped. A forced process exit loses buffered records.
// Keep the newest two-plus hours of checkpoints; report dropped older records.
class HeadsetCheckpointBuffer
{
public:
  struct Record {
    uint64_t frame = 0;
    double activeSeconds = 0;
    uint64_t native = 0, stereo = 0, segments = 0;
    double estimatedMs = 0;
    uint64_t samples = 0, spikes = 0, overruns = 0;
  };
  static constexpr size_t Capacity = 4096;

  bool Initialize() noexcept
  {
    if (m_records) return true;
    m_records.reset(new (std::nothrow) Record[Capacity]{});
    return bool(m_records);
  }

  void Append(const Record& record) noexcept
  {
    if (!m_records) { ++m_dropped; return; }
    if (m_count < Capacity) {
      m_records[(m_first + m_count) % Capacity] = record;
      ++m_count;
    } else {
      m_records[m_first] = record;
      m_first = (m_first + 1) % Capacity;
      ++m_dropped;
    }
  }

  template<class Sink> void Drain(Sink&& sink)
  {
    for (size_t i = 0; i < m_count; ++i)
      sink(m_records[(m_first + i) % Capacity]);
    m_count = m_first = 0;
  }

  size_t Count() const noexcept { return m_count; }
  uint64_t Dropped() const noexcept { return m_dropped; }
  bool Ready() const noexcept { return bool(m_records); }

private:
  std::unique_ptr<Record[]> m_records;
  size_t m_first = 0, m_count = 0;
  uint64_t m_dropped = 0;
};

#endif
