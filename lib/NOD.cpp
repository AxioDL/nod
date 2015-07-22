#include <stdio.h>
#include "NOD/NOD.hpp"
#include "NOD/DiscBase.hpp"

#if _WIN32
#define fseeko _fseeki64
#endif

namespace NOD
{

LogVisor::LogModule LogModule("NODLib");

std::unique_ptr<IDiscIO> NewDiscIOISO(const SystemChar* path);
std::unique_ptr<IDiscIO> NewDiscIOWBFS(const SystemChar* path);

std::unique_ptr<DiscBase> OpenDiscFromImage(const SystemChar* path, bool& isWii)
{
    /* Temporary file handle to determine image type */
#if NOD_UCS2
    FILE* fp = _wfopen(path, L"rb");
#else
    FILE* fp = fopen(path, "rb");
#endif
    if (!fp)
    {
        LogModule.report(LogVisor::Error, _S("Unable to open '%s'"), path);
        return std::unique_ptr<DiscBase>();
    }

    isWii = false;
    std::unique_ptr<IDiscIO> discIO;
    uint32_t magic;
    fread(&magic, 1, 4, fp);
    if (magic == NOD::SBig((uint32_t)'WBFS'))
    {
        fclose(fp);
        discIO = NewDiscIOWBFS(path);
        isWii = true;
    }
    else
    {
        fseeko(fp, 0x18, SEEK_SET);
        fread(&magic, 1, 4, fp);
        magic = NOD::SBig(magic);
        if (magic == 0x5D1C9EA3)
        {
            fclose(fp);
            discIO = NewDiscIOISO(path);
            isWii = true;
        }
        else
        {
            fread(&magic, 1, 4, fp);
            magic = NOD::SBig(magic);
            if (magic == 0xC2339F3D)
            {
                fclose(fp);
                discIO = NewDiscIOISO(path);
            }
            else
                fclose(fp);
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

