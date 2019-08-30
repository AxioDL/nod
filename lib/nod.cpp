#include "nod/nod.hpp"

#include <cstdio>

#include "nod/DiscBase.hpp"
#include "nod/DiscGCN.hpp"
#include "nod/DiscWii.hpp"

namespace nod {

logvisor::Module LogModule("nod");

std::unique_ptr<IDiscIO> NewDiscIOISO(SystemStringView path);
std::unique_ptr<IDiscIO> NewDiscIOWBFS(SystemStringView path);

std::unique_ptr<DiscBase> OpenDiscFromImage(SystemStringView path, bool& isWii) {
  /* Temporary file handle to determine image type */
  std::unique_ptr<IFileIO> fio = NewFileIO(path);
  if (!fio->exists()) {
    LogModule.report(logvisor::Error, fmt(_SYS_STR("Unable to open '{}'")), path);
    return {};
  }
  std::unique_ptr<IFileIO::IReadStream> rs = fio->beginReadStream();
  if (!rs)
    return {};

  isWii = false;
  std::unique_ptr<IDiscIO> discIO;
  uint32_t magic = 0;
  if (rs->read(&magic, 4) != 4) {
    LogModule.report(logvisor::Error, fmt(_SYS_STR("Unable to read magic from '{}'")), path);
    return {};
  }

  if (magic == nod::SBig((uint32_t)'WBFS')) {
    discIO = NewDiscIOWBFS(path);
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
    LogModule.report(logvisor::Error, fmt(_SYS_STR("'{}' is not a valid image")), path);
    return {};
  }

  bool err = false;
  std::unique_ptr<DiscBase> ret;
  if (isWii) {
    ret = std::make_unique<DiscWii>(std::move(discIO), err);
    if (err) {
      return nullptr;
    }
    return ret;
  }

  ret = std::make_unique<DiscGCN>(std::move(discIO), err);
  if (err) {
    return nullptr;
  }
  return ret;
}

std::unique_ptr<DiscBase> OpenDiscFromImage(SystemStringView path) {
  bool isWii;
  return OpenDiscFromImage(path, isWii);
}

} // namespace nod
