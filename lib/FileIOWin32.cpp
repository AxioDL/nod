#include <cstdint>
#if _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include "nod/IFileIO.hpp"
#include "nod/Util.hpp"

#include <logvisor/logvisor.hpp>

#include <nowide/convert.hpp>
#include <nowide/stackstring.hpp>

namespace nod {

class FileIOWin32 : public IFileIO {
  std::wstring m_wpath;
  int64_t m_maxWriteSize;

public:
  FileIOWin32(std::string_view path, int64_t maxWriteSize)
  : m_wpath(nowide::widen(path))
  , m_maxWriteSize(maxWriteSize) {}

  bool exists() override {
#if !WINDOWS_STORE
    HANDLE fp = CreateFileW(m_wpath.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
#else
    HANDLE fp = CreateFile2(m_path.get(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
#endif
    if (fp == INVALID_HANDLE_VALUE)
      return false;
    CloseHandle(fp);
    return true;
  }

  uint64_t size() override {
#if !WINDOWS_STORE
    HANDLE fp = CreateFileW(m_wpath.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
#else
    HANDLE fp = CreateFile2(m_path.get(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
#endif
    if (fp == INVALID_HANDLE_VALUE)
      return 0;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(fp, &sz)) {
      CloseHandle(fp);
      return 0;
    }
    CloseHandle(fp);
    return sz.QuadPart;
  }

  struct WriteStream : public IFileIO::IWriteStream {
    HANDLE fp;
    int64_t m_maxWriteSize;
    WriteStream(std::wstring_view wpath, int64_t maxWriteSize, bool& err) : m_maxWriteSize(maxWriteSize) {
#if !WINDOWS_STORE
      fp = CreateFileW(wpath.data(), GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                       nullptr);
#else
      fp = CreateFile2(wpath.data(), GENERIC_WRITE, FILE_SHARE_WRITE, CREATE_ALWAYS, nullptr);
#endif
      if (fp == INVALID_HANDLE_VALUE) {
        const nowide::stackstring path(wpath.data(), wpath.data() + wpath.size());
        LogModule.report(logvisor::Error, FMT_STRING("unable to open '{}' for writing"), path.get());
        err = true;
      }
    }
    WriteStream(std::wstring_view wpath, uint64_t offset, int64_t maxWriteSize, bool& err)
    : m_maxWriteSize(maxWriteSize) {
#if !WINDOWS_STORE
      fp = CreateFileW(wpath.data(), GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                       nullptr);
#else
      fp = CreateFile2(wpath.data(), GENERIC_WRITE, FILE_SHARE_WRITE, OPEN_ALWAYS, nullptr);
#endif
      if (fp == INVALID_HANDLE_VALUE) {
        const nowide::stackstring path(wpath.data(), wpath.data() + wpath.size());
        LogModule.report(logvisor::Error, FMT_STRING("unable to open '{}' for writing"), path.get());
        err = true;
        return;
      }
      LARGE_INTEGER lioffset;
      lioffset.QuadPart = offset;
      SetFilePointerEx(fp, lioffset, nullptr, FILE_BEGIN);
    }
    ~WriteStream() override { CloseHandle(fp); }
    uint64_t write(const void* buf, uint64_t length) override {
      if (m_maxWriteSize >= 0) {
        LARGE_INTEGER li = {};
        LARGE_INTEGER res;
        SetFilePointerEx(fp, li, &res, FILE_CURRENT);
        if (res.QuadPart + int64_t(length) > m_maxWriteSize) {
          LogModule.report(logvisor::Error, FMT_STRING("write operation exceeds file's {}-byte limit"),
                           m_maxWriteSize);
          return 0;
        }
      }

      DWORD ret = 0;
      WriteFile(fp, buf, length, &ret, nullptr);
      return ret;
    }
  };
  std::unique_ptr<IWriteStream> beginWriteStream() const override {
    bool err = false;
    auto ret = std::make_unique<WriteStream>(m_wpath, m_maxWriteSize, err);

    if (err) {
      return nullptr;
    }

    return ret;
  }
  std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset) const override {
    bool err = false;
    auto ret = std::make_unique<WriteStream>(m_wpath, offset, m_maxWriteSize, err);

    if (err) {
      return nullptr;
    }

    return ret;
  }

  struct ReadStream : public IFileIO::IReadStream {
    HANDLE fp;
    ReadStream(std::wstring_view wpath, bool& err) {
#if !WINDOWS_STORE
      fp = CreateFileW(wpath.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                       nullptr);
#else
      fp = CreateFile2(wpath.data(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
#endif
      if (fp == INVALID_HANDLE_VALUE) {
        err = true;
        const nowide::stackstring path(wpath.data(), wpath.data() + wpath.size());
        LogModule.report(logvisor::Error, FMT_STRING("unable to open '{}' for reading"), path.get());
      }
    }
    ReadStream(std::wstring_view wpath, uint64_t offset, bool& err) : ReadStream(wpath, err) {
      if (err)
        return;
      LARGE_INTEGER lioffset;
      lioffset.QuadPart = offset;
      SetFilePointerEx(fp, lioffset, nullptr, FILE_BEGIN);
    }
    ~ReadStream() override { CloseHandle(fp); }
    void seek(int64_t offset, int whence) override {
      LARGE_INTEGER li;
      li.QuadPart = offset;
      SetFilePointerEx(fp, li, nullptr, whence);
    }
    uint64_t position() const override {
      LARGE_INTEGER li = {};
      LARGE_INTEGER res;
      SetFilePointerEx(fp, li, &res, FILE_CURRENT);
      return res.QuadPart;
    }
    uint64_t read(void* buf, uint64_t length) override {
      DWORD ret = 0;
      ReadFile(fp, buf, length, &ret, nullptr);
      return ret;
    }
    uint64_t copyToDisc(IPartWriteStream& discio, uint64_t length) override {
      uint64_t written = 0;
      uint8_t buf[0x7c00];
      while (length) {
        uint64_t thisSz = nod::min(uint64_t(0x7c00), length);
        if (read(buf, thisSz) != thisSz) {
          LogModule.report(logvisor::Error, FMT_STRING("unable to read enough from file"));
          return written;
        }
        if (discio.write(buf, thisSz) != thisSz) {
          LogModule.report(logvisor::Error, FMT_STRING("unable to write enough to disc"));
          return written;
        }
        length -= thisSz;
        written += thisSz;
      }
      return written;
    }
  };

  std::unique_ptr<IReadStream> beginReadStream() const override {
    bool err = false;
    auto ret = std::make_unique<ReadStream>(m_wpath, err);

    if (err) {
      return nullptr;
    }

    return ret;
  }

  std::unique_ptr<IReadStream> beginReadStream(uint64_t offset) const override {
    bool err = false;
    auto ret = std::make_unique<ReadStream>(m_wpath, offset, err);

    if (err) {
      return nullptr;
    }

    return ret;
  }
};

std::unique_ptr<IFileIO> NewFileIO(std::string_view path, int64_t maxWriteSize) {
  return std::make_unique<FileIOWin32>(path, maxWriteSize);
}

} // namespace nod
