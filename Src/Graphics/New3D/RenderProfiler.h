/*
 * Optional renderer timing for the desktop/VR diagnostic.
 * Part of Supermodel, distributed under GPL-3.0-or-later.
 *
 * CPU intervals use a monotonic high-resolution clock. GPU intervals are
 * timestamp differences along the GL command timeline, not hardware busy
 * counters: they can include submission gaps and synchronization. The whole
 * interval contains the phase intervals and must never be added to them.
 */
#ifndef INCLUDED_NEW3D_RENDERPROFILER_H
#define INCLUDED_NEW3D_RENDERPROFILER_H

#include <GL/glew.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "OSD/Logger.h"
#include "Util/DiagnosticFile.h"

namespace New3D {

class RenderProfiler
{
public:
    enum class Stage : size_t {
        Whole, CenterDecode, CenterUpload, CenterDrawLos,
        BypassDecode, BypassUpload, LeftEye, RightEye, Composite, Count
    };
    // x/y offset, viewport width/height, framebuffer width/height.
    using Dimensions = std::array<unsigned, 6>;

    // Non-copyable scopes guarantee that every begun interval is closed on
    // early returns as well as the normal renderer path.
    class Scope {
    public:
        Scope(RenderProfiler* owner, Stage stage, bool frame = false)
            : m_owner(owner), m_stage(stage), m_frame(frame)
        {
            if (m_owner) m_owner->BeginStage(m_stage);
        }
        ~Scope()
        {
            if (!m_owner) return;
            m_owner->EndStage(m_stage);
            if (m_frame) m_owner->EndFrame();
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        RenderProfiler* m_owner;
        Stage m_stage;
        bool m_frame;
    };

    RenderProfiler() = default;
    ~RenderProfiler() { Close(); }
    RenderProfiler(const RenderProfiler&) = delete;
    RenderProfiler& operator=(const RenderProfiler&) = delete;

    bool Configure(const std::string& prefix, uint64_t firstFrame, unsigned frameCount,
                   bool threaded, bool gpuThreaded, const Dimensions& dimensions, unsigned supersampling)
    {
        m_dimensions = dimensions;
        if (prefix.empty()) return true;
        if (m_configured) return m_file != nullptr;
        m_configured = true;
        m_firstFrame = std::max<uint64_t>(1, firstFrame);
        m_frameCount = std::clamp(frameCount, 1u, 10000u);
        m_path = prefix + ".render.jsonl";
        m_file = Util::OpenExclusiveDiagnostic(m_path);
        if (!m_file) {
            ErrorLog("Renderer profile cannot create exclusive output '%s'.", m_path.c_str());
            return false;
        }
        m_records.reserve(m_frameCount);
        m_gpuAvailable = (GLEW_VERSION_3_3 || GLEW_ARB_timer_query) && glQueryCounter && glGetQueryObjectui64v;
        if (m_gpuAvailable) {
            glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &m_queryBits);
            m_gpuAvailable = m_queryBits > 0;
        }
        if (m_gpuAvailable) {
            for (auto& slot : m_slots) {
                glGenQueries(GLsizei(slot.queries.size()), slot.queries.data());
                for (GLuint query : slot.queries) if (!query) m_gpuAvailable = false;
            }
        }
        // Header writes and allocation occur before the measured frame loop.
        fprintf(m_file,
            "{\"type\":\"render_profile_header\",\"version\":1,\"start_frame\":%llu,\"frame_count\":%u,"
            "\"requested_threaded\":%s,\"requested_gpu_threaded\":%s,\"gpu_timestamps_supported\":%s,\"gpu_timestamp_bits\":%d,"
            "\"query_ring_slots\":%llu,\"cpu_clock\":\"std::chrono::steady_clock\","
            "\"cpu_clock_period_ns\":%.9g,\"units\":\"milliseconds\","
            "\"gpu_meaning\":\"GL timeline elapsed, including possible submission gaps; not hardware busy time\","
            "\"whole_contains_phases\":true,\"results_buffered_until_close\":true,\"blocking_query_waits\":false,"
            "\"viewport_rect\":[%u,%u,%u,%u],\"framebuffer_size\":[%u,%u],\"requested_supersampling\":%u,\"gl_vendor\":",
            (unsigned long long)m_firstFrame, m_frameCount,
            threaded ? "true" : "false", gpuThreaded ? "true" : "false",
            m_gpuAvailable ? "true" : "false", m_queryBits,
            (unsigned long long)m_slots.size(),
            1e9 * double(Clock::period::num) / double(Clock::period::den),
            dimensions[0], dimensions[1], dimensions[2], dimensions[3], dimensions[4], dimensions[5], supersampling);
        WriteGlString(GL_VENDOR);
        fputs(",\"gl_renderer\":", m_file); WriteGlString(GL_RENDERER);
        fputs(",\"gl_version\":", m_file); WriteGlString(GL_VERSION);
        fputs("}\n", m_file);
        if (fflush(m_file) != 0) {
            ErrorLog("Renderer profile cannot write header to '%s'.", m_path.c_str());
            return false;
        }
        InfoLog("Renderer profile enabled: '%s', frames %llu through %llu, asynchronous GPU timestamps=%d.",
            m_path.c_str(), (unsigned long long)m_firstFrame,
            (unsigned long long)(m_firstFrame + m_frameCount - 1), int(m_gpuAvailable));
        return true;
    }

