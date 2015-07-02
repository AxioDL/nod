#include <stdio.h>
#include <string.h>
#include "NOD/NOD.hpp"

static void printHelp()
{
    fprintf(stderr, "Usage:\n"
                    "  nodlib extract [-f] <image-in> [<dir-out>]\n"
                    "  nodlib make <dir-in> [<image-out>]\n");
}

#if NOD_UCS2
#ifdef strcasecmp
#undef strcasecmp
#endif
#define strcasecmp _wcsicmp
int wmain(int argc, wchar_t* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    if (argc < 3)
    {
        printHelp();
        return -1;
    }

    const NOD::SystemChar* inDir = nullptr;
    const NOD::SystemChar* outDir = _S(".");
    bool force = false;
    for (int a=2 ; a<argc ; ++a)
    {
        if (argv[a][0] == '-' && argv[a][1] == 'f')
            force = true;
        else if (!inDir)
            inDir = argv[a];
        else
            outDir = argv[a];
    }

    if (!strcasecmp(argv[1], _S("extract")))
    {
        std::unique_ptr<NOD::DiscBase> disc = NOD::OpenDiscFromImage(inDir);
        if (!disc)
            return -1;

        NOD::DiscBase::IPartition* dataPart = disc->getDataPartition();
        if (!dataPart)
            return -1;

        dataPart->extractToDirectory(outDir, force);
    }
    else if (!strcasecmp(argv[1], _S("make")))
    {
    }
    else
    {
        printHelp();
        return -1;
    }

    return 0;
}

