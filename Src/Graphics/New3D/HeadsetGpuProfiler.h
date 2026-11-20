/* Optional bounded headset GPU timing. GPL-3.0-or-later, like Supermodel.
 * GPU timestamps are a separate GL timeline, never QPC timestamps or busy time.
 * Backend is injected so queue ownership and nonblocking reads can be tested
 * without a GL context. No file IO, allocation, waits, flushes or fences here.
 */
#ifndef INCLUDED_HEADSET_GPU_PROFILER_H
#define INCLUDED_HEADSET_GPU_PROFILER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace HeadsetGpuTiming {
enum class Marker : unsigned {
    PairBegin, LeftBegin, LeftGeometryEnd, LeftLayerEnd, LeftComfortEnd,
    LeftHUDEnd, LeftMenuEnd, LeftAlphaEnd, LeftCopyEnd,
    RightBegin, RightGeometryEnd, RightLayerEnd, RightComfortEnd,
    RightHUDEnd, RightMenuEnd, RightAlphaEnd, RightCopyEnd, PairEnd, Count
};
enum class Status : unsigned {
    Ready, ReadyPartial, Unavailable, DroppedRing, InvalidTimestampOrderOrRange,
    InvalidMarkers, UnresolvedShutdown, ContextLost, OwnerRejected, CapacityExhausted
};
struct Metadata {
    uint64_t sceneFrame=0, sceneEpoch=0, nativeFrame=0;
    int64_t displayTimeNs=0;
    std::array<int,4> dimensions{};
    bool theater=false;
};
struct Counters {
    uint64_t pairs=0, sampled=0, ready=0, partial=0, invalid=0, dropped=0,
        unavailable=0, unresolved=0, contextLost=0, rejected=0,
        capacitySkipped=0, markersRead=0, firstSerial=0, lastSerial=0;
};

