#ifndef HARLEY_MENU_PREFERENCES_H
#define HARLEY_MENU_PREFERENCES_H

#include "Util/ConfigBuilders.h"
#include "Util/NewConfig.h"
#include <atomic>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace HarleyMenuPreferences {
inline bool Number(const Util::Config::Node& preferences,const char* key,
                   double minimum,double maximum,double& result) {
  if (preferences[key].Empty()) return false;
  const auto text=preferences[key].ValueAs<std::string>();
  char* end=nullptr; const double value=std::strtod(text.c_str(),&end);
  if (end==text.c_str() || *end || !std::isfinite(value) || value<minimum || value>maximum) return false;
  result=value; return true;
}

// Import only settings exposed by the menu. Legacy/unknown values stay in the
// sidecar for lossless round trips, but cannot replace explicit game settings.
inline void ApplyViewOverrides(Util::Config::Node& config,const Util::Config::Node& preferences,bool headset) {
  // Preserve the legacy sidecar key on disk, but never apply stereo cinema.
  config.Set("StereoTheaterDepth",0);
  struct Range {const char* key; double minimum,maximum;};
  for (const auto& setting:std::array<Range,5>{{
      {"StereoRiderUp",0,8},{"StereoRiderBack",0,5},{"StereoTheaterScreenAngle",40,100},
      {"StereoHorizonFov",60,120},{"OpenXRResolutionScale",.5,1.5}}}) {
    double value=0;
    if (Number(preferences,setting.key,setting.minimum,setting.maximum,value) &&
        (std::string(setting.key)!="StereoHorizonFov" || !headset)) config.Set(setting.key,float(value));
  }
}

// This mode is a launch choice, never a saved preference. Existing explicit
// free-ride/HUD flags remain authoritative until the user changes this toggle.
// The Model3 timer patch is cached at game load, so both halves change together
// only through the launcher's controlled restart, never during a running ride.
class StartupDreamRide {
public:
  void Initialize(bool freeRide,bool hideHUD) {
    m_initialFreeRide=freeRide; m_initialHideHUD=hideHUD;
    m_requested=freeRide && hideHUD; m_touched=false;
  }
  bool Request(bool startup,bool canRestart,bool supported,double value) {
    if (!startup || !canRestart || !supported || (value!=0.0 && value!=1.0)) return false;
    m_requested=value==1.0; m_touched=true; return true;
  }
  bool Enabled() const { return m_requested; }
  bool Changed() const {
    return m_touched && (m_requested!=m_initialFreeRide || m_requested!=m_initialHideHUD);
  }
private:
  bool m_initialFreeRide=false,m_initialHideHUD=false,m_requested=false,m_touched=false;
};

inline bool StartupRestartRequired(bool startup,float requestedScale,float initialScale,bool dreamRideChanged=false) {
  return startup && (std::abs(requestedScale-initialScale)>.0001f || dreamRideChanged);
}

// The supervisor applies one explicit paired mode override for 77..80. No
// sidecar file is needed, and 75/76 preserve the original launch arguments.
inline int StartupRestartCode(bool calibrate,bool dreamRideChanged,bool dreamRideEnabled) {
  if (!dreamRideChanged) return calibrate?76:75;
  if (calibrate) return dreamRideEnabled?79:80;
  return dreamRideEnabled?77:78;
}

