#include <cinttypes>
#include <cstdio>
#include <cstdint>
#include <cstdlib>

#include "nod/IFileIO.hpp"
#include "Util.hpp"

#include <spdlog/spdlog.h>

namespace nod {

class FileIOFILE : public IFileIO {
  std::string m_path;
  int64_t m_maxWriteSize;

public:
  FileIOFILE(std::string_view path, int64_t maxWriteSize) : m_path(path), m_maxWriteSize(maxWriteSize) {}

  bool exists() override {
    FILE* fp = Fopen(m_path.c_str(), "rb");
    if (!fp)
      return false;
    fclose(fp);
    return true;
  }

  uint64_t size() override {
    FILE* fp = Fopen(m_path.c_str(), "rb");
    if (!fp)
      return 0;
    FSeek(fp, 0, SEEK_END);
    uint64_t result = FTell(fp);
    fclose(fp);
    return result;
  }

  struct WriteStream : public IFileIO::IWriteStream {
    FILE* fp;
    int64_t m_maxWriteSize;
    WriteStream(std::string_view path, int64_t maxWriteSize, bool& err) : m_maxWriteSize(maxWriteSize) {
      fp = Fopen(path.data(), "wb");
      if (!fp) {
        spdlog::error("unable to open '{}' for writing", path);
        err = true;
      }
    }
    WriteStream(std::string_view path, uint64_t offset, int64_t maxWriteSize, bool& err)
    : m_maxWriteSize(maxWriteSize) {
      fp = Fopen(path.data(), "ab");
      if (!fp)
        goto FailLoc;
      fclose(fp);
      fp = Fopen(path.data(), "r+b");
      if (!fp)
        goto FailLoc;
      FSeek(fp, offset, SEEK_SET);
      return;
    FailLoc:
      spdlog::error("unable to open '{}' for writing", path);
      err = true;
    }
    ~WriteStream() override { fclose(fp); }
    uint64_t write(const void* buf, uint64_t length) override {
      if (m_maxWriteSize >= 0) {
        if (FTell(fp) + length > m_maxWriteSize) {
          spdlog::error("write operation exceeds file's {}-byte limit", m_maxWriteSize);
          return 0;
        }
      }
      return fwrite(buf, 1, length, fp);
    }
  };

  std::unique_ptr<IWriteStream> beginWriteStream() const override {
    bool err = false;
    auto ret = std::unique_ptr<IWriteStream>(new WriteStream(m_path, m_maxWriteSize, err));

    if (err)
      return {};

    return ret;
  }

  std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset) const override {
    bool err = false;
    auto ret = std::unique_ptr<IWriteStream>(new WriteStream(m_path, offset, m_maxWriteSize, err));

    if (err)
      return {};

    return ret;
  }

  struct ReadStream : public IFileIO::IReadStream {
    FILE* fp;
    ReadStream(std::string_view path, bool& err) {
      fp = Fopen(path.data(), "rb");
      if (!fp) {
        err = true;
        spdlog::error("unable to open '{}' for reading", path);
      }
    }
    ReadStream(std::string_view path, uint64_t offset, bool& err) : ReadStream(path, err) {
      if (err)
        return;
      FSeek(fp, offset, SEEK_SET);
    }
    ~ReadStream() override { fclose(fp); }
    void seek(int64_t offset, int whence) override { FSeek(fp, offset, whence); }
    uint64_t position() const override { return FTell(fp); }
    uint64_t read(void* buf, uint64_t length) override { return fread(buf, 1, length, fp); }
    uint64_t copyToDisc(IPartWriteStream& discio, uint64_t length) override {
      uint64_t written = 0;
      uint8_t buf[0x7c00];
      while (length) {
        uint64_t thisSz = nod::min(uint64_t(0x7c00), length);
        if (read(buf, thisSz) != thisSz) {
          spdlog::error("unable to read enough from file");
          return written;
        }
        if (discio.write(buf, thisSz) != thisSz) {
          spdlog::error("unable to write enough to disc");
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
    auto ret = std::unique_ptr<IReadStream>(new ReadStream(m_path, err));

    if (err)
      return {};

    return ret;
  }

  std::unique_ptr<IReadStream> beginReadStream(uint64_t offset) const override {
    bool err = false;
    auto ret = std::unique_ptr<IReadStream>(new ReadStream(m_path, offset, err));

    if (err)
      return {};

    return ret;
  }
};

std::unique_ptr<IFileIO> NewFileIO(std::string_view path, int64_t maxWriteSize) {
  return std::make_unique<FileIOFILE>(path, maxWriteSize);
}

} // namespace nod
