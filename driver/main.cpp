#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#if _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include <spdlog/spdlog.h>

#include <nod/DiscBase.hpp>
#include <nod/DiscGCN.hpp>
#include <nod/DiscWii.hpp>
#include <nod/nod.hpp>
#include "../lib/Util.hpp"

static void printHelp() {
  fmt::print(stderr,
    "Usage:\n"
    "  nodtool extract [options] <image-in> [<dir-out>]\n"
    "  nodtool makegcn [options] <fsroot-in> [<image-out>]\n"
    "  nodtool makewii [options] <fsroot-in> [<image-out>]\n"
    "  nodtool mergegcn [options] <fsroot-in> <image-in> [<image-out>]\n"
    "  nodtool mergewii [options] <fsroot-in> <image-in> [<image-out>]\n"
    "Options:\n"
    "  -f         Force (extract only)\n"
    "  -v         Verbose details (extract only).\n");
}

#if _MSC_VER
#include <nowide/args.hpp>

#define PRISize "Iu"
int main(int argc, char* argv[]) {
  nowide::args _(argc, argv);
#else
#define PRISize "zu"
int main(int argc, char* argv[]) {
#endif
  int argidx = 1;
  std::string errand;
  bool verbose = false;
  nod::ExtractionContext ctx = {true, [&](std::string_view str, float c) {
                                  if (verbose)
                                    fmt::print(stderr, "Current node: {}, Extraction {:g}% Complete\n", str,
                                               c * 100.f);
                                }};
  while (argidx < argc) {
    if (!nod::StrCaseCmp(argv[argidx], "-f")) {
      ctx.force = true;
      ++argidx;
      continue;
    } else if (!nod::StrCaseCmp(argv[argidx], "-v")) {
      verbose = true;
      ++argidx;
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

  auto progFunc = [&](float prog, std::string_view name, size_t bytes) {
    fmt::print("\r                                                                      ");
    if (bytes != SIZE_MAX)
      fmt::print("\r{:g}% {} {} B", prog * 100.f, name, bytes);
    else
      fmt::print("\r{:g}% {}", prog * 100.f, name);
    fflush(stdout);
  };

  if (errand == "extract") {
    std::string imageIn;
    std::string dirOut;
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
      dirOut = ".";

    bool isWii;
    std::unique_ptr<nod::DiscBase> disc = nod::OpenDiscFromImage(imageIn, isWii);
    if (!disc)
      return 1;

    nod::Mkdir(dirOut.c_str(), 0755);

    nod::IPartition* dataPart = disc->getDataPartition();
    if (!dataPart)
      return 1;

    if (!dataPart->extractToDirectory(dirOut, ctx))
      return 1;
  } else if (errand == "makegcn") {
    std::string fsrootIn;
    std::string imageOut;
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
      imageOut = fsrootIn + ".gcm";

    /* Pre-validate path */
    nod::Sstat theStat;
    if (nod::Stat(fsrootIn.c_str(), &theStat) || !S_ISDIR(theStat.st_mode)) {
      spdlog::error("unable to stat {} as directory");
      return 1;
    }

    if (!nod::DiscBuilderGCN::CalculateTotalSizeRequired(fsrootIn))
      return 1;

    nod::EBuildResult ret;

    nod::DiscBuilderGCN b(imageOut, progFunc);
    ret = b.buildFromDirectory(fsrootIn);

    fmt::print("\n");
    if (ret != nod::EBuildResult::Success)
      return 1;
  } else if (errand == "makewii") {
    std::string fsrootIn;
    std::string imageOut;
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
      imageOut = fsrootIn + ".iso";

    /* Pre-validate path */
    nod::Sstat theStat;
    if (nod::Stat(fsrootIn.c_str(), &theStat) || !S_ISDIR(theStat.st_mode)) {
      spdlog::error("unable to stat {} as directory");
      return 1;
    }

    bool dual = false;
    if (!nod::DiscBuilderWii::CalculateTotalSizeRequired(fsrootIn, dual))
      return 1;

    nod::EBuildResult ret;

    nod::DiscBuilderWii b(imageOut, dual, progFunc);
    ret = b.buildFromDirectory(fsrootIn);

    fmt::print("\n");
    if (ret != nod::EBuildResult::Success)
      return 1;
  } else if (errand == "mergegcn") {
    std::string fsrootIn;
    std::string imageIn;
    std::string imageOut;
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
      imageOut = fsrootIn + ".gcm";

    /* Pre-validate paths */
    nod::Sstat theStat;
    if (nod::Stat(fsrootIn.c_str(), &theStat) || !S_ISDIR(theStat.st_mode)) {
      spdlog::error("unable to stat {} as directory");
      return 1;
    }
    if (nod::Stat(imageIn.c_str(), &theStat) || !S_ISREG(theStat.st_mode)) {
      spdlog::error("unable to stat {} as file");
      return 1;
    }

    bool isWii;
    std::unique_ptr<nod::DiscBase> disc = nod::OpenDiscFromImage(imageIn, isWii);
    if (!disc) {
      spdlog::error("unable to open image {}");
      return 1;
    }
    if (isWii) {
      spdlog::error("Wii images should be merged with 'mergewii'");
      return 1;
    }

    if (!nod::DiscMergerGCN::CalculateTotalSizeRequired(static_cast<nod::DiscGCN&>(*disc), fsrootIn))
      return 1;

    nod::EBuildResult ret;

    nod::DiscMergerGCN b(imageOut, static_cast<nod::DiscGCN&>(*disc), progFunc);
    ret = b.mergeFromDirectory(fsrootIn);

    fmt::print("\n");
    if (ret != nod::EBuildResult::Success)
      return 1;
  } else if (errand == "mergewii") {
    std::string fsrootIn;
    std::string imageIn;
    std::string imageOut;
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
      imageOut = fsrootIn + ".iso";

    /* Pre-validate paths */
    nod::Sstat theStat;
    if (nod::Stat(fsrootIn.c_str(), &theStat) || !S_ISDIR(theStat.st_mode)) {
      spdlog::error("unable to stat {} as directory");
      return 1;
    }
    if (nod::Stat(imageIn.c_str(), &theStat) || !S_ISREG(theStat.st_mode)) {
      spdlog::error("unable to stat {} as file");
      return 1;
    }

    bool isWii;
    std::unique_ptr<nod::DiscBase> disc = nod::OpenDiscFromImage(imageIn, isWii);
    if (!disc) {
      spdlog::error("unable to open image {}");
      return 1;
    }
    if (!isWii) {
      spdlog::error("GameCube images should be merged with 'mergegcn'");
      return 1;
    }

    bool dual = false;
    if (!nod::DiscMergerWii::CalculateTotalSizeRequired(static_cast<nod::DiscWii&>(*disc), fsrootIn, dual))
      return 1;

    nod::EBuildResult ret;

    nod::DiscMergerWii b(imageOut, static_cast<nod::DiscWii&>(*disc), dual, progFunc);
    ret = b.mergeFromDirectory(fsrootIn);

    fmt::print("\n");
    if (ret != nod::EBuildResult::Success)
      return 1;
  } else {
    printHelp();
    return 1;
  }

  spdlog::info("Success!");
  return 0;
}
