#ifndef INCLUDED_DIAGNOSTIC_FILE_H
#define INCLUDED_DIAGNOSTIC_FILE_H

#include <cstdio>
#include <filesystem>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef SUPERMODEL_WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace Util {

// An existence check is not a reservation. O_EXCL protects prior reports even
// if another process creates the destination after command-line validation.
inline FILE* OpenExclusiveDiagnostic(const std::string& path)
{
#ifdef SUPERMODEL_WIN32
  const int fd = _wopen(std::filesystem::path(path).c_str(),
                       _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
  if (fd < 0) return nullptr;
  FILE* file = _fdopen(fd, "wb");
  if (!file) _close(fd);
#else
  const int fd = open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0666);
  if (fd < 0) return nullptr;
  FILE* file = fdopen(fd, "wb");
  if (!file) close(fd);
#endif
  return file;
}

inline void WriteDiagnosticJsonString(FILE* file, const std::string& value)
{
  fputc('"', file);
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') { fputc('\\', file); fputc(c, file); }
    else if (c < 0x20) fprintf(file, "\\u%04x", unsigned(c));
    else fputc(c, file);
  }
  fputc('"', file);
}

} // namespace Util
#endif
