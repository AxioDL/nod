#include <stdio.h>
#include "NODLib.hpp"
#include "DiscBase.hpp"

namespace NOD
{
std::unique_ptr<IDiscIO> NewDiscIOISO(const char* path);
std::unique_ptr<IDiscIO> NewDiscIOWBFS(const char* path);

std::unique_ptr<DiscBase> OpenDiscFromImage(const char* path, bool& isWii)
{
    /* Temporary file handle to determine image type */
    FILE* fp = fopen(path, "rb");
    if (!fp)
    {
        throw std::runtime_error("Unable to open '" + std::string(path) + "'");
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
        fseek(fp, 0x18, SEEK_SET);
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
        }
    }

    if (!discIO)
    {
        throw std::runtime_error("'" + std::string(path) + "' is not a valid image");
        return std::unique_ptr<DiscBase>();
    }

    if (isWii)
        return std::unique_ptr<DiscBase>(new DiscWii(std::move(discIO)));

    return std::unique_ptr<DiscBase>(new DiscGCN(std::move(discIO)));

}

}

