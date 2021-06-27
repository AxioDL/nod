#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#if _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include <logvisor/logvisor.hpp>

#include <nod/DiscBase.hpp>
#include <nod/DiscGCN.hpp>
#include <nod/DiscWii.hpp>
#include <nod/nod.hpp>


constexpr std::array<nod::Codepage_t, 3> codepages = {
  CP_US_ASCII,
  CP_UTF8,
  CP_SHIFT_JIS,
};


static void printHelp() {
  fmt::print(stderr, FMT_STRING(_SYS_STR(
    "Usage:\n"
    "  nodtool extract [options] <image-in> [<dir-out>]\n"
    "  nodtool makegcn [options] <fsroot-in> [<image-out>]\n"
    "  nodtool makewii [options] <fsroot-in> [<image-out>]\n"
    "  nodtool mergegcn [options] <fsroot-in> <image-in> [<image-out>]\n"
    "  nodtool mergewii [options] <fsroot-in> <image-in> [<image-out>]\n"
    "Options:\n"
    "  -f         Force (extract only)\n"
    "  -v         Verbose details (extract only).\n"
    "  -c <val>   Set multi-byte character set of image(s).\n"
    "Available codepage values:\n"
    "  0          7-bit ASCII (default)\n"
    "  1          UTF-8\n"
    "  2          Shift-JIS\n")));
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
  /* Enable logging to console */
  logvisor::RegisterStandardExceptions();
  logvisor::RegisterConsoleLogger();
#if _WIN32
  _setmode(_fileno(stdin), _O_U16TEXT);
  _setmode(_fileno(stdout), _O_U16TEXT);
  _setmode(_fileno(stderr), _O_U16TEXT);
#endif

  int argidx = 1;
  nod::SystemString errand;
  bool verbose = false;
  nod::Codepage_t discLocale = CP_US_ASCII;
  nod::ExtractionContext ctx = {true, [&](nod::SystemStringView str, float c) {
                                  if (verbose)
                                    fmt::print(stderr, FMT_STRING(_SYS_STR("Current node: {}, Extraction {:g}% Complete\n")),
                                               str, c * 100.f);
                                }};
  while (argidx < argc) {
    if (!strcasecmp(argv[argidx], _SYS_STR("-f"))) {
      ctx.force = true;
      ++argidx;
      continue;
    } else if (!strcasecmp(argv[argidx], _SYS_STR("-v"))) {
      verbose = true;
      ++argidx;
      continue;
    } else if (!strcasecmp(argv[argidx], _SYS_STR("-c"))) {
      if (argidx+1 < argc) {
        unsigned long cpidx = nod::StrToUL(argv[argidx + 1], NULL, 0);
        if (cpidx > codepages.size() - 1)
          nod::LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("Unavailable codepage: {}")), cpidx);
        discLocale = codepages[cpidx];
      } else {
        printHelp();
        return 1;
      }
      argidx += 2;
      continue;
    } else if (errand.empty()) {
      errand = argv[argidx];
      ++argidx;
      continue;
    } else {
      break;
    }
  }

  if (errand.empty()) {
    printHelp();
    return 1;
  }

  auto progFunc = [&](float prog, nod::SystemStringView name, size_t bytes) {
    fmt::print(FMT_STRING(_SYS_STR("\r                                                                      ")));
    if (bytes != SIZE_MAX)
      fmt::print(FMT_STRING(_SYS_STR("\r{:g}% {} {} B")), prog * 100.f, name, bytes);
    else
      fmt::print(FMT_STRING(_SYS_STR("\r{:g}% {}")), prog * 100.f, name);
    fflush(stdout);
  };

  if (errand == _SYS_STR("extract")) {
    nod::SystemString imageIn;
    nod::SystemString dirOut;
    while (argidx < argc) {
      if (imageIn.empty()) {
        imageIn = argv[argidx];
        ++argidx;
        continue;
      } else if (dirOut.empty()) {
        dirOut = argv[argidx];
        ++argidx;
        continue;
      } else {
        printHelp();
        return 1;
      }
    }
    if (dirOut.empty())
      dirOut = _SYS_STR(".");

    bool isWii;
    std::unique_ptr<nod::DiscBase> disc = nod::OpenDiscFromImage(imageIn, isWii, discLocale);
    if (!disc)
      return 1;

    nod::Mkdir(dirOut.c_str(), 0755);

    nod::IPartition* dataPart = disc->getDataPartition();
    if (!dataPart)
      return 1;

    if (!dataPart->extractToDirectory(dirOut, ctx))
      return 1;
  } else if (errand == _SYS_STR("makegcn")) {
    nod::SystemString fsrootIn;
    nod::SystemString imageOut;
    while (argidx < argc) {
      if (fsrootIn.empty()) {
        fsrootIn = argv[argidx];
        ++argidx;
        continue;
      } else if (imageOut.empty()) {
        imageOut = argv[argidx];
        ++argidx;
        continue;
      } else {
        printHelp();
        return 1;
      }
    }
    if (imageOut.empty())
      imageOut = fsrootIn + _SYS_STR(".gcm");

    /* Pre-validate path */
    nod::Sstat theStat;
    if (nod::Stat(fsrootIn.c_str(), &theStat) || !S_ISDIR(theStat.st_mode)) {
      nod::LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to stat {} as directory")), fsrootIn);
      return 1;
    }

    if (!nod::DiscBuilderGCN::CalculateTotalSizeRequired(fsrootIn, discLocale))
      return 1;

    nod::EBuildResult ret;

    nod::DiscBuilderGCN b(imageOut, progFunc, discLocale);
    ret = b.buildFromDirectory(fsrootIn);

    fmt::print(FMT_STRING(_SYS_STR("\n")));
    if (ret != nod::EBuildResult::Success)
      return 1;
  } else if (errand == _SYS_STR("makewii")) {
    nod::SystemString fsrootIn;
    nod::SystemString imageOut;
    while (argidx < argc) {
      if (fsrootIn.empty()) {
        fsrootIn = argv[argidx];
        ++argidx;
        continue;
      } else if (imageOut.empty()) {
        imageOut = argv[argidx];
        ++argidx;
        continue;
      } else {
        printHelp();
        return 1;
      }
    }
    if (imageOut.empty())
      imageOut = fsrootIn + _SYS_STR(".iso");

    /* Pre-validate path */
    nod::Sstat theStat;
    if (nod::Stat(fsrootIn.c_str(), &theStat) || !S_ISDIR(theStat.st_mode)) {
      nod::LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to stat {} as directory")), fsrootIn);
      return 1;
    }

    bool dual = false;
    if (!nod::DiscBuilderWii::CalculateTotalSizeRequired(fsrootIn, dual, discLocale))
      return 1;

    nod::EBuildResult ret;

    nod::DiscBuilderWii b(imageOut, dual, progFunc, discLocale);
    ret = b.buildFromDirectory(fsrootIn);

    fmt::print(FMT_STRING(_SYS_STR("\n")));
    if (ret != nod::EBuildResult::Success)
      return 1;
  } else if (errand == _SYS_STR("mergegcn")) {
    nod::SystemString fsrootIn;
    nod::SystemString imageIn;
    nod::SystemString imageOut;
    while (argidx < argc) {
      if (fsrootIn.empty()) {
        fsrootIn = argv[argidx];
        ++argidx;
        continue;
      } else if (imageIn.empty()) {
        imageIn = argv[argidx];
        ++argidx;
        continue;
      } else if (imageOut.empty()) {
        imageOut = argv[argidx];
        ++argidx;
        continue;
      } else {
        printHelp();
        return 1;
      }
    }
    if (imageOut.empty())
      imageOut = fsrootIn + _SYS_STR(".gcm");

    /* Pre-validate paths */
    nod::Sstat theStat;
    if (nod::Stat(fsrootIn.c_str(), &theStat) || !S_ISDIR(theStat.st_mode)) {
      nod::LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to stat {} as directory")), fsrootIn);
      return 1;
    }
    if (nod::Stat(imageIn.c_str(), &theStat) || !S_ISREG(theStat.st_mode)) {
      nod::LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to stat {} as file")), imageIn);
      return 1;
    }

    bool isWii;
    std::unique_ptr<nod::DiscBase> disc = nod::OpenDiscFromImage(imageIn, isWii, discLocale);
    if (!disc) {
      nod::LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to open image {}")), imageIn);
      return 1;
    }
    if (isWii) {
      nod::LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("Wii images should be merged with 'mergewii'")));
      return 1;
    }

    if (!nod::DiscMergerGCN::CalculateTotalSizeRequired(static_cast<nod::DiscGCN&>(*disc), fsrootIn, discLocale))
      return 1;

    nod::EBuildResult ret;

    nod::DiscMergerGCN b(imageOut, static_cast<nod::DiscGCN&>(*disc), progFunc, discLocale);
    ret = b.mergeFromDirectory(fsrootIn);

    fmt::print(FMT_STRING(_SYS_STR("\n")));
    if (ret != nod::EBuildResult::Success)
      return 1;
  } else if (errand == _SYS_STR("mergewii")) {
    nod::SystemString fsrootIn;
    nod::SystemString imageIn;
    nod::SystemString imageOut;
    while (argidx < argc) {
      if (fsrootIn.empty()) {
        fsrootIn = argv[argidx];
        ++argidx;
        continue;
      } else if (imageIn.empty()) {
        imageIn = argv[argidx];
        ++argidx;
        continue;
      } else if (imageOut.empty()) {
        imageOut = argv[argidx];
        ++argidx;
        continue;
      } else {
        printHelp();
        return 1;
      }
    }
    if (imageOut.empty())
      imageOut = fsrootIn + _SYS_STR(".iso");

    /* Pre-validate paths */
    nod::Sstat theStat;
    if (nod::Stat(fsrootIn.c_str(), &theStat) || !S_ISDIR(theStat.st_mode)) {
      nod::LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to stat {} as directory")), fsrootIn);
      return 1;
    }
    if (nod::Stat(imageIn.c_str(), &theStat) || !S_ISREG(theStat.st_mode)) {
      nod::LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to stat {} as file")), imageIn);
      return 1;
    }

    bool isWii;
    std::unique_ptr<nod::DiscBase> disc = nod::OpenDiscFromImage(imageIn, isWii, discLocale);
    if (!disc) {
      nod::LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("unable to open image {}")), argv[3]);
      return 1;
    }
    if (!isWii) {
      nod::LogModule.report(logvisor::Error, FMT_STRING(_SYS_STR("GameCube images should be merged with 'mergegcn'")));
      return 1;
    }

    bool dual = false;
    if (!nod::DiscMergerWii::CalculateTotalSizeRequired(static_cast<nod::DiscWii&>(*disc), fsrootIn, dual, discLocale))
      return 1;

    nod::EBuildResult ret;

    nod::DiscMergerWii b(imageOut, static_cast<nod::DiscWii&>(*disc), dual, progFunc, discLocale);
    ret = b.mergeFromDirectory(fsrootIn);

    fmt::print(FMT_STRING(_SYS_STR("\n")));
    if (ret != nod::EBuildResult::Success)
      return 1;
  } else {
    printHelp();
    return 1;
  }
  
  nod::LogModule.report(logvisor::Info, FMT_STRING(_SYS_STR("Success!")));
  return 0;
}