template<class Backend> class Probe {
public:
    static constexpr unsigned MarkerCount=unsigned(Marker::Count);
    static constexpr unsigned RingSize=16;
    static constexpr uint64_t MaxPairs=20000;
    static constexpr uint32_t FullMask=(uint32_t(1)<<MarkerCount)-1;

    class Pair {
    public:
        explicit Pair(Probe* owner):m_owner(owner) {}
        ~Pair() { if(m_owner) m_owner->Finish(m_success); }
        void Complete(bool success) { m_success=success; }
        Pair(const Pair&)=delete;
        Pair& operator=(const Pair&)=delete;
    private:
        Probe* m_owner=nullptr;
        bool m_success=false;
    };

    // Call before active rendering, after trace startup and with the GL context
    // current. A disabled/missing trace can become active later; it is not a
    // permanent failure. Repeated same-owner configuration does not reset IDs.
    void Configure(const void* owner,bool requested) {
        if(!requested || !Backend::TraceEnabled()) return;
        if(m_configured) return;
        const auto context=Backend::Context();
        if(!context) return;
        m_owner=owner; m_context=context; m_thread=Backend::Thread();
        m_configured=true; m_counts={};
        m_available=Backend::Supported(m_bits) && m_bits>0 && m_bits<=64;
        if(m_available) {
            for(auto& slot:m_slots) {
                Backend::Generate(MarkerCount,slot.queries.data());
                for(auto query:slot.queries) if(!query) m_available=false;
            }
            if(!m_available) {
                for(auto& slot:m_slots) {
                    Backend::Delete(MarkerCount,slot.queries.data());
                    slot.queries.fill(0);
                }
            }
        }
        const auto now=Backend::Now();
        Backend::Emit("gpu.config",now,now,0,1,m_bits,RingSize,MarkerCount);
    }

    Pair Begin(const void* owner,bool requested,const Metadata& metadata) {
        return Pair(Start(owner,requested,metadata)?this:nullptr);
    }

    void Eye(const void* owner,unsigned eye,bool direct) {
        if(!m_current || owner!=m_owner || eye>1) return;
        m_eye=eye;
        if(direct) m_current->directMask|=uint32_t(1)<<eye;
        Mark(owner,eye==0?Marker::LeftBegin:Marker::RightBegin);
    }
    void GeometryEnd(const void* owner) {
        Mark(owner,m_eye==0?Marker::LeftGeometryEnd:Marker::RightGeometryEnd);
    }
    void LayerEnd(const void* owner) {
        Mark(owner,m_eye==0?Marker::LeftLayerEnd:Marker::RightLayerEnd);
    }
    void EyeMark(const void* owner,Marker leftMarker) {
        Mark(owner,Marker(unsigned(leftMarker)+(m_eye==0?0:8)));
    }
    void Mark(const void* owner,Marker marker) {
        if(!m_current || owner!=m_owner || !m_current->sampled) return;
        if(!Backend::TraceEnabled() || !ContextMatches()) {
            m_current->contextLost=true;
            return;
        }
        const unsigned index=unsigned(marker);
        if(index>=MarkerCount) {m_current->invalidMarkers=true;return;}
        const uint32_t bit=uint32_t(1)<<index;
        if(m_current->issued&bit) {m_current->invalidMarkers=true;return;}
        m_current->begin[index]=Backend::Now();
        Backend::Stamp(m_current->queries[index]);
        m_current->end[index]=Backend::Now();
        m_current->issued|=bit;
    }

    void Close(const void* owner) {
        if(!m_configured || owner!=m_owner) return;
        if(m_current) Finish(false);
        const bool context=ContextMatches();
        // Main normally closes its capture before renderer destruction. In that
        // case pending queries remain explicitly unresolved; do not harvest or
        // wait to improve coverage after the recorded trace has finished.
        if(Backend::TraceEnabled() && context) Harvest();
        for(auto& slot:m_slots) {
            if(slot.pending) {
                ++m_counts.unresolved;
                if(!context) ++m_counts.contextLost;
                if(Backend::TraceEnabled()) EmitResult(slot,context?Status::UnresolvedShutdown:Status::ContextLost);
                slot.pending=false;
            }
            if(context) Backend::Delete(MarkerCount,slot.queries.data());
            slot=Slot{};
        }
        // Bounded shutdown-only log; never called from the active frame path.
        Backend::Summary(m_bits,m_counts);
        m_current=nullptr; m_configured=false; m_available=false; m_owner=nullptr;
        m_context=0; m_thread=0; m_bits=0;
    }

    const Counters& Counts() const { return m_counts; }
    unsigned Pending() const {
        unsigned count=0;for(const auto& slot:m_slots)count+=slot.pending?1:0;return count;
    }

private:
    struct Slot {
        std::array<unsigned,MarkerCount> queries{};
        std::array<uint64_t,MarkerCount> begin{},end{};
        Metadata metadata;
        uint64_t serial=0, pairBegin=0, pairEnd=0;
        uint32_t issued=0,directMask=0;
        bool pending=false,sampled=false,invalidMarkers=false,contextLost=false;
    };
    bool ContextMatches() const {
        return Backend::Context()==m_context && Backend::Thread()==m_thread;
    }
    bool Start(const void* owner,bool requested,const Metadata& metadata) {
        if(!requested || !Backend::TraceEnabled()) return false;
        Configure(owner,true);
        if(!m_configured) return false;
        if(owner!=m_owner || !ContextMatches() || m_current) {
            ++m_counts.rejected;
            return false;
        }
        Harvest();
        if(m_counts.pairs>=MaxPairs) {++m_counts.capacitySkipped;return false;}
        ++m_counts.pairs;
        const auto serial=++m_serial;
        if(!m_counts.firstSerial)m_counts.firstSerial=serial;
        m_counts.lastSerial=serial;
        m_scratch=Slot{};
        m_current=&m_scratch;
        if(m_available) {
            for(auto& slot:m_slots) {
                if(slot.pending) continue;
                const auto queries=slot.queries;
                slot=Slot{};slot.queries=queries;slot.sampled=true;
                m_current=&slot;++m_counts.sampled;
                break;
            }
        }
        m_current->metadata=metadata;
        m_current->serial=serial;
        m_current->pairBegin=Backend::Now();
        m_eye=0;
        Mark(owner,Marker::PairBegin);
        return true;
    }
    void Finish(bool success) {
        if(!m_current) return;
        Mark(m_owner,Marker::PairEnd);
        auto& slot=*m_current;
        slot.pairEnd=Backend::Now();
        const auto& data=slot.metadata;
        Backend::Emit("gpu.pair",slot.pairBegin,slot.pairEnd,slot.serial,
            int64_t(data.sceneFrame),int64_t(data.sceneEpoch),data.displayTimeNs,int64_t(data.nativeFrame));
        Backend::Emit("gpu.layout",slot.pairBegin,slot.pairEnd,slot.serial,
            data.dimensions[0],data.dimensions[1],data.dimensions[2],data.dimensions[3]);
        Backend::Emit("gpu.state",slot.pairBegin,slot.pairEnd,slot.serial,
            data.theater,slot.directMask,success,slot.issued);
        if(slot.sampled) slot.pending=true;
        else if(m_available) {++m_counts.dropped;EmitResult(slot,Status::DroppedRing);}
        else {++m_counts.unavailable;EmitResult(slot,Status::Unavailable);}
        m_current=nullptr;
    }
    void EmitResult(const Slot& slot,Status status,uint64_t begin=0,uint64_t end=0) {
        if(!begin)begin=Backend::Now();
        if(!end)end=begin;
        Backend::Emit("gpu.result",begin,end,slot.serial,unsigned(status),slot.issued,
            int64_t(m_serial-slot.serial),0);
    }
    void Harvest() {
        if(!m_available || !Backend::TraceEnabled() || !ContextMatches()) return;
        for(auto& slot:m_slots) {
            if(!slot.pending) continue;
            bool ready=true;
            // Every issued query is explicitly checked before any result read.
            // A false result skips this slot; no spin, flush, fence or wait.
            for(unsigned i=0;i<MarkerCount && ready;++i)
                if(slot.issued&(uint32_t(1)<<i))ready=Backend::Available(slot.queries[i]);
            if(!ready)continue;
            const auto begin=Backend::Now();
            bool valid=true;uint64_t previous=0;bool havePrevious=false;
            for(unsigned i=0;i<MarkerCount;++i) {
                if(!(slot.issued&(uint32_t(1)<<i)))continue;
                const uint64_t value=Backend::Read(slot.queries[i]);
                const bool range=value<=uint64_t(std::numeric_limits<int64_t>::max()) &&
                    (m_bits==64 || value<(uint64_t(1)<<m_bits));
                if(!range || (havePrevious && value<previous))valid=false;
                Backend::Emit("gpu.mark",slot.begin[i],slot.end[i],slot.serial,
                    i,range?int64_t(value):-1,0,0);
                previous=value;havePrevious=true;++m_counts.markersRead;
            }
            Status status=Status::Ready;
            if(slot.contextLost) status=Status::ContextLost;
            else if(slot.invalidMarkers) status=Status::InvalidMarkers;
            else if(!valid) status=Status::InvalidTimestampOrderOrRange;
            else if(slot.issued!=FullMask) status=Status::ReadyPartial;
            if(status==Status::Ready)++m_counts.ready;
            else if(status==Status::ReadyPartial)++m_counts.partial;
            else {++m_counts.invalid;if(status==Status::ContextLost)++m_counts.contextLost;}
            EmitResult(slot,status,begin,Backend::Now());
            slot.pending=false;
        }
    }

    std::array<Slot,RingSize> m_slots;
    Slot m_scratch;
    Slot* m_current=nullptr;
    const void* m_owner=nullptr;
    uintptr_t m_context=0;
    uint64_t m_thread=0,m_serial=0;
    unsigned m_bits=0,m_eye=0;
    bool m_configured=false,m_available=false;
    Counters m_counts;
};
}
#endif
