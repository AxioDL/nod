#include <stdio.h>
#include <string.h>
#include <LogVisor/LogVisor.hpp>
#include "NOD/NOD.hpp"

static void printHelp()
{
    fprintf(stderr, "Usage:\n"
                    "  nodtool extract [-f] <image-in> [<dir-out>]\n"
                    "  nodtool makegcn <gameid> <game-title> <fsroot-in> <dol-in> <apploader-in> [<image-out>]\n"
                    "  nodtool makewii(sl|dl) <gameid> <game-title> <fsroot-in> <dol-in> <apploader-in> <parthead-in> [<image-out>]\n");
}

#if NOD_UCS2
#ifdef strcasecmp
#undef strcasecmp
#endif
#define strcasecmp _wcsicmp
#define PRISize "Iu"
int wmain(int argc, wchar_t* argv[])
#else
#define PRISize "zu"
int main(int argc, char* argv[])
#endif
{
    if (argc < 3 ||
        (!strcasecmp(argv[1], _S("makegcn")) && argc < 7) ||
        (!strcasecmp(argv[1], _S("makewiisl")) && argc < 8) ||
        (!strcasecmp(argv[1], _S("makewiidl")) && argc < 8))
    {
        printHelp();
        return -1;
    }

    /* Enable logging to console */
    LogVisor::RegisterConsoleLogger();

    NOD::ExtractionContext ctx = { true, true, [&](const std::string& str){
                                       fprintf(stderr, "%s\n", str.c_str());
                                   }};
    const NOD::SystemChar* inDir = nullptr;
    const NOD::SystemChar* outDir = _S(".");

    for (int a=2 ; a<argc ; ++a)
    {
        if (argv[a][0] == '-' && argv[a][1] == 'f')
            ctx.force = true;
        else if (argv[a][0] == '-' && argv[a][1] == 'v')
            ctx.verbose = true;

        else if (!inDir)
            inDir = argv[a];
        else
            outDir = argv[a];
    }

    if (!strcasecmp(argv[1], _S("extract")))
    {
        bool isWii;
        std::unique_ptr<NOD::DiscBase> disc = NOD::OpenDiscFromImage(inDir, isWii);
        if (!disc)
            return -1;

        NOD::Mkdir(outDir, 0755);

        if (isWii)
            static_cast<NOD::DiscWii&>(*disc).writeOutDataPartitionHeader(
                    (NOD::SystemString(outDir) + _S("/partition_head.bin")).c_str());

        NOD::Partition* dataPart = disc->getDataPartition();
        if (!dataPart)
            return -1;

        if (!dataPart->extractToDirectory(outDir, ctx))
            return -1;
    }
    else if (!strcasecmp(argv[1], _S("makegcn")))
    {
#if NOD_UCS2
        if (wcslen(argv[2]) < 6)
            NOD::LogModule.report(LogVisor::FatalError, _S("game-id is not at least 6 characters"));
#else
        if (strlen(argv[2]) < 6)
            NOD::LogModule.report(LogVisor::FatalError, _S("game-id is not at least 6 characters"));
#endif

        /* Pre-validate paths */
        NOD::Sstat theStat;
        if (NOD::Stat(argv[4], &theStat) || !S_ISDIR(theStat.st_mode))
            NOD::LogModule.report(LogVisor::FatalError, _S("unable to stat %s as directory"), argv[4]);
        if (NOD::Stat(argv[5], &theStat) || !S_ISREG(theStat.st_mode))
            NOD::LogModule.report(LogVisor::FatalError, _S("unable to stat %s as file"), argv[5]);
        if (NOD::Stat(argv[6], &theStat) || !S_ISREG(theStat.st_mode))
            NOD::LogModule.report(LogVisor::FatalError, "unable to stat %s as file", argv[6]);

        NOD::SystemUTF8View gameId(argv[2]);
        NOD::SystemUTF8View gameTitle(argv[3]);
        size_t lastIdx = -1;
        auto progFunc = [&](size_t idx, const NOD::SystemString& name, size_t bytes)
        {
            if (idx != lastIdx)
            {
                lastIdx = idx;
                printf("\n");
            }
            if (bytes != -1)
                NOD::Printf(_S("\r%s %" PRISize " B"), name.c_str(), bytes);
            else
                NOD::Printf(_S("\r%s"), name.c_str());
            fflush(stdout);
        };

        if (argc < 8)
        {
            NOD::SystemString outPath(argv[4]);
            outPath.append(_S(".iso"));
            NOD::DiscBuilderGCN b(outPath.c_str(), gameId.utf8_str().c_str(), gameTitle.utf8_str().c_str(), 0x0003EB60, progFunc);
            b.buildFromDirectory(argv[4], argv[5], argv[6]);
        }
        else
        {
            NOD::DiscBuilderGCN b(argv[7], gameId.utf8_str().c_str(), gameTitle.utf8_str().c_str(), 0x0003EB60, progFunc);
            b.buildFromDirectory(argv[4], argv[5], argv[6]);
        }

        printf("\n");
    }
    else if (!strcasecmp(argv[1], _S("makewiisl")) || !strcasecmp(argv[1], _S("makewiidl")))
    {
#if NOD_UCS2
        if (wcslen(argv[2]) < 6)
            NOD::LogModule.report(LogVisor::FatalError, _S("game-id is not at least 6 characters"));
#else
        if (strlen(argv[2]) < 6)
            NOD::LogModule.report(LogVisor::FatalError, _S("game-id is not at least 6 characters"));
#endif

        /* Pre-validate paths */
        NOD::Sstat theStat;
        if (NOD::Stat(argv[4], &theStat) || !S_ISDIR(theStat.st_mode))
            NOD::LogModule.report(LogVisor::FatalError, _S("unable to stat %s as directory"), argv[4]);
        if (NOD::Stat(argv[5], &theStat) || !S_ISREG(theStat.st_mode))
            NOD::LogModule.report(LogVisor::FatalError, _S("unable to stat %s as file"), argv[5]);
        if (NOD::Stat(argv[6], &theStat) || !S_ISREG(theStat.st_mode))
            NOD::LogModule.report(LogVisor::FatalError, _S("unable to stat %s as file"), argv[6]);
        if (NOD::Stat(argv[7], &theStat) || !S_ISREG(theStat.st_mode))
            NOD::LogModule.report(LogVisor::FatalError, _S("unable to stat %s as file"), argv[7]);

        NOD::SystemUTF8View gameId(argv[2]);
        NOD::SystemUTF8View gameTitle(argv[3]);
        size_t lastIdx = -1;
        auto progFunc = [&](size_t idx, const NOD::SystemString& name, size_t bytes)
        {
            if (idx != lastIdx)
            {
                lastIdx = idx;
                printf("\n");
            }
            if (bytes != -1)
                NOD::Printf(_S("\r%s %" PRISize " B"), name.c_str(), bytes);
            else
                NOD::Printf(_S("\r%s"), name.c_str());
            fflush(stdout);
        };

        bool dual = (argv[1][7] == _S('d') || argv[1][7] == _S('D'));

        if (argc < 9)
        {
            NOD::SystemString outPath(argv[4]);
            outPath.append(_S(".iso"));
            NOD::DiscBuilderWii b(outPath.c_str(), gameId.utf8_str().c_str(), gameTitle.utf8_str().c_str(), dual, progFunc);
            b.buildFromDirectory(argv[4], argv[5], argv[6], argv[7]);
        }
        else
        {
            NOD::DiscBuilderWii b(argv[8], gameId.utf8_str().c_str(), gameTitle.utf8_str().c_str(), dual, progFunc);
            b.buildFromDirectory(argv[4], argv[5], argv[6], argv[7]);
        }

        printf("\n");
    }
    else
    {
        printHelp();
        return -1;
    }

    return 0;
}