    Scope Frame(uint64_t hostFrame, uint64_t sceneFrame, uint64_t sceneEpoch, uint64_t renderSerial)
    {
        return Scope(BeginFrame(hostFrame, sceneFrame, sceneEpoch, renderSerial) ? this : nullptr, Stage::Whole, true);
    }

    Scope Measure(Stage stage)
    {
        return Scope(m_active ? this : nullptr, stage);
    }

    // Called while the owning renderer's GL context still exists. Never wait
    // for unresolved work merely to improve measurement coverage.
    void Close()
    {
        if (!m_file) return;
        Harvest();
        for (auto& slot : m_slots) {
            if (slot.pending) {
                m_records[slot.recordIndex].gpuStatus = "unresolved_at_shutdown";
                slot.pending = false;
            }
            glDeleteQueries(GLsizei(slot.queries.size()), slot.queries.data());
            slot.queries.fill(0);
        }
        size_t ready = 0, dropped = 0, unavailable = 0, unresolved = 0, invalid = 0;
        for (const auto& record : m_records) {
            WriteRecord(record);
            const std::string status(record.gpuStatus);
            if (status == "ready") ++ready;
            else if (status == "dropped_ring_full") ++dropped;
            else if (status == "unavailable") ++unavailable;
            else if (status == "unresolved_at_shutdown") ++unresolved;
            else ++invalid;
        }
        fprintf(m_file,
            "{\"type\":\"render_profile_footer\",\"samples\":%llu,\"gpu_ready\":%llu,"
            "\"gpu_dropped\":%llu,\"gpu_unavailable\":%llu,\"gpu_unresolved\":%llu,\"gpu_invalid\":%llu}\n",
            (unsigned long long)m_records.size(), (unsigned long long)ready,
            (unsigned long long)dropped, (unsigned long long)unavailable,
            (unsigned long long)unresolved, (unsigned long long)invalid);
        const bool writeFailed = ferror(m_file) != 0;
        const bool closeFailed = fclose(m_file) != 0;
        m_file = nullptr;
        if (writeFailed || closeFailed)
            ErrorLog("Renderer profile write failed for '%s'.", m_path.c_str());
        else
            InfoLog("Renderer profile saved: %llu CPU samples, GPU ready=%llu dropped=%llu unavailable=%llu unresolved=%llu invalid=%llu.",
                (unsigned long long)m_records.size(), (unsigned long long)ready,
                (unsigned long long)dropped, (unsigned long long)unavailable,
                (unsigned long long)unresolved, (unsigned long long)invalid);
    }

private:
    using Clock = std::chrono::steady_clock;
    static constexpr size_t StageCount = size_t(Stage::Count);
    static constexpr size_t RingSize = 32;

