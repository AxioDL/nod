#include <stdio.h>
#include "NOD/NOD.hpp"
#include "NOD/DiscBase.hpp"

namespace NOD
{

LogVisor::LogModule LogModule("NODLib");

std::unique_ptr<IDiscIO> NewDiscIOISO(const SystemChar* path);
std::unique_ptr<IDiscIO> NewDiscIOWBFS(const SystemChar* path);

std::unique_ptr<DiscBase> OpenDiscFromImage(const SystemChar* path, bool& isWii)
{
    /* Temporary file handle to determine image type */
    std::unique_ptr<IFileIO> fio = NewFileIO(path);
    if (!fio->exists())
    {
        LogModule.report(LogVisor::Error, _S("Unable to open '%s'"), path);
        return std::unique_ptr<DiscBase>();
    }
    std::unique_ptr<IFileIO::IReadStream> rs = fio->beginReadStream();

    isWii = false;
    std::unique_ptr<IDiscIO> discIO;
    uint32_t magic = 0;
    if (rs->read(&magic, 4) != 4)
        LogModule.report(LogVisor::FatalError, _S("Unable to read magic from '%s'"), path);

    if (magic == NOD::SBig((uint32_t)'WBFS'))
    {
        discIO = NewDiscIOWBFS(path);
        isWii = true;
    }
    else
    {
        rs->seek(0x18, SEEK_SET);
        rs->read(&magic, 4);
        magic = NOD::SBig(magic);
        if (magic == 0x5D1C9EA3)
        {
            discIO = NewDiscIOISO(path);
            isWii = true;
        }
        else
        {
            rs->read(&magic, 4);
            magic = NOD::SBig(magic);
            if (magic == 0xC2339F3D)
                discIO = NewDiscIOISO(path);
        }
    }

    if (!discIO)
    {
        LogModule.report(LogVisor::Error, _S("'%s' is not a valid image"), path);
        return std::unique_ptr<DiscBase>();
    }

    if (isWii)
        return std::unique_ptr<DiscBase>(new DiscWii(std::move(discIO)));

    return std::unique_ptr<DiscBase>(new DiscGCN(std::move(discIO)));

}

std::unique_ptr<DiscBase> OpenDiscFromImage(const SystemChar* path)
{
    bool isWii;
    return OpenDiscFromImage(path, isWii);
}

}

