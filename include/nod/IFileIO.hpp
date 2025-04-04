#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

#include "nod/IDiscIO.hpp"

namespace nod {

class IFileIO {
public:
  virtual ~IFileIO() = default;
  virtual bool exists() = 0;
  virtual uint64_t size() = 0;

  struct IWriteStream : nod::IWriteStream {
    uint64_t copyFromDisc(IPartReadStream& discio, uint64_t length);
    uint64_t copyFromDisc(IPartReadStream& discio, uint64_t length, const std::function<void(float)>& prog);
  };
  virtual std::unique_ptr<IWriteStream> beginWriteStream() const = 0;
  virtual std::unique_ptr<IWriteStream> beginWriteStream(uint64_t offset) const = 0;

  struct IReadStream : nod::IReadStream {
    virtual uint64_t copyToDisc(struct IPartWriteStream& discio, uint64_t length) = 0;
  };
  virtual std::unique_ptr<IReadStream> beginReadStream() const = 0;
  virtual std::unique_ptr<IReadStream> beginReadStream(uint64_t offset) const = 0;
};

std::unique_ptr<IFileIO> NewFileIO(std::string_view path, int64_t maxWriteSize = -1);

} // namespace nod
