#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "nod/IDiscIO.hpp"
#include "nod/Util.hpp"

#include <logvisor/logvisor.hpp>

namespace nod {

class IFileIO {
public:
  virtual ~IFileIO() = default;
  virtual bool exists() = 0;
  virtual uint64_t size() = 0;

  struct IWriteStream : nod::IWriteStream {
    uint64_t copyFromDisc(IPartReadStream& discio, uint64_t length) {
      uint64_t read = 0;
      uint8_t buf[0x7c00];
      while (length) {
        uint64_t thisSz = nod::min(uint64_t(0x7c00), length);
        uint64_t readSz = discio.read(buf, thisSz);
        if (thisSz != readSz) {
          LogModule.report(logvisor::Error, FMT_STRING("unable to read enough from disc"));
          return read;
        }
        if (write(buf, readSz) != readSz) {
          LogModule.report(logvisor::Error, FMT_STRING("unable to write in file"));
          return read;
        }
        length -= thisSz;
        read += thisSz;
      }
      return read;
    }
    uint64_t copyFromDisc(IPartReadStream& discio, uint64_t length, const std::function<void(float)>& prog) {
      uint64_t read = 0;
      uint8_t buf[0x7c00];
      uint64_t total = length;
      while (length) {
        uint64_t thisSz = nod::min(uint64_t(0x7c00), length);
        uint64_t readSz = discio.read(buf, thisSz);
        if (thisSz != readSz) {
          LogModule.report(logvisor::Error, FMT_STRING("unable to read enough from disc"));
          return read;
        }
        if (write(buf, readSz) != readSz) {
          LogModule.report(logvisor::Error, FMT_STRING("unable to write in file"));
          return read;
        }
        length -= thisSz;
        read += thisSz;
        prog(read / float(total));
      }
      return read;
    }
  };
  virtual std::unique_ptr<IWriteStream> beginWriteStream() const = 0;
  virtual std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset) const = 0;

  struct IReadStream : nod::IReadStream {
    virtual uint64_t copyToDisc(struct IPartWriteStream& discio, uint64_t length) = 0;
  };
  virtual std::unique_ptr<IReadStream> beginReadStream() const = 0;
  virtual std::unique_ptr<IReadStream> beginReadStream(uint64_t offset) const = 0;
};

std::unique_ptr<IFileIO> NewFileIO(SystemStringView path, int64_t maxWriteSize = -1);

} // namespace nod
