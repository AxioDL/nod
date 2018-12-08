#include <cstdio>
#include <cstdlib>
#include <cinttypes>
#include "nod/Util.hpp"
#include "nod/IFileIO.hpp"

namespace nod {

class FileIOWin32 : public IFileIO {
  SystemString m_path;
  int64_t m_maxWriteSize;

public:
  FileIOWin32(SystemStringView path, int64_t maxWriteSize) : m_path(path), m_maxWriteSize(maxWriteSize) {}

  bool exists() {
#if !WINDOWS_STORE
    HANDLE fp = CreateFileW(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
#else
    HANDLE fp = CreateFile2(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
#endif
    if (fp == INVALID_HANDLE_VALUE)
      return false;
    CloseHandle(fp);
    return true;
  }

  uint64_t size() {
#if !WINDOWS_STORE
    HANDLE fp = CreateFileW(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
#else
    HANDLE fp = CreateFile2(m_path.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
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
    WriteStream(SystemStringView path, int64_t maxWriteSize, bool& err) : m_maxWriteSize(maxWriteSize) {
#if !WINDOWS_STORE
      fp = CreateFileW(path.data(), GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                       nullptr);
#else
      fp = CreateFile2(path.data(), GENERIC_WRITE, FILE_SHARE_WRITE, CREATE_ALWAYS, nullptr);
#endif
      if (fp == INVALID_HANDLE_VALUE) {
        LogModule.report(logvisor::Error, _SYS_STR("unable to open '%s' for writing"), path.data());
        err = true;
      }
    }
    WriteStream(SystemStringView path, uint64_t offset, int64_t maxWriteSize, bool& err)
    : m_maxWriteSize(maxWriteSize) {
#if !WINDOWS_STORE
      fp = CreateFileW(path.data(), GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                       nullptr);
#else
      fp = CreateFile2(path.data(), GENERIC_WRITE, FILE_SHARE_WRITE, OPEN_ALWAYS, nullptr);
#endif
      if (fp == INVALID_HANDLE_VALUE) {
        LogModule.report(logvisor::Error, _SYS_STR("unable to open '%s' for writing"), path.data());
        err = true;
        return;
      }
      LARGE_INTEGER lioffset;
      lioffset.QuadPart = offset;
      SetFilePointerEx(fp, lioffset, nullptr, FILE_BEGIN);
    }
    ~WriteStream() { CloseHandle(fp); }
    uint64_t write(const void* buf, uint64_t length) {
      if (m_maxWriteSize >= 0) {
        LARGE_INTEGER li = {};
        LARGE_INTEGER res;
        SetFilePointerEx(fp, li, &res, FILE_CURRENT);
        if (res.QuadPart + int64_t(length) > m_maxWriteSize) {
          LogModule.report(logvisor::Error, _SYS_STR("write operation exceeds file's %" PRIi64 "-byte limit"),
                           m_maxWriteSize);
          return 0;
        }
      }

      DWORD ret = 0;
      WriteFile(fp, buf, length, &ret, nullptr);
      return ret;
    }
  };
  std::unique_ptr<IWriteStream> beginWriteStream() const {
    bool Err = false;
    auto ret = std::unique_ptr<IWriteStream>(new WriteStream(m_path, m_maxWriteSize, Err));
    if (Err)
      return {};
    return ret;
  }
  std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset) const {
    bool Err = false;
    auto ret = std::unique_ptr<IWriteStream>(new WriteStream(m_path, offset, m_maxWriteSize, Err));
    if (Err)
      return {};
    return ret;
  }

  struct ReadStream : public IFileIO::IReadStream {
    HANDLE fp;
    ReadStream(SystemStringView path, bool& err) {
#if !WINDOWS_STORE
      fp = CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                       nullptr);
#else
      fp = CreateFile2(path.data(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
#endif
      if (fp == INVALID_HANDLE_VALUE) {
        err = true;
        LogModule.report(logvisor::Error, _SYS_STR("unable to open '%s' for reading"), path.data());
      }
    }
    ReadStream(SystemStringView path, uint64_t offset, bool& err) : ReadStream(path, err) {
      if (err)
        return;
      LARGE_INTEGER lioffset;
      lioffset.QuadPart = offset;
      SetFilePointerEx(fp, lioffset, nullptr, FILE_BEGIN);
    }
    ~ReadStream() { CloseHandle(fp); }
    void seek(int64_t offset, int whence) {
      LARGE_INTEGER li;
      li.QuadPart = offset;
      SetFilePointerEx(fp, li, nullptr, whence);
    }
    uint64_t position() const {
      LARGE_INTEGER li = {};
      LARGE_INTEGER res;
      SetFilePointerEx(fp, li, &res, FILE_CURRENT);
      return res.QuadPart;
    }
    uint64_t read(void* buf, uint64_t length) {
      DWORD ret = 0;
      ReadFile(fp, buf, length, &ret, nullptr);
      return ret;
    }
    uint64_t copyToDisc(IPartWriteStream& discio, uint64_t length) {
      uint64_t written = 0;
      uint8_t buf[0x7c00];
      while (length) {
        uint64_t thisSz = nod::min(uint64_t(0x7c00), length);
        if (read(buf, thisSz) != thisSz) {
          LogModule.report(logvisor::Error, "unable to read enough from file");
          return written;
        }
        if (discio.write(buf, thisSz) != thisSz) {
          LogModule.report(logvisor::Error, "unable to write enough to disc");
          return written;
        }
        length -= thisSz;
        written += thisSz;
      }
      return written;
    }
  };
  std::unique_ptr<IReadStream> beginReadStream() const {
    bool Err = false;
    auto ret = std::unique_ptr<IReadStream>(new ReadStream(m_path, Err));
    if (Err)
      return {};
    return ret;
  }
  std::unique_ptr<IReadStream> beginReadStream(uint64_t offset) const {
    bool Err = false;
    auto ret = std::unique_ptr<IReadStream>(new ReadStream(m_path, offset, Err));
    if (Err)
      return {};
    return ret;
  }
};

std::unique_ptr<IFileIO> NewFileIO(SystemStringView path, int64_t maxWriteSize) {
  return std::unique_ptr<IFileIO>(new FileIOWin32(path, maxWriteSize));
}

} // namespace nod
