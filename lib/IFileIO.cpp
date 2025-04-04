#include "nod/IFileIO.hpp"
#include "Util.hpp"
#include <spdlog/spdlog.h>

namespace nod {
uint64_t IFileIO::IWriteStream::copyFromDisc(IPartReadStream& discio, uint64_t length) {
  uint64_t read = 0;
  uint8_t buf[0x7c00];
  while (length) {
    uint64_t thisSz = nod::min(uint64_t(0x7c00), length);
    uint64_t readSz = discio.read(buf, thisSz);
    if (thisSz != readSz) {
      spdlog::error("unable to read enough from disc");
      return read;
    }
    if (write(buf, readSz) != readSz) {
      spdlog::error("unable to write in file");
      return read;
    }
    length -= thisSz;
    read += thisSz;
  }
  return read;
}

uint64_t IFileIO::IWriteStream::copyFromDisc(IPartReadStream& discio, uint64_t length,
                                             const std::function<void(float)>& prog) {
  uint64_t read = 0;
  uint8_t buf[0x7c00];
  uint64_t total = length;
  while (length) {
    uint64_t thisSz = nod::min(uint64_t(0x7c00), length);
    uint64_t readSz = discio.read(buf, thisSz);
    if (thisSz != readSz) {
      spdlog::error("unable to read enough from disc");
      return read;
    }
    if (write(buf, readSz) != readSz) {
      spdlog::error("unable to write in file");
      return read;
    }
    length -= thisSz;
    read += thisSz;
    prog(read / float(total));
  }
  return read;
}
} // namespace nod
