#include "Util.hpp"

#include <spdlog/spdlog.h>

#if _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace nod {
FILE* Fopen(const char* path, const char* mode, FileLockType lock) {
#if _WIN32
  const nowide::wstackstring wpath(path);
  const nowide::wshort_stackstring wmode(mode);
  FILE* fp = _wfopen(wpath.get(), wmode.get());
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
      spdlog::error("flock {}: {}", path, strerror(errno));
#endif
  }

  return fp;
}

bool CheckFreeSpace(const char* path, size_t reqSz) {
#if _WIN32
  ULARGE_INTEGER freeBytes;
  const nowide::wstackstring wpath(path);
  std::array<wchar_t, 1024> buf{};
  wchar_t* end = nullptr;
  DWORD ret = GetFullPathNameW(wpath.get(), 1024, buf.data(), &end);
  if (ret == 0 || ret > 1024) {
    spdlog::error("GetFullPathNameW {}", path);
    return false;
  }
  if (end != nullptr) {
    end[0] = L'\0';
  }
  if (!GetDiskFreeSpaceExW(buf.data(), &freeBytes, nullptr, nullptr)) {
    spdlog::error("GetDiskFreeSpaceExW {}: {}", path, GetLastError());
    return false;
  }
  return reqSz < freeBytes.QuadPart;
#else
  struct statvfs svfs;
  if (statvfs(path, &svfs)) {
    spdlog::error("statvfs {}: {}", path, strerror(errno));
    return false;
  }
  return reqSz < svfs.f_frsize * svfs.f_bavail;
#endif
}
} // namespace nod
