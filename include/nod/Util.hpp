#pragma once

#if _WIN32 && UNICODE
#include <cwctype>
#include <direct.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
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
#include <logvisor/logvisor.hpp>
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
  struct DivTp { T quot, rem; };
  return DivTp{a / b, a % b};
}

/* Log Module */
extern logvisor::Module LogModule;

/* filesystem char type */
#if _WIN32 && UNICODE
#define NOD_UCS2 1
typedef struct _stat Sstat;
static inline int Mkdir(const wchar_t* path, int) { return _wmkdir(path); }
static inline int Stat(const wchar_t* path, Sstat* statout) { return _wstat(path, statout); }
#else
typedef struct stat Sstat;
static inline int Mkdir(const char* path, mode_t mode) { return mkdir(path, mode); }
static inline int Stat(const char* path, Sstat* statout) { return stat(path, statout); }
#endif

/* String-converting views */
#if NOD_UCS2
typedef wchar_t SystemChar;
typedef std::wstring SystemString;
typedef std::wstring_view SystemStringView;
static inline void ToLower(SystemString& str) { std::transform(str.begin(), str.end(), str.begin(), towlower); }
static inline void ToUpper(SystemString& str) { std::transform(str.begin(), str.end(), str.begin(), towupper); }
static inline size_t StrLen(const SystemChar* str) { return wcslen(str); }
class SystemUTF8Conv {
  std::string m_utf8;

public:
  explicit SystemUTF8Conv(SystemStringView str) {
    int len = WideCharToMultiByte(CP_UTF8, 0, str.data(), str.size(), nullptr, 0, nullptr, nullptr);
    m_utf8.assign(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, str.data(), str.size(), &m_utf8[0], len, nullptr, nullptr);
  }
  std::string_view utf8_str() const { return m_utf8; }
  const char* c_str() const { return m_utf8.c_str(); }
};
class SystemStringConv {
  std::wstring m_sys;

public:
  explicit SystemStringConv(std::string_view str) {
    int len = MultiByteToWideChar(CP_UTF8, 0, str.data(), str.size(), nullptr, 0);
    m_sys.assign(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), str.size(), &m_sys[0], len);
  }
  SystemStringView sys_str() const { return m_sys; }
  const SystemChar* c_str() const { return m_sys.c_str(); }
};
#ifndef _SYS_STR
#define _SYS_STR(val) L##val
#endif
#else
typedef char SystemChar;
typedef std::string SystemString;
typedef std::string_view SystemStringView;
static inline void ToLower(SystemString& str) { std::transform(str.begin(), str.end(), str.begin(), tolower); }
static inline void ToUpper(SystemString& str) { std::transform(str.begin(), str.end(), str.begin(), toupper); }
static inline size_t StrLen(const SystemChar* str) { return strlen(str); }
class SystemUTF8Conv {
  std::string_view m_utf8;

public:
  explicit SystemUTF8Conv(SystemStringView str) : m_utf8(str) {}
  std::string_view utf8_str() const { return m_utf8; }
  const char* c_str() const { return m_utf8.data(); }
};
class SystemStringConv {
  SystemStringView m_sys;

public:
  explicit SystemStringConv(std::string_view str) : m_sys(str) {}
  SystemStringView sys_str() const { return m_sys; }
  const SystemChar* c_str() const { return m_sys.data(); }
};
#ifndef _SYS_STR
#define _SYS_STR(val) val
#endif
#endif

static inline void Unlink(const SystemChar* file) {
#if NOD_UCS2
  _wunlink(file);
#else
  unlink(file);
#endif
}

static inline int StrCmp(const SystemChar* str1, const SystemChar* str2) {
#if NOD_UCS2
  return wcscmp(str1, str2);
#else
  return strcmp(str1, str2);
#endif
}

static inline int StrCaseCmp(const SystemChar* str1, const SystemChar* str2) {
#if NOD_UCS2
  return _wcsicmp(str1, str2);
#else
  return strcasecmp(str1, str2);
#endif
}

#undef bswap16
#undef bswap32
#undef bswap64
/* Type-sensitive byte swappers */
template <typename T>
static inline T bswap16(T val) {
#if __GNUC__
  return __builtin_bswap16(val);
#elif _WIN32
  return _byteswap_ushort(val);
#else
  return (val = (val << 8) | ((val >> 8) & 0xFF));
#endif
}

template <typename T>
static inline T bswap32(T val) {
#if __GNUC__
  return __builtin_bswap32(val);
#elif _WIN32
  return _byteswap_ulong(val);
#else
  val = (val & 0x0000FFFF) << 16 | (val & 0xFFFF0000) >> 16;
  val = (val & 0x00FF00FF) << 8 | (val & 0xFF00FF00) >> 8;
  return val;
#endif
}

