#pragma once

#if _WIN32
#include <array>
#include <cwctype>
#include <direct.h>
#include <nowide/stackstring.hpp>
#include <winapifamily.h>
#if defined(WINAPI_FAMILY) && WINAPI_FAMILY != WINAPI_FAMILY_DESKTOP_APP
#define WINDOWS_STORE 1
#else
#define WINDOWS_STORE 0
#endif
#else
#include <cctype>
#include <cerrno>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif
#include <sys/stat.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#ifdef _MSC_VER
#pragma warning(disable : 4996)

#include <sys/stat.h>

#if !defined(S_ISREG) && defined(S_IFMT) && defined(S_IFREG)
#define S_ISREG(m) (((m)&S_IFMT) == S_IFREG)
#endif

#if !defined(S_ISDIR) && defined(S_IFMT) && defined(S_IFDIR)
#define S_ISDIR(m) (((m)&S_IFMT) == S_IFDIR)
#endif

#if !defined(S_ISLNK)
#define S_ISLNK(m) 0
#endif
#endif

#undef min
#undef max

namespace nod {
/* define our own min/max to avoid MSVC BS */
template <typename T>
constexpr T min(T a, T b) {
  return a < b ? a : b;
}
template <typename T>
constexpr T max(T a, T b) {
  return a > b ? a : b;
}

/* template-based div for flexible typing and avoiding a library call */
template <typename T>
constexpr auto div(T a, T b) {
  struct DivTp {
    T quot, rem;
  };
  return DivTp{a / b, a % b};
}

/* filesystem char type */
#if _WIN32
static inline int Mkdir(const char* path, int) {
  const nowide::wstackstring str(path);
  return _wmkdir(str.get());
}

using Sstat = struct ::_stat64;
static inline int Stat(const char* path, Sstat* statout) {
  const nowide::wstackstring wpath(path);
  return _wstat64(wpath.get(), statout);
}
#else
static inline int Mkdir(const char* path, mode_t mode) { return mkdir(path, mode); }

typedef struct stat Sstat;
static inline int Stat(const char* path, Sstat* statout) { return stat(path, statout); }
#endif

static inline int StrCaseCmp(const char* str1, const char* str2) {
#ifdef _MSC_VER
  return _stricmp(str1, str2);
#else
  return strcasecmp(str1, str2);
#endif
}

#ifndef ROUND_UP_32
#define ROUND_UP_32(val) (((val) + 31) & ~31)
#define ROUND_UP_16(val) (((val) + 15) & ~15)
#endif

enum class FileLockType { None = 0, Read, Write };
FILE* Fopen(const char* path, const char* mode, FileLockType lock = FileLockType::None);

static inline int FSeek(FILE* fp, int64_t offset, int whence) {
#if _WIN32
  return _fseeki64(fp, offset, whence);
#elif __APPLE__ || __FreeBSD__
  return fseeko(fp, offset, whence);
#else
  return fseeko64(fp, offset, whence);
#endif
}

static inline int64_t FTell(FILE* fp) {
#if _WIN32
  return _ftelli64(fp);
#elif __APPLE__ || __FreeBSD__
  return ftello(fp);
#else
  return ftello64(fp);
#endif
}

bool CheckFreeSpace(const char* path, size_t reqSz);

} // namespace nod
