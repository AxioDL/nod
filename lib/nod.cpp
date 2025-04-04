#include "nod/nod.hpp"

#include <cstdio>

#include "nod/DiscBase.hpp"
#include "nod/DiscGCN.hpp"
#include "nod/DiscWii.hpp"

#include <spdlog/spdlog.h>

namespace nod {
std::unique_ptr<IDiscIO> NewDiscIOISO(std::string_view path);
std::unique_ptr<IDiscIO> NewDiscIOWBFS(std::string_view path);
std::unique_ptr<IDiscIO> NewDiscIONFS(std::string_view path);

std::unique_ptr<DiscBase> OpenDiscFromImage(std::string_view path, bool& isWii) {
  /* Temporary file handle to determine image type */
  std::unique_ptr<IFileIO> fio = NewFileIO(path);
  if (!fio->exists()) {
    spdlog::error("Unable to open '{}'", path);
    return {};
  }
  std::unique_ptr<IFileIO::IReadStream> rs = fio->beginReadStream();
  if (!rs)
    return {};

  isWii = false;
  std::unique_ptr<IDiscIO> discIO;
  uint32_t magic = 0;
  if (rs->read(&magic, 4) != 4) {
    spdlog::error("Unable to read magic from '{}'", path);
    return {};
  }

  using SignedSize = std::make_signed<std::string::size_type>::type;
  const auto dotPos = SignedSize(path.rfind('.'));
  const auto slashPos = SignedSize(path.find_last_of("/\\"));
  if (magic == nod::SBig((uint32_t)'WBFS')) {
    discIO = NewDiscIOWBFS(path);
    isWii = true;
  } else if (path.size() > 4 && dotPos != -1 && dotPos > slashPos &&
             !path.compare(slashPos + 1, 4, "hif_") &&
             !path.compare(dotPos, path.size() - dotPos, ".nfs")) {
    discIO = NewDiscIONFS(path);
    isWii = true;
  } else {
    rs->seek(0x18, SEEK_SET);
    rs->read(&magic, 4);
    magic = nod::SBig(magic);
    if (magic == 0x5D1C9EA3) {
      discIO = NewDiscIOISO(path);
      isWii = true;
    } else {
      rs->read(&magic, 4);
      magic = nod::SBig(magic);
      if (magic == 0xC2339F3D)
        discIO = NewDiscIOISO(path);
    }
  }

  if (!discIO) {
    spdlog::error("'{}' is not a valid image", path);
    return {};
  }

  bool err = false;
  std::unique_ptr<DiscBase> ret;
  if (isWii) {
    ret = std::make_unique<DiscWii>(std::move(discIO), err);
    if (err)
      return {};
    return ret;
  }

  ret = std::make_unique<DiscGCN>(std::move(discIO), err);
  if (err)
    return {};
  return ret;
}

std::unique_ptr<DiscBase> OpenDiscFromImage(std::string_view path) {
  bool isWii;
  return OpenDiscFromImage(path, isWii);
}

} // namespace nod