template <typename T>
static inline T bswap64(T val) {
#if __GNUC__
  return __builtin_bswap64(val);
#elif _WIN32
  return _byteswap_uint64(val);
#else
  return ((val & 0xFF00000000000000ULL) >> 56) | ((val & 0x00FF000000000000ULL) >> 40) |
         ((val & 0x0000FF0000000000ULL) >> 24) | ((val & 0x000000FF00000000ULL) >> 8) |
         ((val & 0x00000000FF000000ULL) << 8) | ((val & 0x0000000000FF0000ULL) << 24) |
         ((val & 0x000000000000FF00ULL) << 40) | ((val & 0x00000000000000FFULL) << 56);
#endif
}

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
static inline int16_t SBig(int16_t val) { return bswap16(val); }
static inline uint16_t SBig(uint16_t val) { return bswap16(val); }
static inline int32_t SBig(int32_t val) { return bswap32(val); }
static inline uint32_t SBig(uint32_t val) { return bswap32(val); }
static inline int64_t SBig(int64_t val) { return bswap64(val); }
static inline uint64_t SBig(uint64_t val) { return bswap64(val); }

static inline int16_t SLittle(int16_t val) { return val; }
static inline uint16_t SLittle(uint16_t val) { return val; }
static inline int32_t SLittle(int32_t val) { return val; }
static inline uint32_t SLittle(uint32_t val) { return val; }
static inline int64_t SLittle(int64_t val) { return val; }
static inline uint64_t SLittle(uint64_t val) { return val; }
#else
static inline int16_t SLittle(int16_t val) { return bswap16(val); }
static inline uint16_t SLittle(uint16_t val) { return bswap16(val); }
static inline int32_t SLittle(int32_t val) { return bswap32(val); }
static inline uint32_t SLittle(uint32_t val) { return bswap32(val); }
static inline int64_t SLittle(int64_t val) { return bswap64(val); }
static inline uint64_t SLittle(uint64_t val) { return bswap64(val); }

static inline int16_t SBig(int16_t val) { return val; }
static inline uint16_t SBig(uint16_t val) { return val; }
static inline int32_t SBig(int32_t val) { return val; }
static inline uint32_t SBig(uint32_t val) { return val; }
static inline int64_t SBig(int64_t val) { return val; }
static inline uint64_t SBig(uint64_t val) { return val; }
#endif

#ifndef ROUND_UP_32
#define ROUND_UP_32(val) (((val) + 31) & ~31)
#define ROUND_UP_16(val) (((val) + 15) & ~15)
#endif

enum class FileLockType { None = 0, Read, Write };
static inline FILE* Fopen(const SystemChar* path, const SystemChar* mode, FileLockType lock = FileLockType::None) {
#if NOD_UCS2
  FILE* fp = _wfopen(path, mode);
  if (!fp)
    return nullptr;
#else
  FILE* fp = fopen(path, mode);
  if (!fp)
    return nullptr;
#endif

  if (lock != FileLockType::None) {
#if _WIN32
    OVERLAPPED ov = {};
    LockFileEx((HANDLE)(uintptr_t)_fileno(fp), (lock == FileLockType::Write) ? LOCKFILE_EXCLUSIVE_LOCK : 0, 0, 0, 1,
               &ov);
#else
    if (flock(fileno(fp), ((lock == FileLockType::Write) ? LOCK_EX : LOCK_SH) | LOCK_NB))
      LogModule.report(logvisor::Error, FMT_STRING("flock {}: {}"), path, strerror(errno));
#endif
  }

  return fp;
}

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

static inline bool CheckFreeSpace(const SystemChar* path, size_t reqSz) {
#if _WIN32
  ULARGE_INTEGER freeBytes;
  wchar_t buf[1024];
  wchar_t* end;
  DWORD ret = GetFullPathNameW(path, 1024, buf, &end);
  if (!ret || ret > 1024) {
    LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("GetFullPathNameW {}")), path);
    return false;
  }
  if (end)
    end[0] = L'\0';
  if (!GetDiskFreeSpaceExW(buf, &freeBytes, nullptr, nullptr)) {
    LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("GetDiskFreeSpaceExW {}: {}")), path, GetLastError());
    return false;
  }
  return reqSz < freeBytes.QuadPart;
#else
  struct statvfs svfs;
  if (statvfs(path, &svfs)) {
    LogModule.report(logvisor::Error, FMT_STRING("statvfs {}: {}"), path, strerror(errno));
    return false;
  }
  return reqSz < svfs.f_frsize * svfs.f_bavail;
#endif
}

} // namespace nod