    struct Phase {
        Clock::time_point start;
        double cpuMs = 0, gpuMs = 0;
        unsigned gpuIssued = 0;
        bool seen = false, ended = false, gpuReady = false;
    };
    struct Record {
        uint64_t hostFrame = 0, sceneFrame = 0, sceneEpoch = 0, renderSerial = 0;
        Dimensions dimensions{};
        std::array<Phase, StageCount> phases;
        const char* gpuStatus = "unavailable";
    };
    struct QuerySlot {
        std::array<GLuint, StageCount * 2> queries{};
        size_t recordIndex = 0;
        bool pending = false;
    };

    static const char* Name(Stage stage)
    {
        switch (stage) {
        case Stage::Whole: return "whole_renderer";
        case Stage::CenterDecode: return "center_decode";
        case Stage::CenterUpload: return "center_upload";
        case Stage::CenterDrawLos: return "center_draw_los";
        case Stage::BypassDecode: return "bypass_decode";
        case Stage::BypassUpload: return "bypass_upload";
        case Stage::LeftEye: return "left_eye";
        case Stage::RightEye: return "right_eye";
        case Stage::Composite: return "composite";
        default: return "invalid";
        }
    }

    static bool HasGpuInterval(Stage stage)
    {
        return stage != Stage::CenterDecode && stage != Stage::BypassDecode;
    }

    bool BeginFrame(uint64_t hostFrame, uint64_t sceneFrame, uint64_t sceneEpoch, uint64_t renderSerial)
    {
        if (!m_file) return false;
        Harvest();
        // Paused redraws keep their host RunFrame ID. Harvesting is harmless;
        // sampling that ID twice would mix presentation redraws with game ticks.
        if (hostFrame == 0 || hostFrame <= m_lastHostFrame) return false;
        m_lastHostFrame = hostFrame;
        if (hostFrame < m_firstFrame || hostFrame - m_firstFrame >= m_frameCount ||
            m_records.size() >= m_frameCount) return false;
        m_records.emplace_back();
        m_active = &m_records.back();
        m_active->hostFrame = hostFrame;
        m_active->sceneFrame = sceneFrame;
        m_active->sceneEpoch = sceneEpoch;
        m_active->renderSerial = renderSerial;
        m_active->dimensions = m_dimensions;
        m_activeSlot = nullptr;
        if (m_gpuAvailable) {
            m_active->gpuStatus = "dropped_ring_full";
            for (auto& slot : m_slots) {
                if (slot.pending) continue;
                m_activeSlot = &slot;
                slot.recordIndex = m_records.size() - 1;
                m_active->gpuStatus = "pending";
                break;
            }
        }
        return true;
    }

    void BeginStage(Stage stage)
    {
        Phase& phase = m_active->phases[size_t(stage)];
        phase.seen = true;
        phase.start = Clock::now();
        if (m_activeSlot && HasGpuInterval(stage)) {
            glQueryCounter(m_activeSlot->queries[size_t(stage) * 2], GL_TIMESTAMP);
            phase.gpuIssued = 1;
        }
    }

    void EndStage(Stage stage)
    {
        Phase& phase = m_active->phases[size_t(stage)];
        if (m_activeSlot && phase.gpuIssued == 1) {
            glQueryCounter(m_activeSlot->queries[size_t(stage) * 2 + 1], GL_TIMESTAMP);
            phase.gpuIssued = 3;
        }
        phase.cpuMs = std::chrono::duration<double, std::milli>(Clock::now() - phase.start).count();
        phase.ended = true;
    }

    void EndFrame()
    {
        if (m_activeSlot) m_activeSlot->pending = true;
        m_active = nullptr;
        m_activeSlot = nullptr;
    }

