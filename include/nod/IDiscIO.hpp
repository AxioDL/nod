#pragma once

#include <memory>
#include <cstdlib>
#include <cstdio>
#include <cstdint>

#if NOD_ATHENA
#include <athena/IStreamReader.hpp>
#include <athena/IStreamWriter.hpp>
#endif

namespace nod {

struct IReadStream {
  virtual ~IReadStream() = default;
  virtual uint64_t read(void* buf, uint64_t length) = 0;
  virtual void seek(int64_t offset, int whence = SEEK_SET) = 0;
  virtual uint64_t position() const = 0;
};

struct IWriteStream {
  virtual ~IWriteStream() = default;
  virtual uint64_t write(const void* buf, uint64_t length) = 0;
};

class IDiscIO {
public:
  virtual ~IDiscIO() = default;
  virtual std::unique_ptr<IReadStream> beginReadStream(uint64_t offset = 0) const = 0;
  virtual std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset = 0) const = 0;
};

struct IPartReadStream : IReadStream {
  virtual ~IPartReadStream() = default;
};

struct IPartWriteStream : IWriteStream {
  virtual ~IPartWriteStream() = default;
  virtual void close() = 0;
  virtual uint64_t position() const = 0;
};

#if NOD_ATHENA

class AthenaPartReadStream : public athena::io::IStreamReader {
  std::unique_ptr<IPartReadStream> m_rs;

public:
  AthenaPartReadStream(std::unique_ptr<IPartReadStream>&& rs) : m_rs(std::move(rs)) {}

  inline void seek(atInt64 off, athena::SeekOrigin origin) {
    if (origin == athena::Begin)
      m_rs->seek(off, SEEK_SET);
    else if (origin == athena::Current)
      m_rs->seek(off, SEEK_CUR);
  }
  inline atUint64 position() const { return m_rs->position(); }
  inline atUint64 length() const { return 0; }
  inline atUint64 readUBytesToBuf(void* buf, atUint64 sz) {
    m_rs->read(buf, sz);
    return sz;
  }
};

#endif

} // namespace nod
