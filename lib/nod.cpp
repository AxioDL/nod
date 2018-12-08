#include <cstdio>
#include "nod/nod.hpp"
#include "nod/DiscBase.hpp"

namespace nod {

logvisor::Module LogModule("nod");

std::unique_ptr<IDiscIO> NewDiscIOISO(SystemStringView path);
std::unique_ptr<IDiscIO> NewDiscIOWBFS(SystemStringView path);

std::unique_ptr<DiscBase> OpenDiscFromImage(SystemStringView path, bool& isWii) {
  /* Temporary file handle to determine image type */
  std::unique_ptr<IFileIO> fio = NewFileIO(path);
  if (!fio->exists()) {
    LogModule.report(logvisor::Error, _SYS_STR("Unable to open '%s'"), path.data());
    return {};
  }
  std::unique_ptr<IFileIO::IReadStream> rs = fio->beginReadStream();
  if (!rs)
    return {};

  isWii = false;
  std::unique_ptr<IDiscIO> discIO;
  uint32_t magic = 0;
  if (rs->read(&magic, 4) != 4) {
    LogModule.report(logvisor::Error, _SYS_STR("Unable to read magic from '%s'"), path.data());
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
    LogModule.report(logvisor::Error, _SYS_STR("'%s' is not a valid image"), path.data());
    return {};
  }

  bool Err = false;
  std::unique_ptr<DiscBase> ret;
  if (isWii) {
    ret = std::unique_ptr<DiscBase>(new DiscWii(std::move(discIO), Err));
    if (Err)
      return {};
    return ret;
  }

  ret = std::unique_ptr<DiscBase>(new DiscGCN(std::move(discIO), Err));
  if (Err)
    return {};
  return ret;
}

std::unique_ptr<DiscBase> OpenDiscFromImage(SystemStringView path) {
  bool isWii;
  return OpenDiscFromImage(path, isWii);
}

} // namespace nod