    void Harvest()
    {
        if (!m_gpuAvailable) return;
        for (auto& slot : m_slots) {
            if (!slot.pending) continue;
            Record& record = m_records[slot.recordIndex];
            bool available = true;
            for (size_t stage = 0; stage < StageCount && available; ++stage) {
                if (record.phases[stage].gpuIssued != 3) continue;
                for (size_t end = 0; end < 2; ++end) {
                    GLint ready = GL_FALSE;
                    glGetQueryObjectiv(slot.queries[stage * 2 + end], GL_QUERY_RESULT_AVAILABLE, &ready);
                    if (!ready) { available = false; break; }
                }
            }
            if (!available) continue;
            bool valid = true;
            for (size_t stage = 0; stage < StageCount; ++stage) {
                Phase& phase = record.phases[stage];
                if (phase.gpuIssued != 3) continue;
                GLuint64 begin = 0, end = 0;
                // Both results were explicitly checked above. Never request an
                // unavailable result, which could serialize the CPU and GPU.
                glGetQueryObjectui64v(slot.queries[stage * 2], GL_QUERY_RESULT, &begin);
                glGetQueryObjectui64v(slot.queries[stage * 2 + 1], GL_QUERY_RESULT, &end);
                phase.gpuReady = end >= begin;
                if (phase.gpuReady) phase.gpuMs = double(end - begin) / 1e6;
                else valid = false;
            }
            record.gpuStatus = valid ? "ready" : "invalid_timestamp_order";
            slot.pending = false;
        }
    }

    void WriteRecord(const Record& record)
    {
        fprintf(m_file,
            "{\"type\":\"render_profile_frame\",\"frame_id\":%llu,\"scene_frame\":%llu,\"scene_epoch\":%llu,"
            "\"render_serial\":%llu,\"gpu_status\":\"%s\","
            "\"viewport_rect\":[%u,%u,%u,%u],\"framebuffer_size\":[%u,%u],\"phases\":{",
            (unsigned long long)record.hostFrame, (unsigned long long)record.sceneFrame,
            (unsigned long long)record.sceneEpoch, (unsigned long long)record.renderSerial, record.gpuStatus,
            record.dimensions[0], record.dimensions[1], record.dimensions[2], record.dimensions[3],
            record.dimensions[4], record.dimensions[5]);
        for (size_t stage = 0; stage < StageCount; ++stage) {
            if (stage) fputc(',', m_file);
            const Phase& phase = record.phases[stage];
            const char* gpuStatus = !phase.seen ? "not_executed" : !HasGpuInterval(Stage(stage)) ? "cpu_only" :
                phase.gpuReady ? "ready" : record.gpuStatus;
            fprintf(m_file, "\"%s\":{\"cpu_ms\":", Name(Stage(stage)));
            if (phase.ended) fprintf(m_file, "%.9g", phase.cpuMs);
            else fputs("null", m_file);
            fputs(",\"gpu_ms\":", m_file);
            if (phase.gpuReady) fprintf(m_file, "%.9g", phase.gpuMs);
            else fputs("null", m_file);
            fprintf(m_file, ",\"gpu_status\":\"%s\"}", gpuStatus);
        }
        fputs("}}\n", m_file);
    }

    void WriteGlString(GLenum name)
    {
        const auto value = glGetString(name);
        Util::WriteDiagnosticJsonString(m_file, value ? reinterpret_cast<const char*>(value) : "unavailable");
    }

    bool m_configured = false, m_gpuAvailable = false;
    GLint m_queryBits = 0;
    std::string m_path;
    FILE* m_file = nullptr;
    uint64_t m_firstFrame = 121, m_lastHostFrame = 0;
    unsigned m_frameCount = 600;
    std::vector<Record> m_records;
    std::array<QuerySlot, RingSize> m_slots;
    Dimensions m_dimensions{};
    Record* m_active = nullptr;
    QuerySlot* m_activeSlot = nullptr;
};

} // namespace New3D
#endif
