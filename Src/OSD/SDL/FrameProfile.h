#ifndef INCLUDED_CPU_FRAME_PROFILE_H
#define INCLUDED_CPU_FRAME_PROFILE_H

#include <vector>
#include "Util/DiagnosticFile.h"
#include "Util/NewConfig.h"
#include "SDLIncludes.h"
#include "OSD/Logger.h"

// Main/render-thread measurements. RunFrame is wall time inclusive of worker
// waits, GPU synchronization and swap; it must not be called pure CPU compute.
class CpuFrameProfile
{
public:
  ~CpuFrameProfile() { Close(); }

  bool Open(const Util::Config::Node& config, const std::string& game)
  {
    const std::string prefix = config["FrameProfile"].ValueAs<std::string>();
    if (prefix.empty()) return true;
    m_file = Util::OpenExclusiveDiagnostic(prefix + ".cpu.jsonl");
    if (!m_file) { ErrorLog("Cannot exclusively create CPU frame profile '%s.cpu.jsonl'.", prefix.c_str()); return false; }
    m_first = config["FrameProfileStart"].ValueAs<unsigned>();
    m_count = config["FrameProfileFrames"].ValueAs<unsigned>();
    m_frequency = double(SDL_GetPerformanceFrequency());
    m_records.reserve(m_count);
    fputs("{\"type\":\"header\",\"schema\":1,\"timer\":\"SDL_performance_counter\",\"units\":\"milliseconds\",\"game\":", m_file);
    Util::WriteDiagnosticJsonString(m_file, game);
    fprintf(m_file, ",\"sample_start\":%llu,\"sample_limit\":%u,\"counter_frequency\":%.0f,\"requested_multithreaded\":%s,\"requested_gpu_multithreaded\":%s,\"throttle\":%s,\"vsync\":%s,\"stereo\":%s,\"cull_bypass\":%s,\"rider_comfort\":%s,\"run_frames\":%u,\"input_script\":",
            (unsigned long long)m_first, m_count, m_frequency,
            config["MultiThreaded"].ValueAs<bool>() ? "true" : "false",
            config["GPUMultiThreaded"].ValueAs<bool>() ? "true" : "false",
            config["Throttle"].ValueAs<bool>() ? "true" : "false",
            config["VSync"].ValueAs<bool>() ? "true" : "false",
            config["StereoDiagnostic"].ValueAs<bool>() ? "true" : "false",
            config["StereoCullBypass"].ValueAs<bool>() ? "true" : "false",
            config["StereoRiderComfort"].ValueAs<bool>() ? "true" : "false",
            config["RunFrames"].ValueAs<unsigned>());
    Util::WriteDiagnosticJsonString(m_file, config["InputScript"].ValueAs<std::string>());
    fputs(",\"load_state\":", m_file);
    Util::WriteDiagnosticJsonString(m_file, config["InitStateFile"].ValueAs<std::string>());
    fprintf(m_file, ",\"width\":%u,\"height\":%u,\"supersampling\":%u,\"sound_emulated\":%s,\"dsb_emulated\":%s",
            config["XResolution"].ValueAs<unsigned>(), config["YResolution"].ValueAs<unsigned>(),
            config["Supersampling"].ValueAs<unsigned>(), config["EmulateSound"].ValueAs<bool>() ? "true" : "false",
            config["EmulateDSB"].ValueAs<bool>() ? "true" : "false");
    fputs(",\"semantics\":\"RunFrame wall time includes render/worker waits/swap; excludes startup, input polling, UI, explicit throttle, profile writes. Swap/capture are included subsets, not additive. Thread flags are requested settings; check logs for fallback.\"}\n", m_file);
    return true;
  }

  bool Begin(uint64_t frame)
  {
    m_active = m_file && frame >= m_first && frame - m_first < m_count;
    if (!m_active) return false;
    m_current = {};
    m_current.frame = frame;
    m_begin = SDL_GetPerformanceCounter();
    return true;
  }
  bool Active() const { return m_active; }
  void Video(uint64_t captureTicks, uint64_t swapTicks)
  {
    if (!m_active) return;
    m_current.capture += captureTicks;
    m_current.swap += swapTicks;
    ++m_current.videoCallbacks;
  }
  void End()
  {
    if (!m_active) return;
    m_current.run = SDL_GetPerformanceCounter() - m_begin;
    m_records.push_back(m_current);
    m_active = false;
  }
  bool Close()
  {
    if (!m_file) return true;
    const double toMs = 1000.0 / m_frequency;
    for (const auto& r : m_records)
      fprintf(m_file, "{\"type\":\"frame\",\"frame\":%llu,\"run_frame_ms\":%.9f,\"swap_ms\":%.9f,\"capture_ms\":%.9f,\"video_callbacks\":%u}\n",
              (unsigned long long)r.frame, double(r.run)*toMs, double(r.swap)*toMs, double(r.capture)*toMs, r.videoCallbacks);
    fprintf(m_file, "{\"type\":\"footer\",\"samples\":%llu,\"expected_samples\":%u}\n", (unsigned long long)m_records.size(), m_count);
    const bool good = ferror(m_file) == 0;
    const bool closed = fclose(m_file) == 0;
    const bool complete = m_records.size() == m_count;
    m_file = nullptr;
    if (!good || !closed) ErrorLog("Writing CPU frame profile failed.");
    if (!complete) ErrorLog("CPU frame profile is incomplete: %llu of %u requested samples.", (unsigned long long)m_records.size(), m_count);
    return good && closed && complete;
  }

private:
  struct Record { uint64_t frame = 0, run = 0, swap = 0, capture = 0; unsigned videoCallbacks = 0; };
  FILE* m_file = nullptr;
  bool m_active = false;
  uint64_t m_first = 0, m_begin = 0;
  unsigned m_count = 0;
  double m_frequency = 1.0;
  Record m_current;
  std::vector<Record> m_records;
};
#endif