namespace Detail {
constexpr size_t MaximumBytes = 65536;
using Values = std::map<std::string, std::string>;

inline bool ValidName(const std::string& name) {
  return !name.empty() && name.find('\0')==std::string::npos && name.find_first_of("\r\n;[]=,\"/")==std::string::npos &&
    name.front()!=' ' && name.back()!=' ' && name.front()!='\t' && name.back()!='\t';
}
inline bool Snapshot(const Util::Config::Node& root, Values& values) {
  if (root.Key()!="Global") return false;
  for (const auto& child:root) {
    if (!ValidName(child.Key())) return false;
    if (child.HasChildren()) {
      if (!values.emplace(child.Key()+"/", "").second) return false;
      for (const auto& leaf:child) {
        if (!ValidName(leaf.Key()) || leaf.HasChildren()) return false;
        if (!leaf.Empty() && !values.emplace(child.Key()+"/"+leaf.Key(),leaf.ValueAs<std::string>()).second) return false;
      }
    } else if (!child.Empty() && !values.emplace("Global/"+child.Key(),child.ValueAs<std::string>()).second) return false;
  }
  return true;
}
inline bool Serialize(const Util::Config::Node& root, std::string& text) {
  std::ostringstream output;
  output << "; HarleyVR menu preferences. Original Supermodel.ini and arcade NVRAM are preserved.\n";
  auto section=[&](const Util::Config::Node& node) {
    output << "\n[ " << node.Key() << " ]\n";
    for (const auto& leaf:node) if (leaf.IsLeaf() && !leaf.Empty()) {
      const auto value=leaf.ValueAs<std::string>();
      // This INI grammar has no escaped quote/newline representation. Refuse
      // unrepresentable values instead of silently damaging unknown settings.
      if (value.find_first_of("\r\n\0\"",0,4)!=std::string::npos) return false;
      output << leaf.Key() << " = \"" << value << "\"\n";
      if (output.tellp()>std::streamoff(MaximumBytes)) return false;
    }
    return true;
  };
  if (!section(root)) return false;
  for (const auto& child:root) if (child.HasChildren() && !section(child)) return false;
  text=output.str(); return !text.empty() && text.size()<=MaximumBytes;
}
struct Temporary {
  std::filesystem::path path;
  bool owned=false;
  ~Temporary() { if (owned) { std::error_code error; std::filesystem::remove(path,error); } }
};
}

// Returns true only after the complete, verified sidecar replaces its target.
// Failure never truncates the original. The caller must retain its dirty flag.
// Runs only on the owner thread; it writes no emulator config or NVRAM.
inline bool WriteAtomic(const std::string& filename, const Util::Config::Node& preferences) {
  Detail::Temporary temporary;
  try {
    Detail::Values expected;
    std::string text;
    if (!Detail::Snapshot(preferences,expected) || !Detail::Serialize(preferences,text)) return false;
    std::error_code error;
    const auto destination=std::filesystem::absolute(std::filesystem::path(filename),error);
    if (error || destination.filename().empty() || !std::filesystem::is_directory(destination.parent_path(),error) || error) return false;
    const auto status=std::filesystem::symlink_status(destination,error);
    if (error && error!=std::errc::no_such_file_or_directory) return false;
    if (status.type()!=std::filesystem::file_type::not_found && !std::filesystem::is_regular_file(status)) return false;
    static std::atomic<unsigned long long> serial{0};
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt=0;attempt<16;++attempt) {
      temporary.path=destination;
      temporary.path += ".tmp-"+std::to_string(stamp)+"-"+std::to_string(++serial);
#ifdef _WIN32
      HANDLE file=CreateFileW(temporary.path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
      if (file==INVALID_HANDLE_VALUE) {
        if (GetLastError()==ERROR_FILE_EXISTS || GetLastError()==ERROR_ALREADY_EXISTS) continue;
        return false;
      }
      temporary.owned=true;
      DWORD written=0;
      const bool complete=WriteFile(file,text.data(),DWORD(text.size()),&written,nullptr) && written==text.size() && FlushFileBuffers(file);
      const bool closed=CloseHandle(file)!=0;
#else
      const int file=::open(temporary.path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
      if (file<0) { if (errno==EEXIST) continue; return false; }
      temporary.owned=true;
      size_t offset=0;
      while (offset<text.size()) {
        const auto written=::write(file,text.data()+offset,text.size()-offset);
        if (written<0 && errno==EINTR) continue;
        if (written<=0) break;
        offset+=size_t(written);
      }
      const bool complete=offset==text.size() && ::fsync(file)==0;
      const bool closed=::close(file)==0;
#endif
      if (!complete || !closed) return false;
      std::ifstream verify(temporary.path,std::ios::binary);
      const std::string actual((std::istreambuf_iterator<char>(verify)),std::istreambuf_iterator<char>());
      if (verify.bad() || actual!=text) return false;
      verify.close();
      Util::Config::Node parsed("Global"); Detail::Values observed;
      if (Util::Config::FromINIFile(&parsed,temporary.path.string()) || !Detail::Snapshot(parsed,observed) || observed!=expected) return false;
#ifdef _WIN32
      if (!MoveFileExW(temporary.path.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return false;
#else
      std::filesystem::rename(temporary.path,destination,error);
      if (error) return false;
#endif
      temporary.owned=false; return true;
    }
  } catch (...) { return false; }
  return false;
}
}
#endif
